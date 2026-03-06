# Million in 2 Minutes · Handoff für GitHub/Agenten

## Ziel

- In 120 Sekunden extrem hohe Weltabdeckung laden.
- Kurzfristig: maximaler echter Chunk-Durchsatz.
- Mittelfristig: „Millionen sichtbar“ per Far-Field-LOD/Impostor.

## Bereits umgesetzt

- Streaming läuft auch während `loadingOverlay=1` (kein Prestart-Stall mehr).
- Harte Caps stark erhöht (`HVE_STREAM_DISTANCE_CAP`, `HVE_STREAM_INFLIGHT_MAX`, `HVE_STREAM_ENQUEUE_MAX`, `HVE_PRELOAD_BOOTSTRAP_MAX`).
- Kontinuierlicher Mega-Preload per Pulse (`HVE_MEGA_PRELOAD_*`).
- Extremprofil: `run_profile_million_2min.ps1`.

## Aktueller Flaschenhals

- Full-Chunk-Generierung + Full-Mesh pro Chunk ist der Hauptkostenblock.
- Ohne LOD/Impostor ist 1,000,000 **echte** meshed Chunks in 120s praktisch unrealistisch.

## Nächste PRs (für weitere Chatbots)

### PR-1: Far-Field Impostor Ring (höchste Priorität)

- Erzeuge sehr günstige Proxy-Kacheln für große Distanzen.
- Echte Chunks nur im Near-Field behalten.
- Akzeptanz: >1M „sichtbare“ Zellen/Chunks-Äquivalent bei stabiler Steuerung.

### PR-2: Chunk Generation Cache/Bake

- Persistente Height/Noise-Bakes pro Region.
- Beim Start zuerst Bakes laden, später feiner remeshen.
- Akzeptanz: Startdurchsatz deutlich höher als reine on-the-fly Generation.

### PR-3: Mesh LOD Pipeline

- Für entfernte Chunks niedrigere Mesh-Auflösung.
- Optional Remesh in Hintergrundqualität.
- Akzeptanz: gleiche Sichtweite bei weniger Tris/Draws.

### PR-4: Upload/Render Decoupling

- Renderbare Warteschlange priorisieren (near-first strict).
- Große Upload-Batches in getrenntem Budgetfenster.
- Akzeptanz: weniger Input-Lag bei hoher Ladeintensität.

## Experiment-Profile

- `run_profile_balanced.ps1`: spielbare aggressive Defaults.
- `run_profile_million_2min.ps1`: maximale Startlast für 120s.

## Standard-Testprotokoll

1. Build Release.
2. 30s Smoke: `WARN_COUNT`, `SNAP` Wachstum, `ctrlLagMs`.
3. 120s Run: Chunks bei t=30/60/90/120, Overlay-Phase, Input-Qualität.
4. Regression-Gate: keine neuen Abstürze, `WARN_COUNT` nicht explodiert.
