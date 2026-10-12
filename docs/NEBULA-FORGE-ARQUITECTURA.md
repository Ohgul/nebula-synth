# NEBULA FORGE — Arquitectura propuesta de síntesis inversa
Estado: propuesta de ingeniería, **no implementado ni verificado**. Rama de diseño, no modificar presets o audio existente antes de baseline. Fecha 2026-10-11.

## Objetivo de producto
Dado un audio de referencia R, producir una colección de patches P_i **ejecutables enteramente por los módulos nativos de NEBULA** (sin cargar ni reproducir el audio original), cada uno acompañado de explicaciones, puntuaciones comparables, auditoría de representabilidad y árbol de descendientes. Tras elegir un patch, abrir cada parámetro real que lo fabrica, mutar/cambiar módulos y renderizar el resultado de forma reproducible. No confundir esto con guardar fotogramas de audio o colorear mediante efectos.

## Auditoría preliminar de main (revisar 3.1.0 antes de programar)
- `Source/ResynthBuilder.h`: llamada `SoundProfiler::analyse(...,&rd,64,30.0)`; contradice descripción 3.1.0 de 192 cuadros/60 s. Determinar rama/commit efectivo.
- `Source/ResynthModel.h`: modelo temporal de parciales, tablas, ruido y pitch; **es resíntesis**, no necesariamente recuperación de patch paramétrico.
- `Source/Imitator.h`: existe `OfflineSynth` y `setGenome`; posibilidad de evaluar candidatos. Inspeccionar límites y duplicación de parámetros.
- `Source/GenomeOps.h`: Genome representa valores normalizados; para ADN real falta topología de módulos, rutas de modulación, curvas y significados legibles.
- `Source/Parameters.h`: hay Engine::Synth, Sample, Resynth y módulos oscilador, filtros, envolventes y efectos. El modo Synth debe poder renderizar sonido sin sample.
- `Source/PluginProcessor.cpp`: coexistencia de `startImitation()` y de RECREAR; separar flujo de resíntesis fiel y búsqueda de patch.
- Version 3.1.0: no está certificada por estos archivos de main. No prometer implementación hasta encontrar commit fuente y registrar pruebas.

## Definiciones y contratos
1. `ReferenceFeatures`: RMS/LUFS; f0(t) + confianza, transitorios, espectrograma multirresolución, envolvente de amplitud, estéreo, inarmonicidad, ruido condicionado, evolución de sustain/release. Todos con sample rate, tempo y selección del usuario.
2. `SynthPatch`: topología/estructuras tipadas para osciladores, filtros, operadores FM, sub, ruido, envelopes, modulación, ruteo, voces; `ParameterLayout` hace mapping reversible a APVTS. Cambiar presets debe preservar parámetros reconocidos y tener migraciones explícitas.
3. `PatchCandidate`: patch + rendimiento offline + scores desglosados + seed + metadata de motor / versión + costo CPU + representación legible.
4. `CandidateSearcher`: generar candidates por clase de sonido, estimación inicial, búsqueda multietapa coarse-to-fine, refinamiento local, exploración genética, Pareto de similitud/CPU/interpretabilidad; cancelar y recuperar progreso sin bloquear audio/UI.
5. `PatchGenome`: genes tipados por módulos y curvas (no solo valores planos); mutación semántica (tiempo, espectro, FM, transitorios), candados, cruce compatible, rollback y semilla.
6. `DNAStudio`: inspector bidireccional con señalización de cuál oscilador/env/modulación explica cada propiedad audible. A/B del patch sintetizado y referencia a igual loudness, con opción de conmutar a una nueva síntesis.

## Modos de trabajo separados
- **RECREAR (resíntesis)**: mantener temporal model como aproximador rápido / herramienta de análisis, etiquetar cuando depende de audio/transitorio.
- **FORGE (síntesis inversa)**: solo engine Synth; buscar receta editable; la exportación debe sobrevivir al borrar el sample del proyecto. Calcular % comparable usando renders con la misma nota, dinámica y sample rate.
- **DISCOVER (exploración)**: 8-16 candidatos diversos; una ronda de refinamiento conserva referencia y favoritos; sugiere vecindarios genéticos.
- **COSMOS/ABISMO**: modificar parámetros *del patch*, topología cuando se permita, y automatización/modulación; efectos solo como capas opcionales, deshabilitadas para medición de ADN.
- **RENDER**: reproducibilidad semilla/módulos; audio thread sin allocations ni bloqueo; CPU límites; clicks y NaN-tests.

## Arquitectura de pérdida perceptual
`loss = w1*logMagSTFTmulti + w2*f0Contour + w3*onsetEnvelope + w4*phaseAwareLowFreq + w5*inharmonicBands + w6*temporalEvolution + w7*stereo`; normalizar métricas y reportar independientes; calibrar con escucha ABX, penalizar plugins que ganen al sobreajustar volumen y efectos. Priorizar ataque/tono para 808, evolución y modulación para pads, parciales para campanas. Comparar a loudness pareado. No publicar un "porcentaje real" como verdad física.

## Fases con puertas verificables
**0. Fuente única / benchmark:** localizar commit 3.1.0 real, registrar versión, test 808, pad 15s, bell, lead, ruido, test de Poly/Mono, AU/VST3 y Windows; guardar audio A/B, seeds y mediciones. No sobrescribir 3.0.
**1. Patch serializable y render headless:** `SynthPatch` + APVTS adapter, offline render exacto y tests de compatibilidad. Asegurar exportaciones sin referencia.
**2. Extracción y estimación inicial:** generador por clases de sonido: 808 seno+envolventes y transitorio; pads tablas+filtros/LFO; campana FM/parciales; lead osciladores+filtro. Baseline contra Init presets.
**3. Searcher multiobjetivo:** búsqueda asincrónica, seeds, candidatos diversos, guardar reproducibilidad, A/B con loudness; pruebas que no gane un patch silencioso o solo una cola.
**4. DNA Studio:** inspección estructural, conexiones/moduladores gráficas, candados de módulos y genes tipados, mutación musical, historial.
**5. Cosmos/Abismo estructural:** rutas realmente sintéticas; conservar efecto independiente, comparar transformaciones con FX bypass, controlar aliasing/CPU.
**6. Productización:** interfaz minimalista, Mac AU/VST3, Windows VST3, instaladores beta, evaluación músicos, errores y análisis de rendimiento.

## Criterios de aceptación de la primera beta FORGE
- Un 808 y un lead reconstruidos generan audio con `sampleSource` desconectado y muestran pitch/amp envelope y ruta de síntesis editables.
- Repetir misma seed da el mismo render bajo mismo SR/engine y versión.
- Al menos 8 candidatos distintos, ordenados por similitud y diversidad; sus métricas se corresponden con cambios audibles de control.
- Ninguna tarea pesada ocurre en audio thread; cancelación no produce cuelgues.
- Historial de presets 3.0 (38 proyectos) mantiene salida dentro del umbral existente, o se documenta explícitamente migración y ruptura.
- CI real en Windows + Mac con tests unitarios y audio; AU auval/Windows smoke-test.
- Las limitaciones de representabilidad (voces, mezclas, ambience) se muestran claramente.

## Antipatrones prohibidos
- Etiquetar frame-by-frame resynthesis como "ADN editable".
- Convertir un audio solo en wavetables congeladas y afirmar que descubrió su receta.
- Obtener mejor parecido usando chorus/reverb/distorsión en vez de fuentes.
- Bloquear el hilo de audio/UI con búsqueda.
- Crear nuevas interfaces sin camino reproducible del parámetro al sonido.
- Cambiar `main` masivamente sin benchmark ni migración.
