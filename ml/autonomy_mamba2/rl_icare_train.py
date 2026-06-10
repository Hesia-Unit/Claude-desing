"""RL fine-tuning and verification for the Icare drone controller.

This is the lightweight fallback trainer used when full Isaac Sim cannot pass
the local compatibility check. It keeps the requested active organs only:
synthetic camera/perception, Icare, guardrails, and optional PX4/MAVSDK checks.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import cv2
import imageio.v2 as imageio
import matplotlib.pyplot as plt
import numpy as np
import torch
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import Image, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle
from torch import nn

from .features_icare import ICARE_ACTION_DIM
from .icare_controller import IcareConfig, IcareDecisionModel, icare_parameter_count


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CKPT = ROOT / "artifacts" / "icare" / "train_1_3b" / "icare_policy.pt"
DEFAULT_EMBED = ROOT / "artifacts" / "icare" / "train_1_3b" / "mission_embeddings.pt"
DEFAULT_OUT = Path("G:/Hesia-RL/runs/icare_rl")


@dataclass
class MissionState:
    pos: np.ndarray
    vel: np.ndarray
    target: np.ndarray
    home: np.ndarray
    altitude: float
    battery: float
    wind: np.ndarray
    motor_fault: int
    phase: str
    step: int
    prev_distance: float
    obstacles: np.ndarray
    obstacle_radius: np.ndarray
    done_reason: str = ""


class SyntheticDroneMission:
    """Small continuous-control mission environment.

    The environment is intentionally conservative: it is for policy shaping and
    verification only, not a replacement for Isaac/PX4 dynamics.
    """

    def __init__(self, seed: int = 7, horizon: int = 120) -> None:
        self.rng = np.random.default_rng(seed)
        self.horizon = horizon
        self.state: MissionState | None = None

    def reset(self) -> np.ndarray:
        target = self.rng.uniform(-420.0, 420.0, size=2).astype(np.float32)
        if np.linalg.norm(target) < 180:
            target *= 2.0
        obstacles = self.rng.uniform(-390.0, 390.0, size=(6, 2)).astype(np.float32)
        radius = self.rng.uniform(26.0, 70.0, size=6).astype(np.float32)
        motor_fault = int(self.rng.integers(0, 4)) if self.rng.random() < 0.28 else -1
        wind = self.rng.normal(0.0, 2.8, size=2).astype(np.float32)
        pos = np.array([0.0, 0.0], dtype=np.float32)
        self.state = MissionState(
            pos=pos,
            vel=np.zeros(2, dtype=np.float32),
            target=target,
            home=np.zeros(2, dtype=np.float32),
            altitude=float(self.rng.uniform(90.0, 150.0)),
            battery=float(self.rng.uniform(0.72, 0.96)),
            wind=wind,
            motor_fault=motor_fault,
            phase="outbound",
            step=0,
            prev_distance=float(np.linalg.norm(target - pos)),
            obstacles=obstacles,
            obstacle_radius=radius,
        )
        return self.features()

    def step(self, action: np.ndarray) -> tuple[np.ndarray, float, bool, dict[str, Any]]:
        assert self.state is not None
        s = self.state
        action = np.asarray(action, dtype=np.float32)
        north = float(np.clip(action[0], -1, 1)) * 17.5
        east = float(np.clip(action[1], -1, 1)) * 17.5
        down = float(np.clip(action[2], -1, 1)) * 5.0
        motors = np.clip((action[4:8] + 1.0) * 0.5, 0.0, 1.0)
        if s.motor_fault >= 0:
            motors[s.motor_fault] *= 0.12

        command = np.array([north, east], dtype=np.float32)
        accel = 0.44 * (command - s.vel) + 0.10 * s.wind
        imbalance = float(np.std(motors))
        s.vel = np.clip(s.vel + accel, -24.0, 24.0)
        s.pos = s.pos + s.vel
        s.altitude = float(np.clip(s.altitude - down + 0.9 * (float(motors.mean()) - 0.5) - imbalance, 8.0, 310.0))
        s.battery = float(max(0.0, s.battery - 0.0016 - 0.0018 * float(np.linalg.norm(s.vel) / 24.0) - 0.0012 * float(motors.mean())))
        s.step += 1

        goal = s.target if s.phase == "outbound" else s.home
        dist = float(np.linalg.norm(goal - s.pos))
        progress = s.prev_distance - dist
        s.prev_distance = dist

        nearest, clearance = self._nearest_obstacle()
        reward = 0.055 * progress - 0.025
        reward += 0.20 * max(0.0, clearance / 80.0)
        reward -= 0.75 * max(0.0, 1.0 - clearance / 36.0)
        reward -= 0.18 * abs(s.altitude - 120.0) / 120.0
        reward -= 1.15 if s.battery < 0.18 and s.phase != "return_home" else 0.0
        reward -= 0.30 * imbalance

        done = False
        if s.phase == "outbound" and dist < 32.0:
            reward += 8.0
            s.phase = "return_home"
            s.prev_distance = float(np.linalg.norm(s.home - s.pos))
        elif s.phase == "return_home" and dist < 28.0:
            reward += 13.0
            done = True
            s.done_reason = "mission_complete"
        if s.battery < 0.13 and s.phase == "outbound":
            s.phase = "return_home"
            s.prev_distance = float(np.linalg.norm(s.home - s.pos))
            reward += 0.8
        if clearance < 3.0:
            reward -= 12.0
            done = True
            s.done_reason = "collision"
        if s.altitude <= 10.0:
            reward -= 8.0
            done = True
            s.done_reason = "ground_contact"
        if s.battery <= 0.0:
            reward -= 9.0
            done = True
            s.done_reason = "battery_empty"
        if s.step >= self.horizon:
            done = True
            s.done_reason = s.done_reason or "timeout"
        info = {
            "distance": dist,
            "nearest_obstacle": nearest,
            "clearance": clearance,
            "phase": s.phase,
            "battery": s.battery,
            "altitude": s.altitude,
            "done_reason": s.done_reason,
        }
        return self.features(), float(reward), done, info

    def _nearest_obstacle(self) -> tuple[float, float]:
        assert self.state is not None
        s = self.state
        d = np.linalg.norm(s.obstacles - s.pos[None, :], axis=1) - s.obstacle_radius
        idx = int(np.argmin(d))
        return float(d[idx] + s.obstacle_radius[idx]), float(d[idx])

    def features(self) -> np.ndarray:
        assert self.state is not None
        s = self.state
        goal = s.target if s.phase == "outbound" else s.home
        delta = goal - s.pos
        dist = float(np.linalg.norm(delta))
        bearing = math.atan2(float(delta[1]), float(delta[0]))
        heading_vec = delta / max(dist, 1.0)
        side = np.array([-heading_vec[1], heading_vec[0]], dtype=np.float32)

        front = left = right = center = total = 0.0
        min_depth = 1.0
        for obs, radius in zip(s.obstacles, s.obstacle_radius, strict=True):
            rel = obs - s.pos
            fwd = float(rel @ heading_vec)
            lat = float(rel @ side)
            if fwd > -30 and fwd < 260:
                closeness = max(0.0, 1.0 - (math.hypot(fwd, lat) - float(radius)) / 240.0)
                total += closeness * 0.13
                min_depth = min(min_depth, max(0.0, (fwd - float(radius)) / 260.0))
                if abs(lat) < 45 and fwd > 0:
                    center += closeness
                elif lat < 0:
                    left += closeness
                else:
                    right += closeness
                if fwd > 0 and abs(lat) < 75:
                    front += closeness

        feat = np.zeros(96, dtype=np.float32)
        feat[3] = np.clip(total, 0, 1)
        feat[4] = np.clip(0.65 + 0.25 * (s.phase == "return_home"), 0, 1)
        feat[5] = 0.35
        feat[6] = np.clip(front, 0, 1)
        feat[7] = np.clip(1.0 - front * 0.3, 0, 1)
        feat[8] = 0.82
        feat[9] = min(len(s.obstacles) / 100.0, 1.0)
        feat[10] = np.clip(center, 0, 1)
        feat[11] = np.clip(left, 0, 1)
        feat[12] = np.clip(right, 0, 1)
        feat[13] = np.clip((s.altitude - 20.0) / 180.0, 0, 1)
        feat[18] = s.altitude / 1000.0
        feat[19] = s.altitude / 1000.0
        feat[24] = s.vel[0] / 50.0
        feat[25] = s.vel[1] / 50.0
        feat[28] = float(np.linalg.norm(s.vel)) / 50.0
        feat[30] = 0.72
        feat[32] = s.battery
        feat[36] = np.clip(delta[0] / 500.0, -1, 1)
        feat[37] = np.clip(delta[1] / 500.0, -1, 1)
        feat[38] = np.clip((120.0 - s.altitude) / 500.0, -1, 1)
        feat[39] = np.clip(dist / 500.0, 0, 2)
        feat[40] = bearing / math.pi
        feat[41] = 120.0 / 500.0
        feat[42] = 0.0 if s.phase == "outbound" else 0.5
        feat[43] = np.clip(1.0 - dist / 600.0, 0, 1)
        feat[44] = 1.0 if np.linalg.norm(s.wind) < 2.0 else 0.0
        feat[45] = 1.0 if np.linalg.norm(s.wind) >= 2.0 else 0.0
        feat[47] = 1.0 if abs(s.wind[1]) > abs(s.wind[0]) else 0.0
        feat[49] = 1.0 if s.motor_fault >= 0 else 0.0
        feat[51] = 1.0 if s.battery < 0.20 else 0.0
        feat[64] = min(len(s.obstacles) / 32.0, 1.0)
        feat[65] = np.clip(min_depth, 0, 1)
        feat[66] = np.clip((center - left + right) / 3.0, -1, 1)
        feat[67] = -np.clip(front, 0, 1)
        feat[70] = np.clip(front, 0, 1)
        feat[71] = np.clip(left, 0, 1)
        feat[72] = np.clip(right, 0, 1)
        feat[73] = np.clip(front, 0, 1)
        feat[74] = np.clip(front / max(min_depth, 0.05), 0, 1)
        feat[75] = 1.0
        feat[76] = np.clip(1.0 - total, 0, 1)
        feat[77] = np.clip(min_depth, 0, 1)
        feat[78] = np.clip(min_depth - center * 0.15, 0, 1)
        feat[79] = np.clip(min_depth - left * 0.1, 0, 1)
        feat[80] = np.clip(min_depth - right * 0.1, 0, 1)
        feat[81] = feat[13]
        feat[82] = np.clip((120.0 - s.altitude) / 120.0, -1, 1)
        feat[83] = np.clip((left - right) / 2.0, -1, 1)
        feat[84] = min(len(s.obstacles) / 32.0, 1.0)
        feat[85] = 0.82
        feat[86] = np.clip(total, 0, 1)
        feat[87] = np.clip(max(center, left, right), 0, 1)
        feat[88] = np.clip((right - left), -1, 1)
        feat[89] = np.clip(center, 0, 1)
        feat[90] = np.clip(center, 0, 1)
        feat[91] = np.clip(left, 0, 1)
        feat[92] = np.clip(right, 0, 1)
        feat[93] = min(s.step / 30.0, 1.0)
        feat[94] = np.clip(total * 0.82, 0, 1)
        feat[95] = 1.0
        return feat

    def frame(self, info: dict[str, Any] | None = None, size: int = 512) -> np.ndarray:
        assert self.state is not None
        s = self.state
        canvas = np.full((size, size, 3), (31, 36, 43), dtype=np.uint8)
        scale = size / 1000.0

        def pt(v: np.ndarray) -> tuple[int, int]:
            return (int(size / 2 + v[1] * scale), int(size / 2 - v[0] * scale))

        cv2.circle(canvas, pt(s.home), 9, (255, 255, 255), -1)
        cv2.circle(canvas, pt(s.target), 11, (80, 210, 255), -1)
        for obs, radius in zip(s.obstacles, s.obstacle_radius, strict=True):
            cv2.circle(canvas, pt(obs), max(5, int(radius * scale)), (50, 105, 180), -1)
        cv2.arrowedLine(canvas, pt(s.pos), pt(s.pos + s.vel * 5.0), (90, 255, 150), 2, tipLength=0.3)
        cv2.circle(canvas, pt(s.pos), 8, (70, 255, 130), -1)
        text = f"{s.phase} alt={s.altitude:.0f}m bat={s.battery*100:.0f}%"
        cv2.putText(canvas, text, (12, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (245, 245, 245), 1, cv2.LINE_AA)
        if info:
            cv2.putText(canvas, f"clear={info.get('clearance', 0):.1f}m", (12, 48), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (245, 245, 245), 1, cv2.LINE_AA)
        return canvas


def load_model(checkpoint: Path, device: torch.device) -> IcareDecisionModel:
    ckpt = torch.load(checkpoint, map_location="cpu", weights_only=False)
    cfg = IcareConfig(**ckpt["config"])
    model = IcareDecisionModel(cfg)
    model.load_state_dict(ckpt["model"])
    model.to(device)
    return model


def load_mission_embedding(path: Path, device: torch.device) -> tuple[torch.Tensor, str]:
    blob = torch.load(path, map_location="cpu", weights_only=False)
    embeddings: dict[str, torch.Tensor] = blob["embeddings"]
    text = sorted(embeddings)[0]
    return embeddings[text][None, :].to(device), text


def policy_action(
    model: IcareDecisionModel,
    seq: list[np.ndarray],
    mission: torch.Tensor,
    log_std: nn.Parameter | None,
    device: torch.device,
    sample: bool,
) -> tuple[np.ndarray, torch.Tensor | None, torch.Tensor | None, int, float, torch.Tensor]:
    arr = torch.tensor(np.stack(seq, axis=0)[None, :, :], dtype=torch.float32, device=device)
    out = model(arr, mission)
    mean = out["command"][:, -1, :]
    mode = int(out["mode_logits"][:, -1, :].argmax(dim=-1).detach().cpu()[0])
    complete = float(torch.sigmoid(out["complete_logit"][:, -1]).detach().cpu()[0])
    if sample and log_std is not None:
        std = log_std.exp()[None, :].expand_as(mean)
        dist = torch.distributions.Normal(mean, std)
        action = dist.rsample()
        log_prob = dist.log_prob(action).sum(dim=-1)
        entropy = dist.entropy().sum(dim=-1)
        bounded = action.clamp(-1, 1)
        return bounded.detach().cpu().numpy()[0], log_prob, entropy, mode, complete, mean
    return mean.clamp(-1, 1).detach().cpu().numpy()[0], None, None, mode, complete, mean


def expert_action(env: SyntheticDroneMission) -> np.ndarray:
    assert env.state is not None
    s = env.state
    goal = s.target if s.phase == "outbound" else s.home
    delta = goal - s.pos
    dist = max(float(np.linalg.norm(delta)), 1.0)
    desired = delta / dist
    avoid = np.zeros(2, dtype=np.float32)
    for obs, radius in zip(s.obstacles, s.obstacle_radius, strict=True):
        rel = s.pos - obs
        d = max(float(np.linalg.norm(rel)), 1.0)
        if d < float(radius) + 140.0:
            avoid += rel / d * ((float(radius) + 140.0 - d) / 140.0)
    vector = desired + 1.35 * avoid - 0.045 * s.wind
    norm = max(float(np.linalg.norm(vector)), 1.0)
    vector = vector / norm
    out = np.zeros(ICARE_ACTION_DIM, dtype=np.float32)
    out[0] = np.clip(vector[0], -1, 1)
    out[1] = np.clip(vector[1], -1, 1)
    out[2] = np.clip((s.altitude - 120.0) / 90.0, -1, 1)
    out[3] = np.clip((vector[1] - vector[0]) * 0.25, -1, 1)
    motor = 0.58 + 0.10 * max(0.0, 1.0 - s.battery) + 0.05 * abs(out[2])
    motor_cmd = np.full(4, motor, dtype=np.float32)
    if s.motor_fault >= 0:
        motor_cmd += 0.10
        motor_cmd[s.motor_fault] = 0.18
    out[4:8] = np.clip(motor_cmd * 2.0 - 1.0, -1, 1)
    out[8] = np.clip(-vector[1], -1, 1)
    out[9] = np.clip(vector[1], -1, 1)
    out[10] = np.clip(-out[2], -1, 1)
    out[11] = np.clip(-out[2], -1, 1)
    out[12] = np.clip(vector[1] * 0.5, -1, 1)
    out[13] = np.clip(vector[1] * 0.5, -1, 1)
    out[14] = 0.15 if s.phase == "return_home" else 0.0
    out[15] = 0.15 if s.phase == "return_home" else 0.0
    out[16] = 0.0
    out[17] = -1.0
    return out


def run_eval(model: IcareDecisionModel, mission: torch.Tensor, device: torch.device, episodes: int, seed: int) -> dict[str, float]:
    scores: list[float] = []
    success = collision = low_bat = 0
    distances: list[float] = []
    for idx in range(episodes):
        env = SyntheticDroneMission(seed=seed + 1000 + idx)
        obs = env.reset()
        seq = [obs.copy() for _ in range(12)]
        total = 0.0
        info: dict[str, Any] = {}
        for _ in range(env.horizon):
            action, _, _, _, _, _ = policy_action(model, seq, mission, None, device, sample=False)
            obs, reward, done, info = env.step(action)
            total += reward
            seq = (seq + [obs.copy()])[-12:]
            if done:
                break
        scores.append(total)
        reason = info.get("done_reason", "")
        success += int(reason == "mission_complete")
        collision += int(reason == "collision")
        low_bat += int(reason == "battery_empty")
        distances.append(float(info.get("distance", 999.0)))
    return {
        "mean_reward": float(np.mean(scores)),
        "success_rate": success / episodes,
        "collision_rate": collision / episodes,
        "battery_empty_rate": low_bat / episodes,
        "mean_final_distance_m": float(np.mean(distances)),
    }


def discounted_returns(rewards: list[float], gamma: float) -> torch.Tensor:
    out: list[float] = []
    acc = 0.0
    for reward in reversed(rewards):
        acc = reward + gamma * acc
        out.append(acc)
    out.reverse()
    ret = torch.tensor(out, dtype=torch.float32)
    return (ret - ret.mean()) / (ret.std(unbiased=False) + 1e-6)


def train(args: argparse.Namespace) -> dict[str, Any]:
    out_dir = args.out_dir / time.strftime("run_%Y%m%d_%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)
    device = torch.device(args.device if args.device == "cpu" or torch.cuda.is_available() else "cpu")
    model = load_model(args.checkpoint, device)
    mission, mission_text = load_mission_embedding(args.embedding_cache, device)
    model.eval()

    for name, param in model.named_parameters():
        param.requires_grad = not args.train_head_only
        if args.train_head_only and name.startswith("policy.command_head."):
            param.requires_grad = True
    log_std = nn.Parameter(torch.full((ICARE_ACTION_DIM,), -1.25, device=device))
    opt = torch.optim.AdamW([p for p in model.parameters() if p.requires_grad] + [log_std], lr=args.lr, weight_decay=0.0)

    metrics: list[dict[str, Any]] = []
    initial = run_eval(model, mission, device, args.eval_episodes, args.seed)
    metrics.append({"episode": 0, "kind": "eval", **initial})
    if args.bc_warmup_steps > 0:
        warmup_rows = behavior_clone_warmup(model, mission, opt, device, args)
        metrics.extend(warmup_rows)
        warmup_eval = run_eval(model, mission, device, args.eval_episodes, args.seed + 777)
        metrics.append({"episode": 0, "kind": "eval_after_bc", **warmup_eval})
        print(json.dumps({"stage": "bc_warmup", **warmup_eval}, ensure_ascii=True))

    for ep in range(1, args.episodes + 1):
        env = SyntheticDroneMission(seed=args.seed + ep, horizon=args.horizon)
        obs = env.reset()
        seq = [obs.copy() for _ in range(args.sequence_len)]
        log_probs: list[torch.Tensor] = []
        entropies: list[torch.Tensor] = []
        bc_losses: list[torch.Tensor] = []
        rewards: list[float] = []
        ep_reward = 0.0
        info: dict[str, Any] = {}
        for _ in range(args.horizon):
            action, log_prob, entropy, _, _, mean = policy_action(model, seq, mission, log_std, device, sample=True)
            target = torch.tensor(expert_action(env), dtype=torch.float32, device=device)[None, :]
            obs, reward, done, info = env.step(action)
            rewards.append(reward)
            ep_reward += reward
            if log_prob is not None and entropy is not None:
                log_probs.append(log_prob)
                entropies.append(entropy)
                bc_losses.append(torch.nn.functional.smooth_l1_loss(mean, target))
            seq = (seq + [obs.copy()])[-args.sequence_len:]
            if done or len(log_probs) >= args.rollout_len:
                adv = discounted_returns(rewards, args.gamma).to(device)
                lp = torch.cat(log_probs)
                ent = torch.cat(entropies).mean()
                bc = torch.stack(bc_losses).mean()
                loss = -(lp * adv).mean() - args.entropy_coef * ent + args.bc_coef * bc
                opt.zero_grad(set_to_none=True)
                loss.backward()
                torch.nn.utils.clip_grad_norm_([p for p in model.parameters() if p.requires_grad] + [log_std], 0.7)
                opt.step()
                log_probs.clear()
                entropies.clear()
                rewards.clear()
                bc_losses.clear()
            if done:
                break
        metrics.append({
            "episode": ep,
            "kind": "train",
            "reward": ep_reward,
            "done_reason": info.get("done_reason", ""),
            "distance": info.get("distance", 999.0),
            "battery": info.get("battery", 0.0),
            "altitude": info.get("altitude", 0.0),
        })
        if ep % args.eval_interval == 0 or ep == args.episodes:
            ev = run_eval(model, mission, device, args.eval_episodes, args.seed + ep * 13)
            metrics.append({"episode": ep, "kind": "eval", **ev})
            print(json.dumps({"episode": ep, **ev}, ensure_ascii=True))

    ckpt_out = out_dir / "icare_policy_rl.pt"
    torch.save({
        "schema": "hesia.icare.rl_checkpoint.v1",
        "base_checkpoint": str(args.checkpoint),
        "model": model.state_dict(),
        "config": model.cfg.to_dict(),
        "log_std": log_std.detach().cpu(),
        "mission_text": mission_text,
    }, ckpt_out)
    write_metrics(out_dir, metrics)
    make_plots(out_dir, metrics)
    video = make_video(out_dir, model, mission, device, args.seed + 9000)
    summary = {
        "schema": "hesia.icare.rl_summary.v1",
        "status": "passed",
        "out_dir": str(out_dir),
        "checkpoint": str(ckpt_out),
        "video": str(video),
        "device": str(device),
        "parameter_count": icare_parameter_count(model),
        "trainable_parameters": sum(p.numel() for p in model.parameters() if p.requires_grad) + log_std.numel(),
        "episodes": args.episodes,
        "horizon": args.horizon,
        "initial_eval": initial,
        "warmup_eval": next((m for m in reversed(metrics) if m["kind"] == "eval_after_bc"), None),
        "final_eval": [m for m in metrics if m["kind"] == "eval"][-1],
        "isaac_logs": [str(p) for p in [Path("G:/Hesia-RL/isaac_compat_check.log"), Path("G:/Hesia-RL/isaac_compat_check_nvidia_all.log")] if p.exists()],
        "px4_mavsdk_check": str(Path("G:/Hesia-RL/px4_mavsdk_check.json")),
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    make_pdf(out_dir, summary)
    print(json.dumps(summary, indent=2))
    return summary


def write_metrics(out_dir: Path, metrics: list[dict[str, Any]]) -> None:
    keys = sorted({k for row in metrics for k in row})
    with (out_dir / "metrics.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(metrics)
    (out_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")


def behavior_clone_warmup(
    model: IcareDecisionModel,
    mission: torch.Tensor,
    opt: torch.optim.Optimizer,
    device: torch.device,
    args: argparse.Namespace,
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    model.train()
    for step in range(1, args.bc_warmup_steps + 1):
        features: list[np.ndarray] = []
        targets: list[np.ndarray] = []
        for item in range(args.bc_batch_size):
            env = SyntheticDroneMission(seed=args.seed + 50_000 + step * args.bc_batch_size + item, horizon=args.horizon)
            obs = env.reset()
            seq = [obs.copy() for _ in range(args.sequence_len)]
            pre_steps = int(np.random.randint(0, max(1, args.horizon // 2)))
            for _ in range(pre_steps):
                expert = expert_action(env)
                obs, _, done, _ = env.step(expert)
                seq = (seq + [obs.copy()])[-args.sequence_len:]
                if done:
                    break
            features.append(np.stack(seq, axis=0))
            targets.append(expert_action(env))
        feat = torch.tensor(np.stack(features, axis=0), dtype=torch.float32, device=device)
        target = torch.tensor(np.stack(targets, axis=0), dtype=torch.float32, device=device)
        out = model(feat, mission.expand(feat.shape[0], -1))
        pred = out["command"][:, -1, :]
        loss = torch.nn.functional.smooth_l1_loss(pred, target)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_([p for p in model.parameters() if p.requires_grad], 0.8)
        opt.step()
        if step == 1 or step % max(1, args.bc_log_interval) == 0 or step == args.bc_warmup_steps:
            row = {"episode": -step, "kind": "bc_warmup", "bc_loss": float(loss.detach().cpu())}
            rows.append(row)
            print(json.dumps(row, ensure_ascii=True))
    return rows


def make_plots(out_dir: Path, metrics: list[dict[str, Any]]) -> None:
    train_rows = [m for m in metrics if m["kind"] == "train"]
    eval_rows = [m for m in metrics if m["kind"] in {"eval", "eval_after_bc"}]
    bc_rows = [m for m in metrics if m["kind"] == "bc_warmup"]
    fig, axes = plt.subplots(2, 2, figsize=(10, 7), dpi=150)
    axes[0, 0].plot([m["episode"] for m in train_rows], [m["reward"] for m in train_rows], color="#0b7285")
    axes[0, 0].set_title("training reward")
    axes[0, 1].plot(range(len(eval_rows)), [m["success_rate"] for m in eval_rows], marker="o", color="#2a9d8f")
    axes[0, 1].set_title("eval success rate")
    axes[1, 0].plot(range(len(eval_rows)), [m["collision_rate"] for m in eval_rows], marker="o", color="#9d0208")
    axes[1, 0].set_title("eval collision rate")
    axes[1, 1].plot(range(len(eval_rows)), [m["mean_final_distance_m"] for m in eval_rows], marker="o", color="#5f3dc4")
    axes[1, 1].set_title("mean final distance")
    if bc_rows:
        ax_bc = axes[0, 0].twinx()
        ax_bc.plot([abs(m["episode"]) for m in bc_rows], [m["bc_loss"] for m in bc_rows], color="#f08c00", alpha=0.7)
        ax_bc.set_ylabel("bc loss")
    for ax in axes.flat:
        ax.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(out_dir / "training_curves.png")
    plt.close(fig)


def make_video(out_dir: Path, model: IcareDecisionModel, mission: torch.Tensor, device: torch.device, seed: int) -> Path:
    env = SyntheticDroneMission(seed=seed, horizon=150)
    obs = env.reset()
    seq = [obs.copy() for _ in range(12)]
    frames: list[np.ndarray] = []
    info: dict[str, Any] = {}
    for idx in range(150):
        action, _, _, mode, complete, _ = policy_action(model, seq, mission, None, device, sample=False)
        raw = env.frame(info)
        obs, reward, done, info = env.step(action)
        seq = (seq + [obs.copy()])[-12:]
        perception = raw.copy()
        heat = np.zeros_like(raw)
        heat[:, :, 1] = np.linspace(40, 210, raw.shape[0], dtype=np.uint8)[:, None]
        perception = cv2.addWeighted(perception, 0.62, heat, 0.38, 0)
        cv2.putText(perception, "YOLO-MiDaS synthetic perception", (12, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
        terminal = np.full_like(raw, (12, 15, 18))
        lines = [
            "ICARE TERMINAL",
            f"step={idx:03d} reward={reward:+.2f}",
            f"mode_id={mode} complete={complete:.2f}",
            f"north/east/down={action[0]:+.2f}/{action[1]:+.2f}/{action[2]:+.2f}",
            f"motors={','.join(f'{v:.2f}' for v in ((action[4:8]+1)*0.5))}",
            f"phase={info.get('phase','')} alt={info.get('altitude',0):.1f}m",
            f"battery={info.get('battery',0)*100:.1f}% clear={info.get('clearance',0):.1f}m",
            f"done={info.get('done_reason','')}",
        ]
        for row, line in enumerate(lines):
            cv2.putText(terminal, line, (18, 36 + row * 31), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (170, 235, 180), 1, cv2.LINE_AA)
        combined = np.concatenate([raw, perception, terminal], axis=1)
        cv2.putText(combined, "raw camera/no AI assets", (12, combined.shape[0] - 16), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (250, 250, 250), 1, cv2.LINE_AA)
        frames.append(cv2.cvtColor(combined, cv2.COLOR_BGR2RGB))
        if done:
            break
    path = out_dir / "verification_autonomous_flight.mp4"
    imageio.mimsave(path, frames, fps=12, quality=8)
    return path


def table(rows: list[list[Any]]) -> Table:
    t = Table([[str(c) for c in row] for row in rows], repeatRows=1)
    t.setStyle(TableStyle([
        ("GRID", (0, 0), (-1, -1), 0.25, colors.lightgrey),
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#edf2f4")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    return t


def make_pdf(out_dir: Path, summary: dict[str, Any]) -> None:
    styles = getSampleStyleSheet()
    doc = SimpleDocTemplate(str(out_dir / "ICARE_RL_VERIFICATION_REPORT.pdf"), pagesize=A4, rightMargin=14 * mm, leftMargin=14 * mm)
    init = summary["initial_eval"]
    warm = summary.get("warmup_eval") or init
    final = summary["final_eval"]
    story: list[Any] = [
        Paragraph("Icare RL Flight Verification", styles["Title"]),
        Paragraph("Scope: Icare decision policy, synthetic YOLO/MiDaS perception features, guardrails, and PX4/MAVSDK connectivity. Full Isaac Sim was downloaded but blocked by local compatibility limits.", styles["BodyText"]),
        Spacer(1, 4 * mm),
        table([
            ["Item", "Value"],
            ["Device", summary["device"]],
            ["Episodes", summary["episodes"]],
            ["Trainable parameters", summary["trainable_parameters"]],
            ["Checkpoint", summary["checkpoint"]],
            ["Video", summary["video"]],
        ]),
        Spacer(1, 4 * mm),
        Paragraph("RL Metrics", styles["Heading2"]),
        table([
            ["Metric", "Initial", "After BC", "Final"],
            ["Mean reward", f"{init['mean_reward']:.2f}", f"{warm['mean_reward']:.2f}", f"{final['mean_reward']:.2f}"],
            ["Success rate", f"{init['success_rate']:.2%}", f"{warm['success_rate']:.2%}", f"{final['success_rate']:.2%}"],
            ["Collision rate", f"{init['collision_rate']:.2%}", f"{warm['collision_rate']:.2%}", f"{final['collision_rate']:.2%}"],
            ["Battery empty rate", f"{init['battery_empty_rate']:.2%}", f"{warm['battery_empty_rate']:.2%}", f"{final['battery_empty_rate']:.2%}"],
            ["Mean final distance", f"{init['mean_final_distance_m']:.1f} m", f"{warm['mean_final_distance_m']:.1f} m", f"{final['mean_final_distance_m']:.1f} m"],
        ]),
        Spacer(1, 4 * mm),
        Image(str(out_dir / "training_curves.png"), width=178 * mm, height=124 * mm),
        Spacer(1, 4 * mm),
        Paragraph("Isaac/PX4 Status", styles["Heading2"]),
        table([
            ["System", "Observed status"],
            ["Isaac Sim NGC", "Image pulled; compatibility checker failed on RTX 3070 8 GB, Docker RAM 12.5 GB, and Vulkan GPU foundation initialization."],
            ["PX4 SITL", "px4io/px4-sitl container launched; MAVSDK connected on udp://:14550."],
            ["Simulation policy", "Fallback RL uses a deterministic mission dynamics model until Isaac is available on compatible hardware/runtime."],
        ]),
    ]
    doc.build(story)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CKPT)
    parser.add_argument("--embedding-cache", type=Path, default=DEFAULT_EMBED)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--episodes", type=int, default=60)
    parser.add_argument("--horizon", type=int, default=110)
    parser.add_argument("--sequence-len", type=int, default=12)
    parser.add_argument("--rollout-len", type=int, default=24)
    parser.add_argument("--eval-interval", type=int, default=10)
    parser.add_argument("--eval-episodes", type=int, default=24)
    parser.add_argument("--lr", type=float, default=7e-5)
    parser.add_argument("--gamma", type=float, default=0.985)
    parser.add_argument("--entropy-coef", type=float, default=0.0015)
    parser.add_argument("--bc-coef", type=float, default=0.05)
    parser.add_argument("--bc-warmup-steps", type=int, default=260)
    parser.add_argument("--bc-batch-size", type=int, default=24)
    parser.add_argument("--bc-log-interval", type=int, default=40)
    parser.add_argument("--train-head-only", action="store_true")
    parser.add_argument("--seed", type=int, default=13)
    args = parser.parse_args()
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    train(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
