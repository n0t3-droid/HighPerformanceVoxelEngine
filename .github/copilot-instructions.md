# HighPerformanceVoxelEngine · Copilot Instructions

## Priority Doctrine (Always First)
- Prioritize by impact in this strict order: **(1) Core viability**, **(2) invention/new mechanism**, **(3) measurable throughput**, **(4) polish**.
- Prefer creating new mechanisms when existing ones cannot meet goals; do not stop at parameter tweaks if architecture is the bottleneck.
- Every major change must include a low-PC path (bounded CPU/GPU/RAM fallback), not only high-end behavior.
- For huge goals (e.g., million-scale world visibility), split into two layers: **real near-field correctness** + **far-field illusion/LOD**.
- Do not trade input responsiveness for loading speed; controls remain first-class.
- Before closing work, state: what was invented, what was enabled that did not exist before, and how it behaves on low-spec hardware.

## Execution Rules
- Prefer root-cause architecture work over value tuning.
- Keep telemetry-driven validation in every performance PR.
- Never commit build outputs or runtime logs.
