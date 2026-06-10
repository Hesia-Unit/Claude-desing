"""Evaluation + verification video for a trained Icare PPO policy.

Compares the trained policy against the base checkpoint and the analytic expert
on the GPU-vectorised mission, and renders a top-down verification flight with an
Icare command/telemetry terminal overlay (the policy consumes 96-d perception
features, so the render is a schematic top-down, not photoreal).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import cv2
import imageio.v2 as imageio
import numpy as np
import torch

from .features_icare import ICARE_ACTION_DIM, ID_TO_ICARE_MODE
from .icare_controller import IcareConfig, IcareDecisionModel
from .icare_env_vec import IcareVecConfig, IcareVecEnv

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EMBED = ROOT / "artifacts" / "icare" / "train_1_3b" / "mission_embeddings.pt"


def load_policy(checkpoint: Path, device: torch.device) -> tuple[IcareDecisionModel, int]:
    ckpt = torch.load(checkpoint, map_location="cpu", weights_only=False)
    model = IcareDecisionModel(IcareConfig(**ckpt["config"]))
    model.load_state_dict(ckpt["model"])
    model.to(device).eval()
    return model, int(ckpt.get("window", 12))


def load_mission(path: Path, device: torch.device) -> torch.Tensor:
    blob = torch.load(path, map_location="cpu", weights_only=False)
    emb = blob["embeddings"]
    return emb[sorted(emb)[0]][None, :].to(device)


@torch.no_grad()
def evaluate(policy_fn, n: int, horizon: int, device: torch.device, seed: int) -> dict[str, float]:
    env = IcareVecEnv(IcareVecConfig(num_envs=n, horizon=horizon, device=str(device), seed=seed))
    env.reset_all()
    done = torch.zeros(n, dtype=torch.bool, device=device)
    reasons = torch.zeros(n, dtype=torch.long, device=device)
    totals = torch.zeros(n, device=device)
    final = torch.full((n,), 999.0, device=device)
    win = env.features()[:, None, :].repeat(1, 12, 1)
    for _ in range(horizon):
        a = policy_fn(env, win)
        r, d, info = env.step(a)
        live = ~done
        totals += r * live.float()
        nd = d & live
        reasons[nd] = info["reason"][nd]
        final[nd] = info["distance"][nd]
        done = done | d
        win = torch.cat([win[:, 1:, :], env.features()[:, None, :]], dim=1)
        if done.all():
            break
    final[~done] = info["distance"][~done]
    return {
        "episodes": n,
        "success_rate": float((reasons == 1).float().mean()),
        "collision_rate": float((reasons == 2).float().mean()),
        "ground_rate": float((reasons == 3).float().mean()),
        "battery_empty_rate": float((reasons == 4).float().mean()),
        "timeout_rate": float((reasons == 5).float().mean()),
        "mean_reward": float(totals.mean()),
        "mean_final_distance_m": float(final.mean()),
    }


def policy_from_model(model: IcareDecisionModel, mission: torch.Tensor, window: int):
    @torch.no_grad()
    def fn(env: IcareVecEnv, win: torch.Tensor) -> torch.Tensor:
        out = model(win[:, -window:, :], mission.expand(win.shape[0], -1))
        return out["command"][:, -1, :].clamp(-1, 1)
    return fn


def _pt(v, size, scale):
    return (int(size / 2 + float(v[1]) * scale), int(size / 2 - float(v[0]) * scale))


@torch.no_grad()
def make_video(model: IcareDecisionModel, mission: torch.Tensor, window: int, device: torch.device,
               out_path: Path, seed: int, horizon: int = 160, size: int = 520) -> Path:
    env = IcareVecEnv(IcareVecConfig(num_envs=1, horizon=horizon, device=str(device), seed=seed))
    env.reset_all()
    win = env.features()[:, None, :].repeat(1, 12, 1)
    frames: list[np.ndarray] = []
    scale = size / 1000.0
    info = {"distance": torch.tensor([999.0]), "clearance": torch.tensor([99.0]),
            "battery": env.battery.clone(), "altitude": env.altitude.clone(), "reason": torch.tensor([0])}
    for step in range(horizon):
        out = model(win[:, -window:, :], mission)
        action = out["command"][:, -1, :].clamp(-1, 1)
        mode = int(out["mode_logits"][:, -1, :].argmax(-1).item())
        complete = float(torch.sigmoid(out["complete_logit"][:, -1]).item())
        a = action[0].cpu().numpy()

        canvas = np.full((size, size, 3), (31, 36, 43), dtype=np.uint8)
        pos = env.pos[0].cpu().numpy(); vel = env.vel[0].cpu().numpy()
        cv2.circle(canvas, _pt(env.home[0].cpu().numpy(), size, scale), 9, (255, 255, 255), -1)
        cv2.circle(canvas, _pt(env.target[0].cpu().numpy(), size, scale), 11, (80, 210, 255), -1)
        for o, rad in zip(env.obstacles[0].cpu().numpy(), env.obstacle_radius[0].cpu().numpy()):
            cv2.circle(canvas, _pt(o, size, scale), max(5, int(float(rad) * scale)), (50, 105, 180), -1)
        cv2.arrowedLine(canvas, _pt(pos, size, scale), _pt(pos + vel * 5.0, size, scale), (90, 255, 150), 2, tipLength=0.3)
        cv2.circle(canvas, _pt(pos, size, scale), 8, (70, 255, 130), -1)
        phase = "return_home" if int(env.phase[0]) == 1 else "outbound"
        cv2.putText(canvas, f"{phase} alt={float(env.altitude[0]):.0f}m bat={float(env.battery[0])*100:.0f}%",
                    (12, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.56, (245, 245, 245), 1, cv2.LINE_AA)

        term = np.full((size, size, 3), (12, 15, 18), dtype=np.uint8)
        lines = [
            "ICARE (Mamba-2) PPO POLICY",
            f"step={step:03d} mode={ID_TO_ICARE_MODE.get(mode,'?')} complete={complete:.2f}",
            f"vel N/E/D = {a[0]:+.2f}/{a[1]:+.2f}/{a[2]:+.2f}  yaw={a[3]:+.2f}",
            f"motors = {','.join(f'{v:.2f}' for v in (a[4:8]+1)/2)}",
            f"dist={float(info['distance'][0]):.0f}m clear={float(info['clearance'][0]):.0f}m",
            f"alt={float(env.altitude[0]):.0f}m  bat={float(env.battery[0])*100:.0f}%",
        ]
        for i, ln in enumerate(lines):
            cv2.putText(term, ln, (16, 34 + i * 30), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (170, 235, 180), 1, cv2.LINE_AA)
        combined = np.concatenate([canvas, term], axis=1)
        frames.append(cv2.cvtColor(combined, cv2.COLOR_BGR2RGB))

        _, d, info = env.step(action)
        win = torch.cat([win[:, 1:, :], env.features()[:, None, :]], dim=1)
        if bool(d.item()):
            reason = ID_TO_ICARE_MODE.get(0, "")
            rs = int(info["reason"][0])
            label = {1: "MISSION COMPLETE", 2: "COLLISION", 3: "GROUND", 4: "BATTERY", 5: "TIMEOUT"}.get(rs, "")
            for _ in range(14):
                f = frames[-1].copy()
                cv2.putText(f, label, (size // 2 - 120, size - 24), cv2.FONT_HERSHEY_SIMPLEX, 0.9,
                            (90, 255, 130) if rs == 1 else (90, 90, 255), 2, cv2.LINE_AA)
                frames.append(f)
            break
    out_path.parent.mkdir(parents=True, exist_ok=True)
    imageio.mimsave(out_path, frames, fps=15, quality=8)
    return out_path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--checkpoint", type=Path, required=True)
    p.add_argument("--base-checkpoint", type=Path, default=ROOT / "artifacts" / "icare" / "train_1_3b" / "icare_policy.pt")
    p.add_argument("--embedding-cache", type=Path, default=DEFAULT_EMBED)
    p.add_argument("--out-dir", type=Path, default=None)
    p.add_argument("--episodes", type=int, default=1024)
    p.add_argument("--horizon", type=int, default=120)
    p.add_argument("--seed", type=int, default=20260610)
    p.add_argument("--videos", type=int, default=4)
    args = p.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    out_dir = args.out_dir or args.checkpoint.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    mission = load_mission(args.embedding_cache, device)

    trained, window = load_policy(args.checkpoint, device)
    results: dict[str, Any] = {}
    results["trained"] = evaluate(policy_from_model(trained, mission, window), args.episodes, args.horizon, device, args.seed)
    results["expert"] = evaluate(lambda e, w: e.expert_action(), args.episodes, args.horizon, device, args.seed)
    if args.base_checkpoint.exists():
        base, bw = load_policy(args.base_checkpoint, device)
        results["base"] = evaluate(policy_from_model(base, mission, bw), args.episodes, args.horizon, device, args.seed)

    print(json.dumps({"evaluation": results}, indent=2))
    (out_dir / "evaluation_compare.json").write_text(json.dumps(results, indent=2), encoding="utf-8")

    videos = []
    for i in range(args.videos):
        vp = make_video(trained, mission, window, device, out_dir / f"icare_flight_{i+1}.mp4", seed=args.seed + 1000 + i)
        videos.append(str(vp))
    print(json.dumps({"videos": videos}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
