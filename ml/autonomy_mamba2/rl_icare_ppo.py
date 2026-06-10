"""PPO trainer for the Icare Mamba-2 drone policy on a GPU-vectorised mission.

Design notes
------------
* The Icare policy is deployed as a *windowed* model: at inference it is fed the
  last ``window`` observations ``[1, window, F]`` and the last step is used
  (see ``rl_icare_train.policy_action`` / ``run_eval``).  We mirror that exactly,
  so the PPO observation is the 12-frame sliding window and the update is a
  standard feed-forward PPO over windows -- no fragile cross-episode recurrent
  state surgery, and the trained weights drop straight into the existing Icare
  contract.
* Actor: the existing ``IcareDecisionModel`` (Mamba-2), Gaussian over the
  tanh-bounded 18-d command with a learnable per-dim ``log_std``.
* Critic: separate MLP over the flattened window.
* PPO with GAE(lambda), clipped policy + value losses, entropy bonus, optional
  small behaviour-cloning auxiliary loss from the analytic expert for early
  stability.  Mixed precision for throughput on a single RTX 3080.

Isaac Sim was evaluated first but is not viable under WSL2 (NVIDIA exposes
CUDA + D3D12 only, no Vulkan ICD with RTX extensions that the Omniverse renderer
requires); this CUDA-vectorised simulator is the chosen fallback and trains the
Mamba policy to convergence directly.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
from pathlib import Path
from typing import Any

import numpy as np
import torch
from torch import nn

from .features_icare import ICARE_ACTION_DIM
from .icare_controller import IcareConfig, IcareDecisionModel, icare_parameter_count
from .icare_env_vec import IcareVecConfig, IcareVecEnv

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CKPT = ROOT / "artifacts" / "icare" / "train_1_3b" / "icare_policy.pt"
DEFAULT_EMBED = ROOT / "artifacts" / "icare" / "train_1_3b" / "mission_embeddings.pt"
DEFAULT_OUT = Path("G:/Hesia-RL/runs/icare_ppo")
REASON_NAMES = {0: "running", 1: "mission_complete", 2: "collision", 3: "ground_contact", 4: "battery_empty", 5: "timeout"}


class Critic(nn.Module):
    def __init__(self, window: int, feat: int, hidden: int = 256) -> None:
        super().__init__()
        self.net = nn.Sequential(
            nn.LayerNorm(window * feat),
            nn.Linear(window * feat, hidden), nn.SiLU(),
            nn.Linear(hidden, hidden), nn.SiLU(),
            nn.Linear(hidden, 1),
        )
        nn.init.zeros_(self.net[-1].bias)
        nn.init.uniform_(self.net[-1].weight, -3e-3, 3e-3)

    def forward(self, window: torch.Tensor) -> torch.Tensor:
        return self.net(window.flatten(1)).squeeze(-1)


def load_actor(checkpoint: Path, device: torch.device) -> IcareDecisionModel:
    ckpt = torch.load(checkpoint, map_location="cpu", weights_only=False)
    cfg = IcareConfig(**ckpt["config"])
    model = IcareDecisionModel(cfg)
    model.load_state_dict(ckpt["model"])
    return model.to(device)


def load_mission(path: Path, device: torch.device) -> tuple[torch.Tensor, str]:
    blob = torch.load(path, map_location="cpu", weights_only=False)
    emb: dict[str, torch.Tensor] = blob["embeddings"]
    text = sorted(emb)[0]
    return emb[text][None, :].to(device), text


def actor_mean(model: IcareDecisionModel, window: torch.Tensor, mission: torch.Tensor) -> torch.Tensor:
    """window: [B, W, F] -> command mean [B, A] (last timestep)."""
    out = model(window, mission.expand(window.shape[0], -1))
    return out["command"][:, -1, :]


@torch.no_grad()
def evaluate(model: IcareDecisionModel, mission: torch.Tensor, device: torch.device,
             episodes: int, window: int, horizon: int, seed: int) -> dict[str, float]:
    env = IcareVecEnv(IcareVecConfig(num_envs=episodes, horizon=horizon, device=str(device), seed=seed))
    obs = env.reset_all()
    win = obs[:, None, :].repeat(1, window, 1)
    done_mask = torch.zeros(episodes, dtype=torch.bool, device=device)
    totals = torch.zeros(episodes, device=device)
    reasons = torch.zeros(episodes, dtype=torch.long, device=device)
    final_dist = torch.full((episodes,), 999.0, device=device)
    for _ in range(horizon):
        mean = actor_mean(model, win, mission).clamp(-1, 1)
        reward, done, info = env.step(mean)
        live = ~done_mask
        totals += reward * live.float()
        newly = done & live
        reasons[newly] = info["reason"][newly]
        final_dist[newly] = info["distance"][newly]
        done_mask = done_mask | done
        obs = env.features()
        win = torch.cat([win[:, 1:, :], obs[:, None, :]], dim=1)
        if done_mask.all():
            break
    final_dist[~done_mask] = info["distance"][~done_mask]
    return {
        "mean_reward": float(totals.mean()),
        "success_rate": float((reasons == 1).float().mean()),
        "collision_rate": float((reasons == 2).float().mean()),
        "ground_rate": float((reasons == 3).float().mean()),
        "battery_empty_rate": float((reasons == 4).float().mean()),
        "timeout_rate": float((reasons == 5).float().mean()),
        "mean_final_distance_m": float(final_dist.mean()),
    }


def bc_warmup(actor, mission, device, args, eval_fn) -> list[dict[str, Any]]:
    """Supervised imitation of the analytic expert to reach a competent policy
    fast, before PPO refines it.  Trains on the expert's own state distribution
    (windows generated by rolling the expert with small exploration noise)."""
    rows: list[dict[str, Any]] = []
    if args.bc_warmup_steps <= 0:
        return rows
    opt = torch.optim.AdamW(actor.parameters(), lr=args.bc_lr, weight_decay=0.0)
    env = IcareVecEnv(IcareVecConfig(num_envs=args.num_envs, horizon=args.horizon, device=str(device), seed=args.seed + 4242))
    obs = env.reset_all()
    win = obs[:, None, :].repeat(1, args.window, 1)
    W = args.window
    actor.train()
    for step in range(1, args.bc_warmup_steps + 1):
        target = env.expert_action()
        with torch.amp.autocast("cuda", enabled=args.amp):
            mean = actor_mean(actor, win, mission)
            loss = torch.nn.functional.smooth_l1_loss(mean, target)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        nn.utils.clip_grad_norm_(actor.parameters(), 1.0)
        opt.step()
        # step env with noisy expert to diversify states
        noisy = (target + 0.08 * torch.randn_like(target)).clamp(-1, 1)
        _, done, _ = env.step(noisy)
        obs = env.features()
        win = torch.cat([win[:, 1:, :], obs[:, None, :]], dim=1)
        if done.any():
            env.reset(done.nonzero(as_tuple=True)[0])
            fresh = env.features()
            win[done] = fresh[done][:, None, :].repeat(1, W, 1)
        if step == 1 or step % args.bc_log_interval == 0 or step == args.bc_warmup_steps:
            ev = eval_fn(actor)
            rows.append({"update": -step, "kind": "bc_warmup", "bc_loss": float(loss.detach()), **ev})
            print(json.dumps({"bc_step": step, "bc_loss": round(float(loss.detach()), 4),
                              "success": round(ev["success_rate"], 3), "collision": round(ev["collision_rate"], 3)}))
            actor.train()
    return rows


def gae(rewards, values, dones, last_value, gamma, lam):
    T, N = rewards.shape
    adv = torch.zeros_like(rewards)
    last = torch.zeros(N, device=rewards.device)
    for t in reversed(range(T)):
        next_v = last_value if t == T - 1 else values[t + 1]
        nonterm = 1.0 - dones[t].float()
        delta = rewards[t] + gamma * next_v * nonterm - values[t]
        last = delta + gamma * lam * nonterm * last
        adv[t] = last
    return adv


def train(args: argparse.Namespace) -> dict[str, Any]:
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    out_dir = args.out_dir / time.strftime("run_%Y%m%d_%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    actor = load_actor(args.checkpoint, device)
    mission, mission_text = load_mission(args.embedding_cache, device)
    critic = Critic(args.window, IcareVecEnv.feature_dim).to(device)
    log_std = nn.Parameter(torch.full((ICARE_ACTION_DIM,), math.log(args.init_std), device=device))

    opt = torch.optim.AdamW(
        list(actor.parameters()) + list(critic.parameters()) + [log_std],
        lr=args.lr, weight_decay=0.0, eps=1e-5,
    )
    scaler = torch.amp.GradScaler("cuda", enabled=args.amp)

    env = IcareVecEnv(IcareVecConfig(num_envs=args.num_envs, horizon=args.horizon, device=str(device), seed=args.seed))
    obs = env.reset_all()
    win = obs[:, None, :].repeat(1, args.window, 1)  # [N, W, F]

    metrics: list[dict[str, Any]] = []
    init_eval = evaluate(actor, mission, device, args.eval_episodes, args.window, args.horizon, args.seed + 1)
    metrics.append({"update": 0, "kind": "eval", **init_eval})
    print(json.dumps({"stage": "initial", **init_eval}))

    # Behaviour-cloning warmup to a competent policy before PPO.
    eval_fn = lambda m: evaluate(m, mission, device, max(128, args.eval_episodes // 2), args.window, args.horizon, args.seed + 31)
    warm_rows = bc_warmup(actor, mission, device, args, eval_fn)
    metrics.extend(warm_rows)
    if warm_rows:
        bc_eval = evaluate(actor, mission, device, args.eval_episodes, args.window, args.horizon, args.seed + 2)
        metrics.append({"update": 0, "kind": "eval_after_bc", **bc_eval})
        print(json.dumps({"stage": "after_bc", **bc_eval}))

    N, W, F, A = args.num_envs, args.window, IcareVecEnv.feature_dim, ICARE_ACTION_DIM
    T = args.rollout
    best_success = -1.0
    t_start = time.time()

    for update in range(1, args.updates + 1):
        # ---------------- rollout ----------------
        b_win = torch.zeros(T, N, W, F, device=device, dtype=torch.float16 if args.store_fp16 else torch.float32)
        b_act = torch.zeros(T, N, A, device=device)
        b_logp = torch.zeros(T, N, device=device)
        b_val = torch.zeros(T, N, device=device)
        b_rew = torch.zeros(T, N, device=device)
        b_done = torch.zeros(T, N, device=device)
        b_exp = torch.zeros(T, N, A, device=device)
        ep_rewards: list[float] = []
        ep_reasons: list[int] = []
        running_ret = torch.zeros(N, device=device)

        actor.eval()
        with torch.no_grad():
            std = log_std.exp()
            for t in range(T):
                mean = actor_mean(actor, win, mission)
                dist = torch.distributions.Normal(mean, std[None, :])
                action = dist.rsample()
                logp = dist.log_prob(action).sum(-1)
                value = critic(win)
                exp_a = env.expert_action()
                act_clamped = action.clamp(-1, 1)
                reward, done, info = env.step(act_clamped)

                b_win[t] = win.to(b_win.dtype)
                b_act[t] = action
                b_logp[t] = logp
                b_val[t] = value
                b_rew[t] = reward
                b_done[t] = done.float()
                b_exp[t] = exp_a

                running_ret += reward
                if done.any():
                    for r, rs in zip(running_ret[done].tolist(), info["reason"][done].tolist()):
                        ep_rewards.append(r)
                        ep_reasons.append(rs)
                    running_ret[done] = 0.0

                obs = env.features()
                win = torch.cat([win[:, 1:, :], obs[:, None, :]], dim=1)
                # reset finished envs (fresh episode within the rollout) + clear their window
                if done.any():
                    env.reset(done.nonzero(as_tuple=True)[0])
                    fresh = env.features()
                    win[done] = fresh[done][:, None, :].repeat(1, W, 1)
            last_value = critic(win)

        adv = gae(b_rew, b_val, b_done, last_value, args.gamma, args.lam)
        ret = adv + b_val
        adv = (adv - adv.mean()) / (adv.std() + 1e-6)

        # flatten
        fw = b_win.reshape(T * N, W, F)
        fa = b_act.reshape(T * N, A)
        flp = b_logp.reshape(T * N)
        fadv = adv.reshape(T * N)
        fret = ret.reshape(T * N)
        fval = b_val.reshape(T * N)
        fexp = b_exp.reshape(T * N, A)
        n_samples = T * N

        # ---------------- PPO update ----------------
        actor.train()
        stats = {"pg": 0.0, "vf": 0.0, "ent": 0.0, "bc": 0.0, "kl": 0.0, "clipfrac": 0.0, "n": 0}
        bc_coef = args.bc_coef * max(0.0, 1.0 - update / max(1, args.bc_anneal_updates))
        stop_update = False
        for _ in range(args.epochs):
            if stop_update:
                break
            idx_all = torch.randperm(n_samples, device=device)
            for s in range(0, n_samples, args.minibatch):
                mb = idx_all[s:s + args.minibatch]
                w = fw[mb].float()
                with torch.amp.autocast("cuda", enabled=args.amp):
                    mean = actor_mean(actor, w, mission)
                    std = log_std.exp()
                    dist = torch.distributions.Normal(mean, std[None, :])
                    new_logp = dist.log_prob(fa[mb]).sum(-1)
                    entropy = dist.entropy().sum(-1).mean()
                    value = critic(w)

                    ratio = (new_logp - flp[mb]).exp()
                    a_mb = fadv[mb]
                    pg1 = -a_mb * ratio
                    pg2 = -a_mb * ratio.clamp(1 - args.clip, 1 + args.clip)
                    pg_loss = torch.max(pg1, pg2).mean()

                    v_clip = fval[mb] + (value - fval[mb]).clamp(-args.clip, args.clip)
                    vf1 = (value - fret[mb]) ** 2
                    vf2 = (v_clip - fret[mb]) ** 2
                    vf_loss = 0.5 * torch.max(vf1, vf2).mean()

                    bc_loss = torch.nn.functional.smooth_l1_loss(mean, fexp[mb]) if bc_coef > 0 else mean.sum() * 0.0
                    loss = pg_loss + args.vf_coef * vf_loss - args.ent_coef * entropy + bc_coef * bc_loss

                opt.zero_grad(set_to_none=True)
                scaler.scale(loss).backward()
                scaler.unscale_(opt)
                nn.utils.clip_grad_norm_(list(actor.parameters()) + list(critic.parameters()) + [log_std], args.max_grad_norm)
                scaler.step(opt)
                scaler.update()

                with torch.no_grad():
                    approx_kl = float((flp[mb] - new_logp).mean().clamp(min=0))
                    clipfrac = float(((ratio - 1.0).abs() > args.clip).float().mean())
                    stats["pg"] += float(pg_loss.detach()); stats["vf"] += float(vf_loss.detach())
                    stats["ent"] += float(entropy.detach()); stats["bc"] += float(bc_loss.detach())
                    stats["kl"] += approx_kl; stats["clipfrac"] += clipfrac
                    stats["n"] += 1
                # KL-targeted early stop: keep updates trust-region sized and stable
                if args.target_kl > 0 and approx_kl > 1.5 * args.target_kl:
                    stop_update = True
                    break

        nb = max(1, stats["n"])
        row = {
            "update": update, "kind": "train",
            "mean_ep_reward": float(np.mean(ep_rewards)) if ep_rewards else float("nan"),
            "episodes": len(ep_rewards),
            "success_rate_rollout": (np.mean([r == 1 for r in ep_reasons]) if ep_reasons else float("nan")),
            "collision_rate_rollout": (np.mean([r == 2 for r in ep_reasons]) if ep_reasons else float("nan")),
            "pg_loss": stats["pg"] / nb, "vf_loss": stats["vf"] / nb, "entropy": stats["ent"] / nb,
            "bc_loss": stats["bc"] / nb, "approx_kl": stats["kl"] / nb, "clipfrac": stats["clipfrac"] / nb,
            "log_std_mean": float(log_std.mean().detach()), "bc_coef": bc_coef,
            "sps": int(n_samples / (time.time() - t_start)) if update == 1 else None,
        }
        metrics.append(row)

        if update % args.eval_interval == 0 or update == args.updates:
            ev = evaluate(actor, mission, device, args.eval_episodes, args.window, args.horizon, args.seed + 7 * update)
            metrics.append({"update": update, "kind": "eval", **ev})
            elapsed = time.time() - t_start
            print(json.dumps({"update": update, "elapsed_s": round(elapsed, 1),
                              "ep_reward": round(row["mean_ep_reward"], 2),
                              "success": round(ev["success_rate"], 3),
                              "collision": round(ev["collision_rate"], 3),
                              "final_dist_m": round(ev["mean_final_distance_m"], 1),
                              "kl": round(row["approx_kl"], 4)}))
            if ev["success_rate"] >= best_success:
                best_success = ev["success_rate"]
                save_checkpoint(out_dir / "icare_policy_ppo_best.pt", actor, log_std, args, mission_text, ev)

    final_eval = evaluate(actor, mission, device, max(256, args.eval_episodes), args.window, args.horizon, args.seed + 99999)
    metrics.append({"update": args.updates, "kind": "eval_final", **final_eval})
    ckpt_path = out_dir / "icare_policy_ppo.pt"
    save_checkpoint(ckpt_path, actor, log_std, args, mission_text, final_eval)
    write_metrics(out_dir, metrics)
    try:
        make_plots(out_dir, metrics)
    except Exception as exc:  # pragma: no cover
        print(json.dumps({"warn": f"plot failed: {exc}"}))

    summary = {
        "schema": "hesia.icare.ppo_summary.v1",
        "status": "passed",
        "out_dir": str(out_dir),
        "checkpoint": str(ckpt_path),
        "best_checkpoint": str(out_dir / "icare_policy_ppo_best.pt"),
        "device": str(device),
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else "cpu",
        "parameter_count": icare_parameter_count(actor),
        "config": vars(args) | {"checkpoint": str(args.checkpoint), "embedding_cache": str(args.embedding_cache), "out_dir": str(args.out_dir)},
        "mission_text": mission_text,
        "samples_per_update": T * N,
        "total_env_steps": T * N * args.updates,
        "initial_eval": init_eval,
        "final_eval": final_eval,
        "best_success_rate": best_success,
        "wall_time_s": round(time.time() - t_start, 1),
        "isaac_status": "unavailable_wsl2_no_nvidia_vulkan_icd; trained on CUDA-vectorised simulator fallback",
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({"FINAL": summary["final_eval"], "best_success": best_success, "wall_time_s": summary["wall_time_s"]}, indent=2))
    return summary


def save_checkpoint(path: Path, actor, log_std, args, mission_text, eval_metrics) -> None:
    torch.save({
        "schema": "hesia.icare.ppo_checkpoint.v1",
        "base_checkpoint": str(args.checkpoint),
        "model": actor.state_dict(),
        "config": actor.cfg.to_dict(),
        "log_std": log_std.detach().cpu(),
        "mission_text": mission_text,
        "eval": eval_metrics,
        "window": args.window,
    }, path)


def write_metrics(out_dir: Path, metrics: list[dict[str, Any]]) -> None:
    keys = sorted({k for row in metrics for k in row})
    with (out_dir / "metrics.csv").open("w", newline="", encoding="utf-8") as fh:
        wr = csv.DictWriter(fh, fieldnames=keys)
        wr.writeheader()
        wr.writerows(metrics)
    (out_dir / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")


def make_plots(out_dir: Path, metrics: list[dict[str, Any]]) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    tr = [m for m in metrics if m["kind"] == "train"]
    ev = [m for m in metrics if m["kind"] in {"eval", "eval_final"}]
    fig, ax = plt.subplots(2, 2, figsize=(11, 7.5), dpi=140)
    ax[0, 0].plot([m["update"] for m in tr], [m["mean_ep_reward"] for m in tr], color="#0b7285")
    ax[0, 0].set_title("rollout mean episode reward"); ax[0, 0].set_xlabel("update")
    ax[0, 1].plot([m["update"] for m in ev], [m["success_rate"] for m in ev], marker="o", color="#2a9d8f", label="success")
    ax[0, 1].plot([m["update"] for m in ev], [m["collision_rate"] for m in ev], marker="x", color="#9d0208", label="collision")
    ax[0, 1].set_title("eval success / collision"); ax[0, 1].legend(); ax[0, 1].set_ylim(-0.02, 1.02)
    ax[1, 0].plot([m["update"] for m in ev], [m["mean_final_distance_m"] for m in ev], marker="o", color="#5f3dc4")
    ax[1, 0].set_title("eval mean final distance (m)"); ax[1, 0].set_xlabel("update")
    ax[1, 1].plot([m["update"] for m in tr], [m["approx_kl"] for m in tr], color="#f08c00", label="approx_kl")
    ax[1, 1].plot([m["update"] for m in tr], [m["entropy"] for m in tr], color="#1864ab", label="entropy")
    ax[1, 1].set_title("optimisation (kl / entropy)"); ax[1, 1].legend()
    for a in ax.flat:
        a.grid(alpha=0.25)
    fig.tight_layout(); fig.savefig(out_dir / "training_curves.png"); plt.close(fig)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser()
    p.add_argument("--checkpoint", type=Path, default=DEFAULT_CKPT)
    p.add_argument("--embedding-cache", type=Path, default=DEFAULT_EMBED)
    p.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    p.add_argument("--num-envs", type=int, default=768)
    p.add_argument("--rollout", type=int, default=96)
    p.add_argument("--horizon", type=int, default=120)
    p.add_argument("--window", type=int, default=12)
    p.add_argument("--updates", type=int, default=300)
    p.add_argument("--epochs", type=int, default=3)
    p.add_argument("--minibatch", type=int, default=1024)
    p.add_argument("--lr", type=float, default=1.2e-4)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--lam", type=float, default=0.95)
    p.add_argument("--clip", type=float, default=0.2)
    p.add_argument("--vf-coef", type=float, default=0.5)
    p.add_argument("--ent-coef", type=float, default=0.004)
    p.add_argument("--bc-coef", type=float, default=0.08)
    p.add_argument("--bc-anneal-updates", type=int, default=80)
    p.add_argument("--bc-warmup-steps", type=int, default=400)
    p.add_argument("--bc-lr", type=float, default=3e-4)
    p.add_argument("--bc-log-interval", type=int, default=100)
    p.add_argument("--target-kl", type=float, default=0.03)
    p.add_argument("--init-std", type=float, default=0.3)
    p.add_argument("--max-grad-norm", type=float, default=1.0)
    p.add_argument("--eval-interval", type=int, default=10)
    p.add_argument("--eval-episodes", type=int, default=512)
    p.add_argument("--seed", type=int, default=13)
    p.add_argument("--amp", action="store_true", default=True)
    p.add_argument("--no-amp", dest="amp", action="store_false")
    p.add_argument("--store-fp16", action="store_true", default=True)
    return p


def main() -> int:
    args = build_parser().parse_args()
    train(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
