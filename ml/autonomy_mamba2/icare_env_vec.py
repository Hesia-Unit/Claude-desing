"""GPU-vectorised quadrotor mission environment for Icare RL training.

This is a fully batched (N parallel environments) re-implementation of the
single-environment ``SyntheticDroneMission`` used by the legacy fallback
trainer.  Semantics (dynamics, 96-d feature layout, reward shaping, terminal
conditions) are kept faithful so a policy trained here stays compatible with
the existing Icare feature/command contract, but everything runs as dense CUDA
tensors so we can collect millions of transitions for PPO.

Why not Isaac Sim: the Omniverse RTX renderer requires the proprietary NVIDIA
Vulkan driver with ray-tracing extensions, which the WSL2 driver model does not
expose (CUDA + D3D12 only, no NVIDIA Vulkan ICD).  Icare consumes compact 96-d
perception/telemetry features rather than pixels, so a high-throughput physics +
feature simulator is the appropriate tool and runs thousands of envs on a single
RTX 3080.
"""

from __future__ import annotations

from dataclasses import dataclass

import torch

from .features_icare import ICARE_ACTION_DIM, ICARE_FEATURE_DIM

N_OBST = 6


@dataclass
class IcareVecConfig:
    num_envs: int = 1024
    horizon: int = 120
    n_obstacles: int = N_OBST
    device: str = "cuda"
    seed: int = 13


class IcareVecEnv:
    """Batched continuous-control drone mission (north/east plane + altitude)."""

    feature_dim = ICARE_FEATURE_DIM
    action_dim = ICARE_ACTION_DIM

    def __init__(self, cfg: IcareVecConfig) -> None:
        self.cfg = cfg
        self.n = cfg.num_envs
        self.k = cfg.n_obstacles
        self.device = torch.device(cfg.device)
        self.gen = torch.Generator(device=self.device)
        self.gen.manual_seed(cfg.seed)
        z = lambda *s: torch.zeros(*s, device=self.device)
        self.pos = z(self.n, 2)
        self.vel = z(self.n, 2)
        self.target = z(self.n, 2)
        self.home = z(self.n, 2)
        self.altitude = z(self.n)
        self.battery = z(self.n)
        self.wind = z(self.n, 2)
        self.motor_fault = torch.full((self.n,), -1, device=self.device, dtype=torch.long)
        self.phase = z(self.n).long()  # 0 outbound, 1 return_home
        self.step_count = z(self.n).long()
        self.prev_distance = z(self.n)
        self.obstacles = z(self.n, self.k, 2)
        self.obstacle_radius = z(self.n, self.k)
        self.reset(torch.arange(self.n, device=self.device))

    # ---- helpers -------------------------------------------------------
    def _rand(self, *shape, lo: float = 0.0, hi: float = 1.0) -> torch.Tensor:
        return torch.rand(*shape, generator=self.gen, device=self.device) * (hi - lo) + lo

    def _randn(self, *shape, std: float = 1.0) -> torch.Tensor:
        return torch.randn(*shape, generator=self.gen, device=self.device) * std

    def reset(self, idx: torch.Tensor) -> None:
        m = idx.numel()
        if m == 0:
            return
        target = self._rand(m, 2, lo=-420.0, hi=420.0)
        close = target.norm(dim=-1) < 180.0
        target[close] *= 2.0
        self.target[idx] = target
        self.obstacles[idx] = self._rand(m, self.k, 2, lo=-390.0, hi=390.0)
        self.obstacle_radius[idx] = self._rand(m, self.k, lo=26.0, hi=70.0)
        has_fault = self._rand(m) < 0.28
        fault_id = torch.randint(0, 4, (m,), generator=self.gen, device=self.device)
        self.motor_fault[idx] = torch.where(has_fault, fault_id, torch.full_like(fault_id, -1))
        self.wind[idx] = self._randn(m, 2, std=2.8)
        self.pos[idx] = 0.0
        self.vel[idx] = 0.0
        self.home[idx] = 0.0
        self.altitude[idx] = self._rand(m, lo=90.0, hi=150.0)
        self.battery[idx] = self._rand(m, lo=0.72, hi=0.96)
        self.phase[idx] = 0
        self.step_count[idx] = 0
        self.prev_distance[idx] = (self.target[idx] - self.pos[idx]).norm(dim=-1)

    def reset_all(self) -> torch.Tensor:
        self.reset(torch.arange(self.n, device=self.device))
        return self.features()

    # ---- geometry ------------------------------------------------------
    def _goal(self) -> torch.Tensor:
        return torch.where(self.phase[:, None] == 0, self.target, self.home)

    def _clearance(self) -> torch.Tensor:
        # signed distance to nearest obstacle surface (min over k)
        d = (self.obstacles - self.pos[:, None, :]).norm(dim=-1) - self.obstacle_radius
        return d.min(dim=-1).values

    # ---- dynamics ------------------------------------------------------
    def step(self, action: torch.Tensor):
        a = action.clamp(-1.0, 1.0)
        north = a[:, 0] * 17.5
        east = a[:, 1] * 17.5
        down = a[:, 2] * 5.0
        motors = ((a[:, 4:8] + 1.0) * 0.5).clamp(0.0, 1.0)
        fault = self.motor_fault
        has_fault = fault >= 0
        if has_fault.any():
            fr = fault.clamp(min=0)
            scale = torch.ones_like(motors)
            scale[has_fault] = scale[has_fault].scatter(
                1, fr[has_fault, None], 0.12
            )
            motors = motors * scale
        command = torch.stack([north, east], dim=-1)
        accel = 0.44 * (command - self.vel) + 0.10 * self.wind
        imbalance = motors.std(dim=-1)
        self.vel = (self.vel + accel).clamp(-24.0, 24.0)
        self.pos = self.pos + self.vel
        self.altitude = (self.altitude - down + 0.9 * (motors.mean(dim=-1) - 0.5) - imbalance).clamp(8.0, 310.0)
        speed = self.vel.norm(dim=-1)
        self.battery = (self.battery - 0.0016 - 0.0018 * speed / 24.0 - 0.0012 * motors.mean(dim=-1)).clamp(min=0.0)
        self.step_count = self.step_count + 1

        goal = self._goal()
        dist = (goal - self.pos).norm(dim=-1)
        progress = self.prev_distance - dist
        self.prev_distance = dist
        clearance = self._clearance()

        reward = 0.055 * progress - 0.025
        reward = reward + 0.20 * (clearance / 80.0).clamp(min=0.0)
        reward = reward - 0.75 * (1.0 - clearance / 36.0).clamp(min=0.0)
        reward = reward - 0.18 * (self.altitude - 120.0).abs() / 120.0
        low_bat = (self.battery < 0.18) & (self.phase != 1)
        reward = reward - 1.15 * low_bat.float()
        reward = reward - 0.30 * imbalance

        done = torch.zeros(self.n, dtype=torch.bool, device=self.device)
        reason = torch.zeros(self.n, dtype=torch.long, device=self.device)  # 0 none

        reached_out = (self.phase == 0) & (dist < 32.0)
        reward = reward + 8.0 * reached_out.float()
        # switch outbound->return
        switch = reached_out
        self.phase = torch.where(switch, torch.ones_like(self.phase), self.phase)
        if switch.any():
            self.prev_distance[switch] = (self.home[switch] - self.pos[switch]).norm(dim=-1)

        reached_home = (self.phase == 1) & (dist < 28.0) & (~switch)
        reward = reward + 13.0 * reached_home.float()
        done = done | reached_home
        reason = torch.where(reached_home, torch.full_like(reason, 1), reason)  # 1 complete

        low_bat_out = (self.battery < 0.13) & (self.phase == 0)
        self.phase = torch.where(low_bat_out, torch.ones_like(self.phase), self.phase)
        if low_bat_out.any():
            self.prev_distance[low_bat_out] = (self.home[low_bat_out] - self.pos[low_bat_out]).norm(dim=-1)
        reward = reward + 0.8 * low_bat_out.float()

        collision = clearance < 3.0
        reward = reward - 12.0 * collision.float()
        done = done | collision
        reason = torch.where(collision & (reason == 0), torch.full_like(reason, 2), reason)  # 2 collision

        ground = self.altitude <= 10.0
        reward = reward - 8.0 * ground.float()
        done = done | ground
        reason = torch.where(ground & (reason == 0), torch.full_like(reason, 3), reason)  # 3 ground

        empty = self.battery <= 0.0
        reward = reward - 9.0 * empty.float()
        done = done | empty
        reason = torch.where(empty & (reason == 0), torch.full_like(reason, 4), reason)  # 4 battery

        timeout = self.step_count >= self.cfg.horizon
        done = done | timeout
        reason = torch.where(timeout & (reason == 0), torch.full_like(reason, 5), reason)  # 5 timeout

        info = {"distance": dist, "clearance": clearance, "reason": reason,
                "battery": self.battery.clone(), "altitude": self.altitude.clone(),
                "phase": self.phase.clone()}
        return reward, done, info

    # ---- 96-d feature packing (matches SyntheticDroneMission.features) --
    def features(self) -> torch.Tensor:
        n, k = self.n, self.k
        goal = self._goal()
        delta = goal - self.pos
        dist = delta.norm(dim=-1)
        bearing = torch.atan2(delta[:, 1], delta[:, 0])
        heading = delta / dist.clamp(min=1.0)[:, None]
        side = torch.stack([-heading[:, 1], heading[:, 0]], dim=-1)

        rel = self.obstacles - self.pos[:, None, :]                      # [n,k,2]
        fwd = (rel * heading[:, None, :]).sum(-1)                        # [n,k]
        lat = (rel * side[:, None, :]).sum(-1)                           # [n,k]
        radius = self.obstacle_radius
        in_band = (fwd > -30) & (fwd < 260)
        closeness = (1.0 - (torch.hypot(fwd, lat) - radius) / 240.0).clamp(min=0.0) * in_band.float()
        min_depth_each = ((fwd - radius) / 260.0).clamp(min=0.0)
        min_depth = torch.where(in_band, min_depth_each, torch.ones_like(min_depth_each)).amin(dim=-1).clamp(max=1.0)
        total = (closeness * 0.13).sum(-1)
        center_m = (lat.abs() < 45) & (fwd > 0)
        left_m = lat < 0
        right_m = lat >= 0
        front_m = (fwd > 0) & (lat.abs() < 75)
        center = (closeness * center_m.float()).sum(-1)
        left = (closeness * (left_m & ~center_m).float()).sum(-1)
        right = (closeness * (right_m & ~center_m).float()).sum(-1)
        front = (closeness * front_m.float()).sum(-1)

        c01 = lambda x: x.clamp(0.0, 1.0)
        c11 = lambda x: x.clamp(-1.0, 1.0)
        f = torch.zeros(n, ICARE_FEATURE_DIM, device=self.device)
        alt = self.altitude
        bat = self.battery
        ph_ret = (self.phase == 1).float()
        f[:, 3] = c01(total)
        f[:, 4] = c01(0.65 + 0.25 * ph_ret)
        f[:, 5] = 0.35
        f[:, 6] = c01(front)
        f[:, 7] = c01(1.0 - front * 0.3)
        f[:, 8] = 0.82
        f[:, 9] = min(k / 100.0, 1.0)
        f[:, 10] = c01(center)
        f[:, 11] = c01(left)
        f[:, 12] = c01(right)
        f[:, 13] = c01((alt - 20.0) / 180.0)
        f[:, 18] = alt / 1000.0
        f[:, 19] = alt / 1000.0
        f[:, 24] = self.vel[:, 0] / 50.0
        f[:, 25] = self.vel[:, 1] / 50.0
        f[:, 28] = self.vel.norm(dim=-1) / 50.0
        f[:, 30] = 0.72
        f[:, 32] = bat
        f[:, 36] = c11(delta[:, 0] / 500.0)
        f[:, 37] = c11(delta[:, 1] / 500.0)
        f[:, 38] = c11((120.0 - alt) / 500.0)
        f[:, 39] = (dist / 500.0).clamp(0.0, 2.0)
        f[:, 40] = bearing / torch.pi
        f[:, 41] = 120.0 / 500.0
        f[:, 42] = 0.5 * ph_ret
        f[:, 43] = c01(1.0 - dist / 600.0)
        wind_n = self.wind.norm(dim=-1)
        f[:, 44] = (wind_n < 2.0).float()
        f[:, 45] = (wind_n >= 2.0).float()
        f[:, 47] = (self.wind[:, 1].abs() > self.wind[:, 0].abs()).float()
        f[:, 49] = (self.motor_fault >= 0).float()
        f[:, 51] = (bat < 0.20).float()
        f[:, 64] = min(k / 32.0, 1.0)
        f[:, 65] = c01(min_depth)
        f[:, 66] = c11((center - left + right) / 3.0)
        f[:, 67] = -c01(front)
        f[:, 70] = c01(front)
        f[:, 71] = c01(left)
        f[:, 72] = c01(right)
        f[:, 73] = c01(front)
        f[:, 74] = c01(front / min_depth.clamp(min=0.05))
        f[:, 75] = 1.0
        f[:, 76] = c01(1.0 - total)
        f[:, 77] = c01(min_depth)
        f[:, 78] = c01(min_depth - center * 0.15)
        f[:, 79] = c01(min_depth - left * 0.1)
        f[:, 80] = c01(min_depth - right * 0.1)
        f[:, 81] = f[:, 13]
        f[:, 82] = c11((120.0 - alt) / 120.0)
        f[:, 83] = c11((left - right) / 2.0)
        f[:, 84] = min(k / 32.0, 1.0)
        f[:, 85] = 0.82
        f[:, 86] = c01(total)
        f[:, 87] = c01(torch.maximum(torch.maximum(center, left), right))
        f[:, 88] = c11(right - left)
        f[:, 89] = c01(center)
        f[:, 90] = c01(center)
        f[:, 91] = c01(left)
        f[:, 92] = c01(right)
        f[:, 93] = (self.step_count.float() / 30.0).clamp(max=1.0)
        f[:, 94] = c01(total * 0.82)
        f[:, 95] = 1.0
        return f

    # ---- vectorised expert (for BC warmup / reference) -----------------
    def expert_action(self) -> torch.Tensor:
        goal = self._goal()
        delta = goal - self.pos
        dist = delta.norm(dim=-1).clamp(min=1.0)
        desired = delta / dist[:, None]
        rel = self.pos[:, None, :] - self.obstacles
        d = rel.norm(dim=-1).clamp(min=1.0)
        infl = (self.obstacle_radius + 140.0)
        active = (d < infl).float()
        push = (rel / d[..., None]) * ((infl - d).clamp(min=0.0) / 140.0)[..., None] * active[..., None]
        avoid = push.sum(dim=1)
        vec = desired + 1.35 * avoid - 0.045 * self.wind
        vec = vec / vec.norm(dim=-1, keepdim=True).clamp(min=1.0)
        out = torch.zeros(self.n, ICARE_ACTION_DIM, device=self.device)
        out[:, 0] = vec[:, 0].clamp(-1, 1)
        out[:, 1] = vec[:, 1].clamp(-1, 1)
        out[:, 2] = ((self.altitude - 120.0) / 90.0).clamp(-1, 1)
        out[:, 3] = ((vec[:, 1] - vec[:, 0]) * 0.25).clamp(-1, 1)
        motor = 0.58 + 0.10 * (1.0 - self.battery).clamp(min=0.0) + 0.05 * out[:, 2].abs()
        mc = motor[:, None].repeat(1, 4)
        has_fault = self.motor_fault >= 0
        mc[has_fault] += 0.10
        if has_fault.any():
            fr = self.motor_fault.clamp(min=0)
            mc[has_fault] = mc[has_fault].scatter(1, fr[has_fault, None], 0.18)
        out[:, 4:8] = (mc * 2.0 - 1.0).clamp(-1, 1)
        out[:, 8] = (-vec[:, 1]).clamp(-1, 1)
        out[:, 9] = vec[:, 1].clamp(-1, 1)
        out[:, 10] = (-out[:, 2]).clamp(-1, 1)
        out[:, 11] = (-out[:, 2]).clamp(-1, 1)
        out[:, 12] = (vec[:, 1] * 0.5).clamp(-1, 1)
        out[:, 13] = (vec[:, 1] * 0.5).clamp(-1, 1)
        out[:, 14] = 0.15 * (self.phase == 1).float()
        out[:, 15] = 0.15 * (self.phase == 1).float()
        out[:, 17] = -1.0
        return out
