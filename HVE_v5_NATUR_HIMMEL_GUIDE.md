# HVE v5.0 – Natur & Himmel Meisterwerk Edition

## Vision
Eine unendliche, einladende Welt mit natürlichem Himmel, glaubwürdigen Bergen, Flüssen und Biomen – bei stabiler Performance auch auf alten Laptops.

Zielwerte:
- TSI >= 98
- TRS >= 9.9

## Warum das schöner aussieht (Mathe kurz erklärt)

### 1) Ridged Multifractal für Berge
Formelidee:
- `ridged = max(0, offset - abs(noise))^2`
- Gewichtung pro Oktave via `weight = clamp(ridged * gain, 0, 1)`

Wirkung:
- `abs(noise)` macht aus Wellen beidseitige Grate.
- `offset - ...` hebt Kanten hervor.
- Quadrieren verstärkt scharfe Grate und lässt Talbereiche weich auslaufen.

Ergebnis:
- Keine „Streifenhügel“, sondern natürliche Felsrücken und Täler.

### 2) Flussführung über Flow-Feld
Formelidee:
- `flow = fbm(p * 0.0008) * 2.0`
- `river = 1 - smoothstep(0.0, 0.15, abs(flow))`

Wirkung:
- `abs(flow)` identifiziert Flusslinien.
- `smoothstep` erzeugt weiche Ufer statt harter Kanten.
- Höhe lokal absenken (4–8 Blöcke) ergibt natürliche Täler.

### 3) Sky Scattering (low-cost)
Formelidee:
- `color = exp(-opticalDepth * (rayleigh + mie)) * sunColor`

In HVE low-spec angenähert über:
- Tag/Nacht-Interpolation
- Sonnen-Highlight `pow(sun, 32)`
- Günstige FBM-Wolken

Wirkung:
- Deutlich bessere Stimmung, Sonnenuntergang und god-rays ohne teure Volumetrik.

## Copy-paste Code (bereits integriert)

### RidgedFBM (Generation)
```cpp
static float RidgedFBM(float x, float z, int octaves = 7, float offset = 0.8f, float gain = 0.6f) {
    float signal = 0.0f;
    float weight = 1.0f;
    float amp = 1.0f;
    float freq = 0.0015f;

    for (int i = 0; i < octaves; ++i) {
        float n = std::abs(s_Ridges.GetNoise(x * freq, z * freq));
        n = offset - n;
        n = std::max(0.0f, n);
        n = n * n;
        n *= weight;
        signal += n * amp;
        weight = std::clamp(n * gain, 0.0f, 1.0f);
        amp *= 0.5f;
        freq *= 2.02f;
    }
    return signal;
}
```

### River-Flow
```cpp
const float riverFlow = (s_River.GetNoise(wx * 0.0008f, wz * 0.0008f) * 2.0f)
    + (s_Hills.GetNoise(wx * 0.0016f, wz * 0.0016f) * 0.35f);
const float riverField = std::abs(riverFlow);
float riverMask = 1.0f - Smoothstep(0.0f, 0.15f, riverField);
```

### Sky Shader Files
- `assets/shaders/world_sky.vert`
- `assets/shaders/world_sky.frag`

Uniforms:
- `u_SunDir`
- `u_TimeOfDay`

## Low-Spec Formeln (integriert)

```text
detail_level = clamp((gpuScore * 0.8) + (ramMB / 32000.0), 0.3, 1.0)
view_dist = 32 + (gpuScore * 48)
memory = chunks * 3.8KB * detail_level
```

Praktisch in HVE:
- Hardware-Erkennung liefert `gpuScore`.
- Auto-Quality reduziert `viewDistance`, `streamMargin` und `uploadBudget` auf schwachen Geräten.

## Dateien mit Änderungen
- `src/Game/World/Generation.cpp`
- `src/Engine/Core/Application.cpp`
- `assets/shaders/world_sky.vert`
- `assets/shaders/world_sky.frag`

## Hinweis zu PDF
Direktes PDF-Exportieren kann in dieser Laufumgebung nicht automatisch ausgeführt werden.
Empfehlung: Diese Markdown-Datei in VS Code öffnen und mit einem Markdown-PDF-Plugin exportieren.
