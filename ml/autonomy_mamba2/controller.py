"""Mamba-2 style decision policy for the HESIA inner autonomy loop.

The perception model is intentionally external: YOLO11m-seg provides compact
scene features, while this controller consumes feature sequences, Pixhawk
telemetry, mission goal deltas, and disturbance flags to propose system commands.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any

import torch
from torch import nn


@dataclass(slots=True)
class Mamba2PolicyConfig:
    input_dim: int = 64
    d_model: int = 192
    d_state: int = 16
    layers: int = 6
    expansion: int = 2
    mode_classes: int = 8
    command_dim: int = 4
    dropout: float = 0.05

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


class RMSNorm(nn.Module):
    def __init__(self, dim: int, eps: float = 1e-6) -> None:
        super().__init__()
        self.eps = eps
        self.weight = nn.Parameter(torch.ones(dim))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        scale = torch.rsqrt(x.pow(2).mean(dim=-1, keepdim=True) + self.eps)
        return x * scale * self.weight


class Mamba2Mixer(nn.Module):
    """Small diagonal state-space mixer with linear-time recurrent inference."""

    def __init__(self, cfg: Mamba2PolicyConfig) -> None:
        super().__init__()
        inner = cfg.d_model * cfg.expansion
        self.d_state = cfg.d_state
        self.in_proj = nn.Linear(cfg.d_model, inner * 4)
        self.dt_proj = nn.Linear(inner, inner)
        self.out_proj = nn.Linear(inner, cfg.d_model)
        self.dropout = nn.Dropout(cfg.dropout)
        self.log_a = nn.Parameter(torch.empty(inner, cfg.d_state))
        self.b = nn.Parameter(torch.empty(inner, cfg.d_state))
        self.c = nn.Parameter(torch.empty(inner, cfg.d_state))
        self.reset_parameters()

    def reset_parameters(self) -> None:
        nn.init.normal_(self.log_a, mean=-2.0, std=0.2)
        nn.init.normal_(self.b, mean=0.0, std=0.02)
        nn.init.normal_(self.c, mean=0.0, std=0.02)

    def forward(self, x: torch.Tensor, state: torch.Tensor | None = None) -> tuple[torch.Tensor, torch.Tensor]:
        batch, steps, _ = x.shape
        u, gate, dt_seed, skip = self.in_proj(x).chunk(4, dim=-1)
        u = torch.nn.functional.silu(u)
        gate = torch.sigmoid(gate)
        dt = torch.nn.functional.softplus(self.dt_proj(dt_seed)).clamp(max=10.0)
        decay = torch.exp(-dt.unsqueeze(-1) * torch.exp(self.log_a).unsqueeze(0).unsqueeze(0))

        if state is None:
            state = x.new_zeros(batch, u.shape[-1], self.d_state)

        outputs = []
        b = self.b.unsqueeze(0)
        c = self.c.unsqueeze(0)
        for t in range(steps):
            state = decay[:, t] * state + u[:, t].unsqueeze(-1) * b
            y_t = (state * c).sum(dim=-1)
            outputs.append(y_t * gate[:, t] + skip[:, t])
        y = torch.stack(outputs, dim=1)
        return self.out_proj(self.dropout(y)), state


class Mamba2Block(nn.Module):
    def __init__(self, cfg: Mamba2PolicyConfig) -> None:
        super().__init__()
        hidden = cfg.d_model * cfg.expansion
        self.norm_mixer = RMSNorm(cfg.d_model)
        self.mixer = Mamba2Mixer(cfg)
        self.norm_ff = RMSNorm(cfg.d_model)
        self.ff = nn.Sequential(
            nn.Linear(cfg.d_model, hidden),
            nn.SiLU(),
            nn.Dropout(cfg.dropout),
            nn.Linear(hidden, cfg.d_model),
        )

    def forward(self, x: torch.Tensor, state: torch.Tensor | None = None) -> tuple[torch.Tensor, torch.Tensor]:
        mixed, next_state = self.mixer(self.norm_mixer(x), state)
        x = x + mixed
        x = x + self.ff(self.norm_ff(x))
        return x, next_state


class Mamba2DecisionPolicy(nn.Module):
    """Sequence policy returning velocity/yaw proposals and a decision mode."""

    def __init__(self, cfg: Mamba2PolicyConfig | None = None) -> None:
        super().__init__()
        self.cfg = cfg or Mamba2PolicyConfig()
        self.input_proj = nn.Linear(self.cfg.input_dim, self.cfg.d_model)
        self.blocks = nn.ModuleList(Mamba2Block(self.cfg) for _ in range(self.cfg.layers))
        self.norm = RMSNorm(self.cfg.d_model)
        self.command_head = nn.Sequential(
            nn.Linear(self.cfg.d_model, self.cfg.d_model),
            nn.SiLU(),
            nn.Linear(self.cfg.d_model, self.cfg.command_dim),
            nn.Tanh(),
        )
        self.mode_head = nn.Linear(self.cfg.d_model, self.cfg.mode_classes)

    def forward(
        self,
        features: torch.Tensor,
        states: list[torch.Tensor | None] | None = None,
    ) -> dict[str, torch.Tensor | list[torch.Tensor]]:
        if features.ndim != 3:
            raise ValueError("features must have shape [batch, time, input_dim]")
        if features.shape[-1] != self.cfg.input_dim:
            raise ValueError(f"expected input_dim={self.cfg.input_dim}, got {features.shape[-1]}")

        x = self.input_proj(features)
        next_states: list[torch.Tensor] = []
        if states is None:
            states = [None] * len(self.blocks)
        for block, state in zip(self.blocks, states, strict=True):
            x, next_state = block(x, state)
            next_states.append(next_state)
        h = self.norm(x)
        return {
            "command": self.command_head(h),
            "mode_logits": self.mode_head(h),
            "states": next_states,
        }

    @torch.no_grad()
    def step(self, feature: torch.Tensor, states: list[torch.Tensor | None] | None = None) -> dict[str, Any]:
        out = self.forward(feature[:, None, :], states=states)
        return {
            "command": out["command"][:, -1],
            "mode_logits": out["mode_logits"][:, -1],
            "states": out["states"],
        }


def count_parameters(model: nn.Module) -> int:
    return sum(param.numel() for param in model.parameters())
