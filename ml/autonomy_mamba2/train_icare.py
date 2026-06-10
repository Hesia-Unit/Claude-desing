"""Train Icare on enriched mission autonomy records."""

from __future__ import annotations

import argparse
import dataclasses
import json
import time
import hashlib
from pathlib import Path
from typing import Any

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, Dataset

from .features_icare import ICARE_MODES, command_to_vector, pack_icare_record
from .icare_controller import DEFAULT_LANGUAGE_MODEL, IcareConfig, IcareDecisionModel, icare_parameter_count


class IcareJsonlDataset(Dataset[tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, str]]):
    def __init__(self, path: Path, sequence_len: int) -> None:
        self.records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
        if not self.records:
            raise ValueError(f"empty dataset: {path}")
        self.sequence_len = sequence_len

    def __len__(self) -> int:
        return max(1, len(self.records) - self.sequence_len + 1)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, str]:
        seq = self.records[idx : idx + self.sequence_len]
        if len(seq) < self.sequence_len:
            seq = seq + [seq[-1]] * (self.sequence_len - len(seq))
        features = np.stack([pack_icare_record(record) for record in seq], axis=0)
        commands = []
        modes = []
        complete = []
        for record in seq:
            cmd = record.get("expert_command") or {}
            commands.append(command_to_vector(cmd))
            modes.append(ICARE_MODES.get(str(cmd.get("mode", "hold")), 0))
            complete.append(1.0 if cmd.get("mission_complete") else 0.0)
        return (
            torch.tensor(features, dtype=torch.float32),
            torch.tensor(np.stack(commands), dtype=torch.float32),
            torch.tensor(modes, dtype=torch.long),
            torch.tensor(complete, dtype=torch.float32),
            str(seq[-1].get("mission_text", "")),
        )


@torch.no_grad()
def build_mission_embeddings(
    texts: list[str],
    model_path: Path,
    device: torch.device,
    max_tokens: int,
) -> dict[str, torch.Tensor]:
    from transformers import AutoModelForCausalLM, AutoTokenizer

    tokenizer = AutoTokenizer.from_pretrained(str(model_path), local_files_only=True)
    lm = AutoModelForCausalLM.from_pretrained(
        str(model_path),
        local_files_only=True,
        dtype=torch.float16 if device.type == "cuda" else torch.float32,
        device_map=str(device),
        low_cpu_mem_usage=True,
    ).eval()
    cache: dict[str, torch.Tensor] = {}
    for text in sorted(set(texts)):
        enc = tokenizer(text or "Mission UAV.", return_tensors="pt", truncation=True, max_length=max_tokens).to(device)
        out = lm(**enc, output_hidden_states=True)
        hidden = out.hidden_states[-1][:, -1, :].detach().float().cpu().squeeze(0)
        cache[text] = hidden
    del lm
    if device.type == "cuda":
        torch.cuda.empty_cache()
    return cache


def collate_with_embeddings(
    batch: list[tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, str]],
    embedding_cache: dict[str, torch.Tensor],
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    features, commands, modes, complete, texts = zip(*batch, strict=True)
    mission = torch.stack([embedding_cache[text] for text in texts], dim=0)
    return torch.stack(features), torch.stack(commands), torch.stack(modes), torch.stack(complete), mission


def embedding_cache_key(texts: list[str], model_path: Path, max_tokens: int) -> str:
    h = hashlib.sha256()
    h.update(str(model_path).encode("utf-8"))
    h.update(str(max_tokens).encode("utf-8"))
    for text in sorted(set(texts)):
        h.update(text.encode("utf-8"))
        h.update(b"\0")
    return h.hexdigest()


@torch.no_grad()
def evaluate(model: IcareDecisionModel, loader: DataLoader, device: torch.device) -> dict[str, float]:
    model.eval()
    total_action = 0.0
    total_correct = 0
    total_complete_correct = 0
    total_tokens = 0
    for features, command, mode, complete, mission in loader:
        features = features.to(device)
        command = command.to(device)
        mode = mode.to(device)
        complete = complete.to(device)
        mission = mission.to(device)
        out = model(features, mission)
        total_action += float(torch.abs(out["command"] - command).mean().detach().cpu()) * mode.numel()  # type: ignore[operator]
        total_correct += int((out["mode_logits"].argmax(dim=-1) == mode).sum().detach().cpu())  # type: ignore[index]
        complete_pred = (torch.sigmoid(out["complete_logit"]) > 0.5).float()  # type: ignore[arg-type]
        total_complete_correct += int((complete_pred == complete).sum().detach().cpu())
        total_tokens += int(mode.numel())
    model.train()
    return {
        "action_mae": total_action / max(total_tokens, 1),
        "mode_accuracy": total_correct / max(total_tokens, 1),
        "mission_complete_accuracy": total_complete_correct / max(total_tokens, 1),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1/train.jsonl"))
    parser.add_argument("--val-dataset", type=Path, default=Path("F:/Set-Donner/datasets/icare_autonomy_v1/val.jsonl"))
    parser.add_argument("--language-model", type=Path, default=DEFAULT_LANGUAGE_MODEL)
    parser.add_argument("--out-dir", type=Path, default=Path("artifacts/icare/train"))
    parser.add_argument("--sequence-len", type=int, default=12)
    parser.add_argument("--batch-size", type=int, default=8)
    parser.add_argument("--steps", type=int, default=300)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--max-mission-tokens", type=int, default=96)
    parser.add_argument("--embedding-cache", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    train_ds = IcareJsonlDataset(args.dataset, args.sequence_len)
    val_ds = IcareJsonlDataset(args.val_dataset, args.sequence_len) if args.val_dataset.exists() else None
    texts = [str(record.get("mission_text", "")) for record in train_ds.records]
    if val_ds:
        texts.extend(str(record.get("mission_text", "")) for record in val_ds.records)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    cache_path = args.embedding_cache or (args.out_dir / "mission_embeddings.pt")
    cache_key = embedding_cache_key(texts, args.language_model, args.max_mission_tokens)
    t0 = time.perf_counter()
    embeddings = None
    if cache_path.exists():
        blob = torch.load(cache_path, map_location="cpu", weights_only=False)
        if blob.get("cache_key") == cache_key:
            embeddings = blob.get("embeddings")
    if embeddings is None:
        embeddings = build_mission_embeddings(texts, args.language_model, device, args.max_mission_tokens)
        torch.save({"cache_key": cache_key, "language_model": str(args.language_model), "embeddings": embeddings}, cache_path)
    embed_sec = time.perf_counter() - t0
    mission_dim = len(next(iter(embeddings.values())))

    collate = lambda batch: collate_with_embeddings(batch, embeddings)
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, drop_last=False, collate_fn=collate)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size, shuffle=False, drop_last=False, collate_fn=collate) if val_ds else None

    cfg = IcareConfig(mission_dim=mission_dim)
    model = IcareDecisionModel(cfg).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    action_loss = nn.SmoothL1Loss()
    mode_loss = nn.CrossEntropyLoss()
    complete_loss = nn.BCEWithLogitsLoss()

    if args.dry_run:
        features, command, mode, complete, mission = next(iter(train_loader))
        with torch.no_grad():
            out = model(features.to(device), mission.to(device))
        payload = {
            "schema": "hesia.icare.train.dry_run.v1",
            "status": "passed",
            "language_model": str(args.language_model),
            "mission_embedding_dim": mission_dim,
            "embedding_sec": embed_sec,
            "feature_shape": list(features.shape),
            "command_shape": list(command.shape),
            "mode_shape": list(mode.shape),
            "complete_shape": list(complete.shape),
            "model_command_shape": list(out["command"].shape),
            "parameter_count": icare_parameter_count(model),
        }
        (args.out_dir / "dry_run.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(json.dumps(payload, indent=2))
        return 0

    metrics = []
    initial_validation = evaluate(model, val_loader, device) if val_loader else None
    step = 0
    model.train()
    while step < args.steps:
        for features, command, mode, complete, mission in train_loader:
            features = features.to(device)
            command = command.to(device)
            mode = mode.to(device)
            complete = complete.to(device)
            mission = mission.to(device)
            out = model(features, mission)
            loss_action = action_loss(out["command"], command)  # type: ignore[arg-type]
            loss_mode = mode_loss(out["mode_logits"].reshape(-1, cfg.mode_classes), mode.reshape(-1))  # type: ignore[index]
            loss_complete = complete_loss(out["complete_logit"], complete)  # type: ignore[arg-type]
            loss = loss_action + 0.25 * loss_mode + 0.15 * loss_complete
            opt.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            step += 1
            if step == 1 or step % 25 == 0:
                item = {"step": step, "loss": float(loss.detach().cpu()), "action_loss": float(loss_action.detach().cpu())}
                if val_loader and step % 100 == 0:
                    item.update({f"val_{key}": value for key, value in evaluate(model, val_loader, device).items()})
                metrics.append(item)
            if step >= args.steps:
                break

    checkpoint = args.out_dir / "icare_policy.pt"
    torch.save(
        {
            "schema": "hesia.icare.checkpoint.v1",
            "config": cfg.to_dict(),
            "model": model.state_dict(),
            "language_model": str(args.language_model),
            "mission_embedding_dim": mission_dim,
            "metrics": metrics,
        },
        checkpoint,
    )
    final_validation = evaluate(model, val_loader, device) if val_loader else None
    summary = {
        "schema": "hesia.icare.train.v1",
        "status": "passed",
        "checkpoint": str(checkpoint),
        "language_model": str(args.language_model),
        "steps": step,
        "embedding_sec": embed_sec,
        "parameter_count": icare_parameter_count(model),
        "initial_validation": initial_validation,
        "final_validation": final_validation,
        "metrics_tail": metrics[-10:],
    }
    (args.out_dir / "train_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
