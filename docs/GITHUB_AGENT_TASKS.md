# GitHub Agent Tasks · Million-in-2min Program

Ziel: In 120 Sekunden massiv höhere Weltabdeckung erreichen und anschließend „Millionen sichtbar“ über LOD/Impostor ermöglichen.

## Regeln für alle Agents

- Jede Aufgabe in eigener Branch bearbeiten.
- Keine Build-Artefakte committen (`build/`, `.obj`, `.tlog`, `.exe`, Logs).
- PR klein halten: 1 Thema pro PR.
- Telemetrie in `hve_last_run.log` vergleichen (vorher/nachher).

## Priorität (verbindlich)

- Reihenfolge ist immer: **(1) Ermöglichen**, **(2) Erfinden**, **(3) Stabilisieren**, **(4) Verschönern**.
- Wenn ein Ziel mit bestehender Architektur nicht erreichbar ist, muss der Agent einen neuen Mechanismus entwerfen (nicht nur Werte erhöhen).
- Jede PR braucht einen Low-PC-Pfad mit klaren Limits/Fallbacks.
- Für „Millionen“-Ziele gilt: nah = echt, fern = LOD/Impostor.
- Steuerung darf nicht geopfert werden: Input-Latenz und Kontrollqualität sind harte Gates.

---

## PR-01 · Far-Field Impostor Ring (höchste Priorität)

**Branch:** `feat/farfield-impostor-ring`

### Scope (PR-01)

- Remote-Terrain als günstige Proxy-Ringe rendern.
- Near-Field bleibt echte Chunk-Pipeline.
- Übergangszone gegen Pop-In (blend/hysteresis).

### Technik-Ansatz (PR-01)

- Neues Render-Modul für Far-Field (separate Draw-Path).
- Pro Ring stark reduzierte Geometrie (height-only oder coarse mesh).
- Distanzbasierte Umschaltung: near=real, far=impostor.

### Akzeptanzkriterien (PR-01)

- Sichtweite wirkt „sofort groß“ nach Start.
- Kein harter FPS-Einbruch bei hoher Sichtweite.
- Beim Übergang near↔far kein starkes Flackern.

### Messung (PR-01)

- 30s: stabile Steuerung (`ctrlLagMs` niedrig), höhere visuelle Abdeckung.
- 120s: deutlich mehr sichtbare Fläche als ohne Impostor.

---

## PR-02 · Generation Bake Cache

**Branch:** `feat/gen-bake-cache`

### Scope (PR-02)

- Persistenter Height/Noise-Bake pro Region/Tile.
- Start lädt Bakes zuerst, Full-Detail später nach.

### Technik-Ansatz (PR-02)

- Region-Keying + Versionsschema.
- Miss: bake erstellen; Hit: direkt verwenden.
- Optional Hintergrund-Refresh bei Seed/Param-Änderung.

### Akzeptanzkriterien (PR-02)

- Startphase lädt schneller als reine on-the-fly Generierung.
- Kein visueller Bruch bei Übergang Bake→Detail.

### Messung (PR-02)

- Cold vs Warm Start vergleichen (Chunks @ 30/60/120s).

---

## PR-03 · Multi-LOD Chunk Mesh Pipeline

**Branch:** `feat/chunk-mesh-lod`

### Scope (PR-03)

- Mehrere LOD-Stufen für entfernte Chunks.
- Priorität: near LOD0, far LOD1/LOD2.

### Technik-Ansatz (PR-03)

- Distanzabhängiger LOD-Selector.
- Mesh-Builder für grobe LODs (vereinfachte Sampling-Dichte).
- Optional asynchrones Upgrade auf höheres LOD.

### Akzeptanzkriterien (PR-03)

- Draw/Tris sinken bei großer Sichtweite.
- Keine Steuerungsverschlechterung durch LOD-Umschaltung.

---

## PR-04 · Startup Loader Scheduler v2

**Branch:** `feat/startup-loader-scheduler-v2`

### Scope (PR-04)

- Startup-Lader priorisiert radial und „ahead of motion“.
- Harte Budgets dynamisch nach Queue-Druck und Input-Qualität.

### Technik-Ansatz (PR-04)

- Separate Queues: critical-near, near, far.
- Defizit-basierte Budgetverteilung + Schutz bei Input-Lag.
- Weiterhin Overlay-Streaming aktiv halten.

### Akzeptanzkriterien (PR-04)

- Kein Stall während `loadingOverlay=1`.
- Höhere Chunks in den ersten 120s als aktuelle Baseline.

---

## PR-05 · Telemetry & Benchmark Harness

**Branch:** `chore/telemetry-bench-harness`

### Scope (PR-05)

- Standardisierte Benchmark-Skripte (30s/120s).
- Kennzahlen in eine einfache Vergleichsdatei schreiben.

### Muss-Metriken (PR-05)

- `chunks`
- `inFlightGen`
- `completed`
- `ctrlLagMs`
- `ctrlQ`
- `latency`
- `loadingOverlay` Status

### Akzeptanzkriterien (PR-05)

- Reproduzierbare Vorher/Nachher-Vergleiche pro PR.

---

## Merge-Strategie

- Reihenfolge: PR-01 → PR-04 → PR-03 → PR-02 → PR-05
- Falls Konflikte: zuerst Scheduler + Telemetrie auf neuesten `main` rebasen.

## Definition of Done (global)

- Build Release erfolgreich.
- Keine neuen kritischen Warn-Spitzen.
- 30s und 120s Messlauf dokumentiert.
- Kurze PR-Notiz: „Was verbessert, was bleibt offen".
