# NEBULA-CORE-040 · P0: auditoría del repositorio

Rama de trabajo: `feature/nebula-0.4.0`, creada desde `forge/v4-dev` @ `7e82f13`. Contiene la 3.1.0 y la primera etapa de FORGE.

## Fuente base

| Rama | Commit | Estado |
| --- | --- | --- |
| `main` | `99622cc` | 3.0 subida a mano. No tiene la 3.1.0 ni las pruebas. **No se modifica.** |
| `feature/nebula-minimal-ui` | `cca9cde` | PR #1. Ya está incluida en `nebula-v3-dev`. |
| `nebula-v3-dev` | `c63506b` | **3.1.0** (PR #2): CI de Mac en verde, 11 pruebas y 38 proyectos de compatibilidad. |
| `design/inverse-synthesis-forge` | `7ef3c5f` | PR #3. Sólo documento de arquitectura. |
| `forge/v4-dev` | `7e82f13` | 3.1.0 + auditoría + FORGE etapa 1 (PR #4). CI de Mac en verde. **Base de 0.4.0.** |

## Baseline de regresión

Registrado en `docs/auditoria/` (pruebas y banco completo de la 3.1.0). Lo verificado en `forge/v4-dev` antes de empezar 0.4.0:

- **CI de macOS** (run 38208038359):
  - compila VST3, AU y Standalone universales;
  - pasan Ui, Stereo, Determinism, V3, Midi, Dna, Transform, Forge, Render, V21 y V2;
  - auval: `AU VALIDATION SUCCEEDED`;
  - compatibilidad: 38 de 38 proyectos (peor diferencia -91.3 dB).
- **Local** (Linux, clang): las mismas pruebas pasan; compatibilidad -106.7 dB.

## Inventario de motores (`engines_to_audit`)

| Motor del encargo | Estado en Nebula | Dónde |
| --- | --- | --- |
| SUBTRACTIVE | Sí: 2 osciladores (seno, triángulo, sierra, pulso, Super Saw, Sync, tabla), sub, unísono hasta 7, 2 filtros (LP/BP/HP/Notch, 12/24 dB, Limpio/Moog/Moog+), drive | `DspParts.h`, `SynthVoice.h` |
| WAVETABLE | Sí: 4 tablas incluidas + tabla de usuario (de audio o dibujada), posición = Forma | `Wavetable.h` |
| FM | Parcial: un solo par (Osc2 → fase de Osc1), con índice modulable desde la matriz. No hay grafo de operadores. | `SynthVoice::generate` |
| PHASE_MODULATION | Igual que FM (es modulación de fase de Osc1) | `NebOsc::next (pm)` |
| ADDITIVE | Sólo dentro de la resíntesis (parciales del modelo). **No existe como motor nativo editable.** | `ResynthModel.h` |
| FILTERED_NOISE | Sí: ruido con color + filtros + Env/matriz → Ruido | `SynthVoice::generate` |
| GRANULAR | Sí, sobre el sample (motor Sample). Depende del audio, así que no sirve para FORGE. | `SynthVoice::nextCloud` |
| SPECTRAL_RESYNTHESIS | Sí: RECREAR (tabla armónica por cuadros, parciales y ruido por bandas). Modo separado de FORGE. | `ResynthBuilder.h`, `ResynthModel.h` |

**Modulación:** 3 envolventes (con curva y loop), 2 LFO (sync), y una matriz de 8 fuentes × 11 destinos con cantidad y tiempo globales.

**Parámetros:** un solo APVTS con 149 parámetros base más la matriz. Los IDs son estables y todos son automatizables. El estado guarda el sample, el modelo de resíntesis, la tabla de usuario, el jardín, el ADN y la semilla de azar.

## Hallazgos de arquitectura

Además de los de `docs/auditoria/AUDITORIA-3.1.0.md`:

1. **Lo que falta para el contrato `PATCH_DNA`:**
   - Estructura que ya existe: `Forge::SynthPatch` (valores reales). Su topología es fija: la del motor Synth.
   - El "grafo FM" es un solo par de osciladores.
   - El "grafo de modulación" es la matriz.
   - No hay módulos que se puedan agregar o quitar. Para 0.4.0 el grafo de ADN se deriva de esa topología fija (qué módulos están activos y qué rutas tienen cantidad ≠ 0); no se inventa un motor modular nuevo.
2. **Brechas para las referencias del encargo:** no hay motor aditivo nativo ni operadores FM múltiples. Las campanas inarmónicas sólo se aproximan con FM de un par. Se deja para cuando 808 y lead estén validados (regla `implementation_rule` del encargo).
3. **Versión:** el encargo pide `target_version: 0.4.0`, pero el plugin publicado es 3.1.0. Bajar el número de versión del AU/VST3 puede hacer que los hosts conserven la versión en caché o la traten como más vieja. **Decisión:** "0.4.0" se usa como nombre del hito (rama, documentos e informes) y el número del plugin no se cambia sin la aprobación del usuario.
4. **Hilo de audio:** el banco de estabilidad de la 3.1.0 muestra bloques de hasta 12× tiempo real (ver auditoría). Es un riesgo para AT009 y queda pendiente de diagnóstico.

## Gate P0: BASELINE_VERIFIED

**PASSED**: fuente localizada, baseline ejecutado y registrado, inventario y riesgos documentados.
