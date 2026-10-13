# NEBULA SYNTH v0.3.1 · NEBULA-CORE-050

Rama `feature/nebula-0.3.1-evolution`, creada sobre `feature/forge-2-fm-architectures` (PR #7). Ese PR incluye el #6 (banco FORGE 2.0, penalización de colas cortadas) y las arquitecturas FM L9–L12. `main` no se tocó.

## Versión (auditoría)

| Dónde | Antes | Ahora |
|---|---|---|
| `CMakeLists.txt` (fuente única) | 3.1.0 publicada; 0.4.0 en `feature/forge-2-*` (commit e13a716) | **0.3.1** |
| Interfaz | "v3.1.0" junto al logo | **"NEBULA SYNTH v0.3.1"**, siempre visible en el pie |
| Instalador | versión de CMake | versión de CMake |

- **Conflicto registrado:** el número baja de 3.1.0 (y de 0.4.0) a 0.3.1 porque así lo pide la especificación.
  - La identidad del plugin no cambia: código `Nbl1`, fabricante `Rmcd` y bundle id. Los proyectos guardados se siguen abriendo.
  - Algunos hosts guardan la versión del AU en caché y pueden avisar de que la instalada es "más vieja". Si Ableton o Logic lo muestran, basta con volver a escanear los plugins.
- La versión del **formato del estado** (`version = 3` en el proyecto) es otra cosa y no se tocó.
- El número no sube solo: para publicar hace falta tu autorización.

## Plataforma

- **CI sólo macOS:** se quitó el trabajo de Windows.
- **Sin VST3 nuevo:**
  - El CI compila AU + app.
  - pluginval y auval corren sobre el AU.
  - El instalador lleva AU + app; el VST3 sólo entra con `NEBULA_INCLUDE_VST3=1`.
  - La arquitectura de formatos en CMake (`AU VST3 Standalone`) se conserva, como pide la especificación.

## Lo implementado

### FORGE progresivo

- `Searcher::onCandidate` avisa dos veces por semilla:
  - cuando empieza a optimizarse (provisional);
  - cuando termina, con la fidelidad medida completa.
- `Searcher::objective` permite cambiar lo que se minimiza; DESCUBRIR lo usa para la distancia a la referencia.
- Sin objetivo, RECREAR busca la máxima fidelidad igual que antes.

### Cerebro vivo (DESCUBRIR) · `Source/Forge/Brain.h`, `Source/BrainView.h`

- **Generación 0:** FORGE en vivo. Las neuronas aparecen mientras se optimizan y laten sólo mientras ese trabajo existe.
- **Cada neurona:**
  - id estable y padres;
  - generación y origen;
  - patch real, que se puede escuchar, usar y editar;
  - similitud medida (no estimada);
  - novedad: distancia en la huella sonora al patch más cercano de la base, escalada por la separación típica entre vecinos de la base.
- **Evolucionar:**
  - Se generan 4× más hijos de los pedidos, por mutación de parámetros y estructura y por cruce por módulos.
  - Se respetan los candados.
  - Se descartan duplicados y se elige con diversidad estructural.
- **Distancia a la referencia (0–1):**
  - Cambia el objetivo: se busca una pérdida objetivo creciente, no la mínima.
  - Cambia la fuerza de la mutación.
  - Con 0 se vuelve a "lo más fiel".
- **Preferencias de la sesión:**
  - Lo que te gusta acerca el objetivo en la huella sonora y además entra como pareja de cruce. Queda registrado como padre.
  - Lo que no te gusta aleja el objetivo.
  - "Olvidar" las reinicia.
- **Deshacer y rehacer** por generación.
- **Determinista:** misma semilla y mismas acciones dan la misma red.
- **Cómo está hecho** (`Source/Forge/Explain.h`): cada línea sale de los valores reales del patch. Si un módulo no actúa, no aparece; si un módulo está encendido pero no se oye, lo dice.

### Operador FM 3 · motor

- **Parámetros nuevos** (versión de parámetro 2): `op3On`, `op3Ratio` (0.25–16), `op3Amount`, `op3Env` (Env 3 → índice) y `op3Target` (Osc 1 o Osc 2).
- **Sonido:** un seno que modula la fase del osc 1, o del osc 2 en cadena 3 → 2 → 1.
- **Compatibilidad:**
  - Apagado por defecto: los proyectos anteriores suenan igual. Lo verifica la prueba Determinism con los 38 proyectos v3 de referencia.
  - No consume números del azar de la voz.
- **Editor:** sección "FM OP 3" en CREAR → Editar.
- **Integración:**
  - módulo `Op3` en el grafo, los candados, la mutación estructural y la explicación;
  - arquitectura de búsqueda `L13` (campana de 3 operadores);
  - el 60 % de las campanas de la base nueva lo usan.
- **Base de patches regenerada** (`dsp2-…`, 2002 patches).

## Medido

### FORGE (prueba `Forge`)

- 808: 73 % y lead: 79 %. Con la versión de la rama 0.4.0 eran 78 % y 82 %.
- La bajada coincide con dos cambios de este punto de partida:
  - el castigo a las colas cortadas, que trajo el PR #6;
  - la base de patches nueva.
- Los umbrales de la prueba (60 %) siguen pasando. Hay que investigarlo.

### Recuperación interna (`Forge recuperacion 3 2`)

Son 21 patches ocultos de la base nueva, 3 por categoría, buscados con 2 semillas.

- Fidelidad media **71.9 %**.
- Variación entre semillas: 1.7 puntos.
- Distancia de parámetros 0.137, frente a 0.177 al azar.
- 18.6 s por búsqueda.

Por categoría (mejor semilla):

| Categoría | Fidelidad de los 3 patches |
|---|---|
| BASS | 83 / 74 / 82 % |
| 808 | 88 / 90 / 81 % |
| LEAD | 63 / 74 / 79 % |
| PLUCK | 86 / 69 / 78 % |
| PAD | 57 / 77 / 75 % |
| FM_BELL | 38 / 52 / 77 % |
| NOISE_TEXTURE | 61 / 56 / 70 % |

- No se compara directo con el 70.7 % anterior: la base y su partición de prueba cambiaron.
- **La meta de 0.85 no se alcanzó.**

### Cerebro (prueba `Brain`)

- La generación 0 crece en 11 pasos visibles en ~25 s.
- La similitud mostrada coincide con una medida independiente.
- Con distancia "lejos", el parecido baja de 62 % a 12 % y el cambio de parámetros sube de 0.007 a 0.023.
- Con "me gusta", la distancia de la generación a la neurona elegida baja de 2.83 a 1.79.

## Pendiente o no verificado

Ver `docs/LIMITACIONES.md`.

- **Escucha y prueba en Ableton:** pendiente de tu prueba.
- **Fidelidad media 0.85:** es la meta, no un resultado. La última medida con 14 patches ocultos fue 0.707 (antes del operador 3). Hay que repetirla con más patches.
- **Síntesis aditiva editable:** todavía no; existe dibujar la onda (tabla).
- **El editor de ADN no incluye los efectos dentro de la receta:** los efectos se editan en CREAR → Efectos, pero no viajan con la receta de FORGE ni del cerebro.
- **Las preferencias son de la sesión:** no se guardan con el proyecto.
