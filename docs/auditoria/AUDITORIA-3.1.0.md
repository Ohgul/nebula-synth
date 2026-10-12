# Auditoría de Nebula 3.1.0 (fase 0 de FORGE)

Fecha: 11 oct 2026. Rama de trabajo: `forge/v4-dev`, creada desde `nebula-v3-dev` @ `c63506b`.

## 1. Dónde está la 3.1.0

| Rama | Último commit | Qué contiene |
| --- | --- | --- |
| `nebula-v3-dev` | `c63506b` (11 oct) | **La 3.1.0 completa**: análisis adaptativo, cola real, MIDI, ADN, Cosmos/Abismo, % de parecido honesto, interfaz grafito, CI con pruebas. Es el PR #2. |
| `main` | `99622cc` (10 oct) | La 3.0 subida a mano: `analyse (..., 64, 30.0)`, región automática de 4 s y sin la carpeta `tests/`. **No es la 3.1.0.** |
| `feature/nebula-minimal-ui` | `cca9cde` (10 oct) | Primera paleta minimalista y la compensación de ruido del 808. Es el PR #1 y ya está incluido en `nebula-v3-dev`. |
| `design/inverse-synthesis-forge` | `7ef3c5f` | Sólo `docs/NEBULA-FORGE-ARQUITECTURA.md` (PR #3). No tiene código. |

La discrepancia de "64 cuadros / 30 s" frente a "adaptativo / 60 s" se explica así: `main` nunca recibió la 3.1.0. La 3.1.0 vive en `nebula-v3-dev`, que usa `analyse (..., 0, 120.0)` (cuadros adaptativos, máximo 192) y analiza hasta 60 s.

## 2. Baseline reproducible (antes de FORGE)

Comando: `NebulaTests <prueba>` (Linux, clang, Release, 2 núcleos, máquina de pruebas lenta).

| Prueba | Resultado | Tiempo |
| --- | --- | --- |
| Ui | OK | 26 s |
| Stereo | OK | 31 s |
| Determinism (38 proyectos) | OK | 49 s |
| V3 | OK | 33 s |
| Midi | OK | 28 s |
| Dna | OK | 53 s |
| Transform | OK | 53 s |
| Render | OK | 38 s |
| V21 | OK | 19 s |
| V2 | OK | 51 s |
| Imitate | OK | 130 s |
| Banco completo | OK | ver abajo |

El banco completo (métricas, fidelidad de RECREAR, CPU, latencia, estabilidad, clics y compatibilidad) está en [baseline-3.1.0-banco.md](baseline-3.1.0-banco.md). Lo principal:

- **Compatibilidad:** 38 de 38 proyectos 3.0 suenan igual. Peor diferencia: -106.7 dB en Linux y -91.3 dB en el Mac del CI; el criterio es ≤ -90 dB.
- **CPU de la resíntesis** (un núcleo, bloque de 512): 13.4 % a 1x, 29 % a 2x y 55 % a 4x.
- **Latencia reportada / medida:** 0/0 muestras a 1x, 3/2 a 2x y 4/3 a 4x.
- **Clics:** los robos de voz no suben el pico respecto al control (-0.3 y -0.9 dB).
- **Estabilidad:** 200 s de notas y automatización al azar sin NaN.

## 3. Hallazgos

| # | Hallazgo | Impacto | Estado |
| --- | --- | --- | --- |
| 1 | `OfflineSynth::renderStereo` ponía el note-off en una posición relativa al bloque, pero `renderNextBlock (buf, midi, pos, n)` usa posiciones absolutas. **La nota nunca se soltaba.** | El % de parecido medía mal los sonidos sostenidos (pads: -4 dB de error de envolvente que no era real). "Arrastrar audio a Ableton" exportaba notas sin su suelta. | **Corregido** en esta rama. Pad con vibrato: 67 % → 86 %; voz: 80 % → 90 %. |
| 2 | El "genoma" (`GenomeOps`) es un vector plano de valores normalizados, sin topología, módulos ni rutas. | No sirve como ADN estructural editable. | FORGE introduce `SynthPatch` (ver `docs/FORGE.md`). El DNA Studio estructural queda para la fase 4. |
| 3 | Hay dos renderizadores offline: `OfflineSynth` (atado a un procesador y su lista de parámetros) y el nuevo `Forge::PatchRenderer` (independiente). | Duplicación. | Pendiente: migrar el imitador y el % de parecido a `PatchRenderer`. |
| 4 | El imitador (`Imitator`) busca patches por evolución al azar con una distancia gruesa (24 bandas). | Se superpone con FORGE y es menos explicable. | Se mantiene por compatibilidad. FORGE no lo usa. |
| 5 | RECREAR es resíntesis por cuadros (tablas derivadas del audio), no una receta paramétrica. | Correcto como modo propio, pero no debe presentarse como "ADN recuperado". | Se mantiene, etiquetado como resíntesis. FORGE produce recetas del motor Synth. |
| 6 | Estabilidad: el bloque más lento llega a 7.9× el tiempo real (Sinte) y 12.6× (Sample) en el banco. Hay 6327 muestras por encima de 1.0 con 200 s de automatización extrema (la protección las limita a 1.5). | Riesgo de cortes puntuales en el DAW si se repite en vivo. | **Sin investigar**: probablemente asignaciones o cálculo de tablas en el hilo de audio. Pendiente para la fase de producto. |
| 7 | El CI sólo compila y prueba en macOS. No hay Windows ni pluginval. | Requisitos 11 del encargo. | Pendiente. |
| 8 | La ponderación K del % de parecido usa los coeficientes de 48 kHz. | Pequeña desviación a 44.1 kHz. | Documentado. |
| 9 | Los parámetros viven en un solo APVTS con IDs estables; todos los de síntesis son automatizables (verificado en `ForgeTest`). | — | OK |
