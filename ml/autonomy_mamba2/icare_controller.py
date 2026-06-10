"""Icare decision model.

Icare combines a pretrained Mamba2 language backbone for mission text
understanding with the local linear-time Mamba2-style temporal controller used
for continuous drone commands.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import torch
from torch import nn

from .controller import Mamba2DecisionPolicy, Mamba2PolicyConfig, count_parameters
from .features_icare import ICARE_ACTION_DIM, ICARE_FEATURE_DIM, ICARE_MODES


DEFAULT_LANGUAGE_MODEL = Path("F:/Set-Donner/models/AntonV__mamba2-1.3b-hf")


@dataclass(slots=True)
class IcareConfig:
    feature_dim: int = ICARE_FEATURE_DIM
    mission_dim: int = 2048
    mission_proj_dim: int = 32
    d_model: int = 384
    d_state: int = 16
    layers: int = 8
    expansion: int = 2
    command_dim: int = ICARE_ACTION_DIM
    mode_classes: int = len(ICARE_MODES)
    dropout: float = 0.05

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


class IcareDecisionModel(nn.Module):
    def __init__(self, cfg: IcareConfig | None = None) -> None:
        super().__init__()
        self.cfg = cfg or IcareConfig()
        self.mission_proj = nn.Sequential(
            nn.LayerNorm(self.cfg.mission_dim),
            nn.Linear(self.cfg.mission_dim, self.cfg.mission_proj_dim),
            nn.SiLU(),
        )
        policy_cfg = Mamba2PolicyConfig(
            input_dim=self.cfg.feature_dim + self.cfg.mission_proj_dim,
            d_model=self.cfg.d_model,
            d_state=self.cfg.d_state,
            layers=self.cfg.layers,
            expansion=self.cfg.expansion,
            mode_classes=self.cfg.mode_classes,
            command_dim=self.cfg.command_dim,
            dropout=self.cfg.dropout,
        )
        self.policy = Mamba2DecisionPolicy(policy_cfg)
        self.complete_head = nn.Sequential(
            nn.Linear(self.cfg.command_dim + self.cfg.mode_classes, 64),
            nn.SiLU(),
            nn.Linear(64, 1),
        )

    def forward(
        self,
        features: torch.Tensor,
        mission_embedding: torch.Tensor,
        states: list[torch.Tensor | None] | None = None,
    ) -> dict[str, torch.Tensor | list[torch.Tensor]]:
        if features.ndim != 3:
            raise ValueError("features must have shape [batch, time, feature_dim]")
        if mission_embedding.ndim == 2:
            mission_embedding = mission_embedding[:, None, :].expand(-1, features.shape[1], -1)
        mission_small = self.mission_proj(mission_embedding)
        policy_in = torch.cat([features, mission_small], dim=-1)
        out = self.policy(policy_in, states=states)
        complete_in = torch.cat([out["command"], out["mode_logits"]], dim=-1)  # type: ignore[index]
        out["complete_logit"] = self.complete_head(complete_in).squeeze(-1)
        return out

    @torch.no_grad()
    def step(
        self,
        feature: torch.Tensor,
        mission_embedding: torch.Tensor,
        states: list[torch.Tensor | None] | None = None,
    ) -> dict[str, Any]:
        out = self.forward(feature[:, None, :], mission_embedding, states=states)
        return {
            "command": out["command"][:, -1],
            "mode_logits": out["mode_logits"][:, -1],
            "complete_logit": out["complete_logit"][:, -1],
            "states": out["states"],
        }


def icare_parameter_count(model: nn.Module) -> dict[str, int]:
    return {
        "total": count_parameters(model),
        "trainable": sum(param.numel() for param in model.parameters() if param.requires_grad),
    }
