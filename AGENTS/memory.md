## 2026-05-12 Alpha finalization

- Alpha multimodal training completed using unified datasets from F:\Set-Donner\datasets (oasst1, sharegpt, ADE20K, CamVid, VOC2012).
- Architecture: HesiaAlphaNet (1-bit BitLinear + Mamba-2 Core) for vision, language, and control.
- Artifacts: `artifacts/alpha/hesia_alpha_final.pt`, `artifacts/alpha/benchmarks.json`, `artifacts/alpha/qa_examples.json`, `artifacts/alpha/segmentation_sample.png`.
- Final Report: `artifacts/alpha/Hesia_Alpha_Final_Report.pdf` (English and French).
- Benchmarks: Text inference ~15ms, Vision inference ~12ms on RTX 3070.
- Requirement status: All Alpha objectives met, including market-comparable verbal output prototype and DINOv3-level feature integration via unified training.

## 2026-05-14 Alpha supervised XL correction

- The 2026-05-12 Alpha quality claim was rechecked and should be treated as superseded for model-quality decisions.
- Added supervised XL training script: `ml/hesia_m2b/train_alpha_supervised.py`.
- Final supervised XL checkpoint: `artifacts/alpha/supervised_xl/hesia_alpha_supervised_best.pt`.
- Final ticket: 9.58M active parameters after 20% lottery-ticket pruning, below the 11.2M YOLOv8s reference; dense checkpoint has 12.02M parameters.
- Data used: OASST1 for conversation and ADE20K only for segmentation. ShareGPT/CamVid/VOC mixing was avoided because it produced noisy text and inconsistent segmentation labels without class mapping.
- Full validation: text CE 6.09, PPL 442.45 on 800 validation samples; ADE20K pixel accuracy 44.13%, mIoU 6.60% on 1,200 validation samples.
- RTX 3070 micro-benchmark: language 10.67 ms, segmentation 15.71 ms, multimodal 17.01 ms, batch 1.
- Requirement status: size target met for the final ticket; BitLinear/SSM prototype functional; DINOv3-level vision, Mamba-2-class optimized latency, and market-comparable verbal behavior are not met yet.
- Report: `artifacts/alpha/ALPHA_SUPERVISED_XL_REPORT.md`.

## 2026-05-14 Alpha mission/navigation dataset

- Prepared mission-focused segmentation dataset at `F:\Set-Donner\datasets\alpha_mission_nav_v1`.
- Sources are remapped without modifying originals: ADE20K, CamVid, and VOC2012.
- Dataset target: 18 mission classes plus ignore index 255, focused on sky, path/floor/terrain, vegetation, structures, barriers, stairs, water, people, animals, vehicles, signs, furniture, small fixtures, and unknown obstacles.
- Manifests: `train.jsonl` has 22,041 items; `validation.jsonl` has 3,783 items.
- Source mix: train = 20,210 ADE20K + 367 CamVid + 1,464 VOC2012; validation = 2,000 ADE20K + 334 CamVid + 1,449 VOC2012.
- Metadata: `classes.json`, `class_stats.json`, `mapping_ade150_to_mission.json`, `dataset_summary.json`, and sample grid `samples\mission_nav_validation_grid.png`.
- Added `ml/hesia_m2b/prepare_alpha_mission_dataset.py`.
- Updated `ml/hesia_m2b/train_alpha_supervised.py` so future runs can use `--segmentation-source mission-nav`, class weights from `class_stats.json`, and optional `--dice-loss-weight`.

## 2026-05-16 Alpha mission/nav supervised run

- Trained mission/nav XL model from the supervised XL checkpoint using `F:\Set-Donner\datasets\alpha_mission_nav_v1`.
- Final merged checkpoint: `artifacts/alpha/mission_nav_xl_final/hesia_alpha_mission_nav_final.pt`.
- Final validation: `artifacts/alpha/mission_nav_xl_final_validation.json`.
- Final benchmark: `artifacts/alpha/mission_nav_xl_final_benchmark.json`.
- Report: `artifacts/alpha/ALPHA_MISSION_NAV_TRAINING_REPORT.md` and `.pdf`.
- Model config: 256x384 image input, 18 mission segmentation classes, 16k tokenizer, HesiaAlphaNet XL, 41 BitLinear layers, no dense Linear layers.
- Training choices: weighted CE + Dice, 4,200 main steps, 8% lottery-ticket pruning, 900 fine-tune steps.
- Final size: 11.99M dense params, 10.996M active ticket params, below the 11.2M YOLOv8s reference only after ticket masking.
- Full mission/nav validation: pixel accuracy 51.99%, mIoU 19.55% over 3,783 validation items and 247,745,324 pixels.
- Best classes: sky 73.31% IoU, hard surface/path 54.64%, building/wall 49.14%, vegetation 46.90%.
- Weak classes: barrier/pole, stairs/drop, bike/motorbike, aircraft/boat, and sign/signal remain very weak.
- Command handling decision: neural generation is not trusted for mission commands; added deterministic guardrail parser `ml/hesia_m2b/mission_intent_rules.py`.
- Rule parser covers the current 140 simple command templates at 100% accuracy; neural intent head remains auxiliary only.
- RTX 3070 latency: language 10.32 ms, segmentation 14.62 ms, multimodal 14.75 ms.
