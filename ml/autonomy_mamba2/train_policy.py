"""Supervised trainer for the Mamba-2 autonomy policy."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset

from .controller import Mamba2DecisionPolicy, Mamba2PolicyConfig
from .features import FEATURE_DIM, pack_record


MODE_TO_ID = {
    "hold": 0,
    "cruise": 1,
    "climb": 2,
    "descend": 3,
    "avoid": 4,
    "recover_stall": 5,
    "motor_loss_recovery": 6,
    "return_home": 7,
}


class AutonomyJsonlDataset(Dataset[tuple[torch.Tensor, torch.Tensor, torch.Tensor]]):
    def __init__(self, path: Path, sequence_len: int, allow_placeholder: bool = False) -> None:
        self.records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
        if not self.records:
            raise ValueError(f"empty dataset: {path}")
        bad = [
            record.get("sample_id")
            for record in self.records
            if (record.get("expert_command") or {}).get("source") == "missing_expert_placeholder"
        ]
        if bad and not allow_placeholder:
            raise ValueError(
                f"{len(bad)} samples have placeholder expert_command; collect simulator/expert labels before training"
            )
        self.sequence_len = sequence_len

    def __len__(self) -> int:
        return max(1, len(self.records) - self.sequence_len + 1)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        seq = self.records[idx : idx + self.sequence_len]
        if len(seq) < self.sequence_len:
            seq = seq + [seq[-1]] * (self.sequence_len - len(seq))
        features = np.stack([pack_record(record) for record in seq], axis=0)
        commands = []
        modes = []
        for record in seq:
            cmd = record.get("expert_command") or {}
            commands.append(
                [
                    float(cmd.get("north_mps", 0.0)),
                    float(cmd.get("east_mps", 0.0)),
                    float(cmd.get("down_mps_ned", 0.0)),
                    float(cmd.get("yaw_rate_rad_s", 0.0)),
                ]
            )
            modes.append(MODE_TO_ID.get(str(cmd.get("mode", "hold")), 0))
        return (
            torch.tensor(features, dtype=torch.float32),
            torch.tensor(commands, dtype=torch.float32).clamp(-1.0, 1.0),
            torch.tensor(modes, dtype=torch.long),
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--val-dataset", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("artifacts/autonomy_mamba2/train"))
    parser.add_argument("--sequence-len", type=int, default=16)
    parser.add_argument("--steps", type=int, default=1000)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--allow-placeholder", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    dataset = AutonomyJsonlDataset(args.dataset, args.sequence_len, allow_placeholder=args.allow_placeholder)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True, drop_last=False)
    val_loader = None
    if args.val_dataset:
        val_dataset = AutonomyJsonlDataset(args.val_dataset, args.sequence_len, allow_placeholder=args.allow_placeholder)
        val_loader = DataLoader(val_dataset, batch_size=args.batch_size, shuffle=False, drop_last=False)
    cfg = Mamba2PolicyConfig(input_dim=FEATURE_DIM)
    model = Mamba2DecisionPolicy(cfg).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    cmd_loss = nn.SmoothL1Loss()
    mode_loss = nn.CrossEntropyLoss()

    def evaluate() -> dict[str, float] | None:
        if val_loader is None:
            return None
        model.eval()
        total_loss = 0.0
        total_mae = 0.0
        total_correct = 0
        total_tokens = 0
        with torch.no_grad():
            for features, command, mode in val_loader:
                features = features.to(device)
                command = command.to(device)
                mode = mode.to(device)
                out = model(features)
                total_loss += float(cmd_loss(out["command"], command).detach().cpu()) * features.shape[0]
                total_mae += float(torch.mean(torch.abs(out["command"] - command)).detach().cpu()) * features.numel() / features.shape[-1]
                pred_mode = out["mode_logits"].argmax(dim=-1)
                total_correct += int((pred_mode == mode).sum().detach().cpu())
                total_tokens += int(mode.numel())
        model.train()
        denom_batches = max(1, len(val_loader.dataset))  # type: ignore[arg-type]
        return {
            "command_smooth_l1": total_loss / denom_batches,
            "command_mae": total_mae / max(total_tokens, 1),
            "mode_accuracy": total_correct / max(total_tokens, 1),
        }

    if args.dry_run:
        features, command, mode = next(iter(loader))
        with torch.no_grad():
            out = model(features.to(device))
        payload = {
            "schema": "hesia.autonomy.train_policy.dry_run.v1",
            "status": "passed",
            "samples": len(dataset.records),
            "sequence_len": args.sequence_len,
            "feature_shape": list(features.shape),
            "command_shape": list(command.shape),
            "mode_shape": list(mode.shape),
            "model_command_shape": list(out["command"].shape),
            "validation": evaluate(),
        }
        args.out_dir.mkdir(parents=True, exist_ok=True)
        (args.out_dir / "dry_run.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(json.dumps(payload, indent=2))
        return 0

    args.out_dir.mkdir(parents=True, exist_ok=True)
    model.train()
    step = 0
    metrics: list[dict[str, float]] = []
    initial_validation = evaluate()
    while step < args.steps:
        for features, command, mode in loader:
            features = features.to(device)
            command = command.to(device)
            mode = mode.to(device)
            out = model(features)
            loss_command = cmd_loss(out["command"], command)
            loss_mode = mode_loss(out["mode_logits"].reshape(-1, cfg.mode_classes), mode.reshape(-1))
            loss = loss_command + 0.2 * loss_mode
            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            step += 1
            if step % 25 == 0 or step == 1:
                item = {"step": float(step), "loss": float(loss.detach().cpu())}
                if step % 100 == 0:
                    val = evaluate()
                    if val:
                        item.update({f"val_{key}": float(value) for key, value in val.items()})
                metrics.append(item)
            if step >= args.steps:
                break

    checkpoint = args.out_dir / "mamba2_policy.pt"
    torch.save({"config": cfg.to_dict(), "model": model.state_dict(), "metrics": metrics}, checkpoint)
    final_validation = evaluate()
    summary = {
        "schema": "hesia.autonomy.train_policy.v1",
        "status": "passed",
        "steps": step,
        "checkpoint": str(checkpoint),
        "initial_validation": initial_validation,
        "final_validation": final_validation,
        "metrics": metrics[-10:],
    }
    (args.out_dir / "train_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
