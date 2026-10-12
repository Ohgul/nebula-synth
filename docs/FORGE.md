# FORGE: síntesis inversa (primera etapa)

FORGE busca **recetas del motor Synth de Nebula** que suenen como un audio de referencia. Una receta usa osciladores, sub, ruido, filtros, envolventes, LFOs y matriz de modulación. No guarda audio, ni tablas sacadas del sample, ni modelos de resíntesis. Por eso **suena igual aunque el sample ya no esté**.

RECREAR (resíntesis por cuadros) sigue existiendo como modo aparte. FORGE no lo usa.

## Cómo se usa

1. En **Cerebro**, carga un sample y pulsa **FORGE**.
2. **Buscar recetas.** Nebula analiza la sección elegida y entrega 8 candidatos.
   - Cada uno muestra su receta, la fidelidad (total y por color, envolvente, tono y golpe), la complejidad, la CPU y la semilla.
   - Con la misma semilla y el mismo sample salen los mismos candidatos.
3. **▶** escucha un candidato sin cambiar tu sonido. **Referencia** escucha el original afinado a su tecla.
4. **Usar:** el candidato pasa a ser tu sonido (motor Synth, sin efectos ni macros). Sus módulos se ven y se editan en **Editar** y **Modulación**, y se automatizan desde el DAW.
5. **Mutar:** genera 8 descendientes del candidato, medidos contra la referencia. La generación anterior queda guardada.

El porcentaje es **fidelidad de recreación**: cuánto se parece a la referencia. No es una nota de calidad musical. Se mide con las mismas métricas validadas del banco (`Source/Similarity.h`).

## Arquitectura

| Contrato del encargo | Código | Responsabilidad |
| --- | --- | --- |
| `ReferenceFeatures` | `Source/Forge/ReferenceFeatures.h` | Análisis de la referencia (ver la lista debajo) |
| `SynthPatch` | `Source/Forge/SynthPatch.h` | Valores reales de los parámetros de síntesis, metadatos (origen, referencia, arquitectura, semilla), guardado XML con versión y migración |
| `PatchRenderer` | `Source/Forge/PatchRenderer.h` | Render offline con las voces reales del plugin, sin procesador ni sample; determinista |
| `PatchCandidate` | `Source/Forge/ForgeSearch.h` | Patch, fidelidad por dimensión, complejidad, CPU, generación y padre |
| `CandidateSearcher` | `Forge::Searcher` | Estimación, refinamiento, exploración, selección |
| (trabajo en segundo plano) | `Source/Forge/ForgeJob.h` | Hilo propio, cancelación, resultado bajo candado |
| Interfaz | `Source/ForgePanel.h` | Lista de candidatos, escuchar, usar, mutar |
| `PatchGenome`, `ModulationGraph`, `DNAStudio` | — | **Pendientes (fase 4).** Hoy los genes son los parámetros de cada arquitectura y las rutas son la matriz del motor. |

**Qué mide `ReferenceFeatures`:**
- tono y su confianza;
- curva de tono: caída inicial (medida por cruces por cero en graves), glide final y vibrato (frecuencia, profundidad e inicio);
- envolvente: ataque, decaimiento a -20 dB, nivel sostenido, inicio y duración de la suelta;
- armónicos del cuerpo y ruido fuera de ellos;
- brillo a lo largo de la nota;
- golpe inicial;
- ancho estéreo;
- clase: tonal, bajo/808, percusivo, inarmónico, ruido o evolutivo;
- avisos de lo que no puede inferir con fiabilidad.

### La búsqueda

1. **Estimación.** Cada arquitectura traduce lo medido a parámetros reales. Por ejemplo, para "seno con caída de tono": Env2 → Tono con la caída medida, amplitud A/D/S de la curva de sonoridad y Env3 → Tono si hay glide.
   - Bajos y 808 (8 arquitecturas): seno con caída, seno saturado, seno con clic de ruido, triángulo, seno con sub, seno con FM, saturado con clic y pulso filtrado.
   - Leads y tonales (8): sierra con filtro, dos sierras desafinadas, pulso, sierra con pulso, Super Saw, triángulo con FM, Sync, y sierra con sub y ruido.
2. **Refinamiento.** Búsqueda por coordenadas en el espacio normalizado de los genes de cada arquitectura (pasos que se achican y rangos acotados alrededor de la estimación).
   - El orden de los genes sale de la semilla, así que la búsqueda es determinista.
   - Durante la búsqueda se compara a 32 kHz contra la referencia ya medida. Cuesta unos 40 ms por evaluación.
3. **Exploración.** Variaciones de los mejores, refinadas a su vez, hasta completar el pedido.
4. **Selección.** Se eliminan los casi idénticos. Con una fidelidad parecida (5 %) gana la receta más simple.
5. **Medición final.** Cada candidato elegido se mide a la frecuencia original con la comparación completa del banco, y se mide su CPU.

Un patch mudo no puede ganar: queda descartado antes de igualar el volumen. FORGE nunca usa efectos: un candidato no puede "esconder" una mala reconstrucción con reverb o chorus.

## Resultados (`NebulaTests Forge`, semilla 1, máquina de pruebas)

| Referencia | Mejor candidato | Fidelidad | Color | Envolvente | Tono | Tiempo de búsqueda |
| --- | --- | --- | --- | --- | --- | --- |
| 808 F1 con caída y clic (sintético) | Seno + clic de ruido, Env2 → Tono +8.4 st | 69 % | 3.1 dB | 2.9 dB | 0.2 ct | ~20 s |
| Lead A4: dos sierras, filtro que se abre, vibrato tardío (sintético) | Sierra + filtro, vibrato 5.6 Hz 28 ct | 81 % | 0.7 dB | 3.2 dB | 4.3 ct | ~14 s |

El error de envolvente del lead viene sobre todo del batido entre las dos sierras desafinadas del original. Una receta no lo reproduce con la misma fase, y la métrica (ventanas de 50 ms) lo cuenta como error.

## Criterios de aceptación (sección 10 del encargo) en esta etapa

| # | Criterio | Estado | Prueba |
| --- | --- | --- | --- |
| 1 | Genera audio sin el sample | Sí | `ForgeTest`: "suena igual sin el sample original" (-400 dB de diferencia) |
| 2 | Se guarda y se recupera | Sí | Proyecto guardado y abierto: idéntico; `SynthPatch` XML |
| 3 | Muestra los módulos que lo producen | Parcial | Lista de FORGE (receta legible) y Editar/Modulación. Falta el DNA Studio estructural. |
| 4 | Controles conectados al DSP | Sí | Son los parámetros reales del motor |
| 5 | Envolventes acordes al sonido | Sí, medido | Error de envolvente reportado por candidato |
| 6 | Modulaciones funcionan | Sí | Env2 → Tono, Env2 → Ruido y Env2 → FM medidos |
| 7 | La visualización responde a cambios reales | Parcial | Editar y Modulación muestran los valores; falta una vista estructural propia |
| 8 | Automatizable desde el DAW | Sí | Todos los genes son parámetros automatizables |
| 9 | Determinista | Sí | Misma semilla = mismos candidatos; render offline determinista |
| 10 | A/B | Sí | Candidato y referencia, sin cambiar el sonido |
| 11 | Sin clics ni valores no finitos | Sí | Pico, NaN y saltos medidos |
| 12 | No necesita efectos | Sí | Las recetas no incluyen efectos; al usarlas se apagan |

Además, para cada tipo principal de gen hay una prueba con efecto medible:

| Gen | Cambio medido |
| --- | --- |
| Tono (octava +1) | La frecuencia se duplica |
| Envolvente (decaimiento) | La caída es más del 50 % más larga |
| Filtro (corte) | +10 dB de energía por encima de 1.5 kHz |
| FM | Sube el brillo |
| Curva de tono (Env2 → Tono) | Sube la frecuencia del ataque |

## Demostración del encargo (sección 14)

| Paso | Estado |
| --- | --- |
| 1-5. Cargar un 808, analizar, 8 patches, escuchar y comparar, elegir | Sí, desde la interfaz (Cerebro → FORGE) y en pruebas |
| 6-7. Abrir SYNTH y ADN y ver osciladores y envolventes | Parcial: se ven en Editar y Modulación. No existe todavía una pestaña SYNTH/ADN estructural para FORGE (el editor de ADN actual es el de resíntesis). |
| 8-9. Cambiar tono y envolvente y escucharlo | Sí (perillas reales; medido en pruebas) |
| 10-13. Guardar, quitar el sample, comprobar que suena, volver a cargarlo | Sí |
| 14-15. Mutar y comparar descendiente con antecesor | Sí: Mutar en FORGE; cada hijo indica su padre y su fidelidad |

## Limitaciones de esta etapa

- **Sólo recetas monofónicas de una nota.** No hay acordes, mezclas, voces ni sonidos polifónicos.
- **Clases de sonido:** sólo bajos/808 y tonales (lead, pluck). Pads evolutivos, campanas, ruido y texturas usan por ahora las arquitecturas tonales (fase 5).
- **Caída de tono:** un 808 con más de 12 semitonos de caída no se alcanza (la matriz llega a ±12 st).
- **Fidelidad del 808:** 69 %. El color queda a 3 dB porque la caída medida (4.8 st) es menor que la real (12 st en 12 ms); el optimizador la sube a 8.4 st.
- **Mutar necesita el sample** para medir a los descendientes; sin él se rechaza.
- **La búsqueda tarda 15-25 s** en la máquina de pruebas (unas 320 evaluaciones).
- **No hay** DNA Studio estructural, historial navegable de generaciones en la interfaz, cruces entre candidatos ni CI de Windows.
