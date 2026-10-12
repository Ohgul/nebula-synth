# Nebula 3.1: funciones

Esta guía describe lo que cambió en la versión 3.1 y cómo se usa. Lo que ya existía en la 3.0 está en el `README.md`.

## 1. Recrear (resíntesis)

Arrastra un audio al Cerebro y pulsa **RECREAR**. Nebula analiza la sección elegida y la reconstruye con estas capas:

- una tabla armónica que cambia en el tiempo;
- parciales inarmónicos;
- ruido en 24 bandas;
- la curva de tono;
- el golpe original del ataque, en los bajos.

### Bajos 808

- **Tono:** se detecta hasta 25 Hz. Los glides (por ejemplo, de G1 a C1) se siguen sin errores de octava.
- **Ruido:** el sostenido sale limpio; el ruido que aparecía venía de la ventana de análisis.
- **Ataque:** los primeros 40 ms son el ataque original, con la fase de la fundamental alineada para que entre sin corte.

### Pads largos

- Se analizan hasta 60 s; antes el análisis se cortaba a los 4 s.
- La cantidad de cuadros se adapta al sonido (hasta 192).
- Se detectan el ataque, el cuerpo y la cola.
- Mientras sostienes la nota, el cuerpo se recorre de ida y vuelta, así el timbre sigue cambiando.
- Al soltar suena la **cola real** del sonido.

### % de parecido

Es honesto: se calcula con las mismas métricas que valida el banco de pruebas (`Source/Similarity.h`). Muestra:

- **color**: distancia log-mel de 32 bandas, en dB;
- **envolvente**: error RMS de la curva de sonoridad, en dB;
- **tono**: diferencia de la nota y de la curva de tono, en cents.

Escala de referencia:

| Error | Parecido |
| --- | --- |
| 0.45 dB (diferencia inaudible) | ~93 % |
| 2 dB | 72 % |
| 6 dB | 37 % |

El total es una media geométrica ponderada: si un aspecto sale muy mal, baja el total.

Antes, la escala daba 100 % con 2 dB de distancia. Los números de ahora son más bajos porque reflejan lo que de verdad se oye.

## 2. Editor de ADN

Ábrelo desde **Cerebro → ADN** o desde **Editar → Editar ADN**. Necesita un sonido recreado.

Cada perilla es un parámetro real del plugin, así que se puede automatizar en Ableton. Cada gen tiene una prueba que mide su efecto (`tests/DnaTest.cpp`).

| Grupo | Gen | Qué hace en el motor |
| --- | --- | --- |
| A Identidad tonal | Tono | Escala la curva de tono original (vibrato, glides): de 0 (plano) a 2 (exagerado) |
| | Formantes | Desplaza la envolvente espectral ±12 st sin cambiar la nota |
| B Estructura armónica | Brillo | Inclina los armónicos ±12 dB/oct |
| | Brillo en el tiempo | Cambia esa inclinación del ataque al final |
| | Pares/Impares | Quita (hueco) o refuerza los armónicos pares |
| | Armónicos | Cuántos armónicos quedan, de casi un seno a todos |
| C Textura | Capas armónica, parciales y ruido | Nivel de cada capa |
| | Color del ruido | Inclinación de ±12 dB en las 24 bandas |
| | Aspereza | Modulación de amplitud rápida (~37 Hz, irregular) |
| | Densidad | Dos copias desafinadas de la tabla (coro interno) |
| D Tiempo | Ataque | Estira o acorta el ataque del modelo (×0.25 a ×4) |
| | Golpe | Nivel del transitorio original |
| | Velocidad | Velocidad con la que se recorre el sonido |
| | Cola | Estira o acorta la cola real al soltar |
| E Movimiento | Evolución | 0 = timbre fijo (el promedio), 1 = como el original, 2 = cambios exagerados |
| | Congelar | Al sostener, el timbre se queda al principio del cuerpo |
| | Inarmonía | Estira las frecuencias de los parciales (sólo si el sonido tiene parciales; si no, la perilla aparece apagada) |
| | Ancho | Ancho estéreo |

Hay dos tipos de genes:

- **Estructurales** (Brillo, Brillo en el tiempo, Pares/Impares, Armónicos, Formantes, Evolución y Cosmos/Abismo): reconstruyen la tabla del modelo. La nota que ya está sonando cambia a la tabla nueva con un fundido de 30 ms, sin clic.
- **En tiempo real** (los demás): actúan en cada bloque de audio.

El **modelo base** (lo que salió de RECREAR) nunca se modifica. El proyecto guarda el modelo base y los genes.

**Mutar** cambia al azar los genes que no tienen candado. El resultado es reproducible: la misma semilla sobre el mismo ADN da siempre el mismo resultado.

- La semilla siguiente aparece en el cuadro *Semilla*. Puedes escribir otra.
- **Deshacer** vuelve al ADN anterior a la mutación.
- **Candado:** el gen no cambia al mutar, y la mutación del resto de los genes es la misma que sin candado.
- **Copiar / Pegar:** el ADN se copia como texto JSON, para pasarlo a otra instancia de Nebula.
- **Guardar A / A / Guardar B / B:** comparación A/B. Las ranuras se guardan con el proyecto.
- **Original:** devuelve los genes sin candado a su valor por defecto.

## 3. Cosmos y Abismo

Se controlan con la barra **ABISMO ← ORIGINAL → COSMOS**, que aparece en Cerebro → Transformar y en Efectos → Cosmos/Abismo.

En el centro suena el original exacto, bit a bit. Doble clic en la barra la devuelve al centro.

### Cosmos (derecha): atmosférico y espacial

- **En el modelo:**
  - expande el espectro con ecos de octava y de quinta;
  - difumina los armónicos;
  - abre las fases.
- **Al recorrer el modelo:**
  - el cuerpo avanza más lento (el ataque no cambia);
  - el timbre deriva despacio;
  - se suma una capa fantasma una octava arriba, abierta en estéreo.
- **En cualquier motor:**
  - deriva de afinación de unos 12 cents;
  - difusión estéreo modulada que sigue sonando bien en mono;
  - el espectro "respira";
  - una capa de aire leve.

### Abismo (izquierda): experimental y destructivo

- **En el modelo:**
  - fragmenta el espectro (bandas que se apagan, con un patrón que cambia a lo largo de la nota);
  - pliega los armónicos;
  - deforma sus niveles.
- **Al recorrer el modelo:** salta a otros puntos del sonido, a veces hacia atrás.
- **En cualquier motor:**
  - caos de afinación por pasos;
  - modulación en anillo inarmónica;
  - distorsión asimétrica con pliegue;
  - repeticiones y reversa de trozos recientes;
  - reducción de bits y de frecuencia en el último tramo.

### Identidad y Salida

- **Identidad** mezcla el original: con 100 % no hay ningún cambio.
- **Salida** ajusta el nivel final, de -24 a +6 dB.

### Protección

- Control automático de nivel.
- Limitador suave: la voz nunca pasa de 1.0.
- Bloqueo de continua.
- Si aparece un NaN o un infinito, la voz se reinicia en silencio.
- Además, el plugin tiene una guarda global: un bloque con valores no finitos sale en silencio y los efectos se reinician.

### Reproducibilidad y costo

- El azar sale de la semilla de cada nota, así que el mismo proyecto da el mismo render.
- **CPU:** 8 voces de resíntesis usan ~14 % de un núcleo sin transformar y ~27 % con Cosmos o Abismo (medido en la máquina de pruebas, que es lenta).

## 4. MIDI y voces

- **RECREAR deja el sonido en modo Poli y sin glide.** Antes heredaba el modo Mono del preset anterior, y por eso sonaba una sola nota.
- **Robar una voz no hace clic:** la voz robada se apaga en ~3 ms.
- **Pedal de sustain**, pitch bend y modo mono con retorno a la nota anterior, verificados en `tests/MidiTest.cpp`.
- **Latencia:** se reporta al host según el oversampling.

## 5. Interfaz

- Estilo grafito minimalista:
  - paneles planos con borde de 1 px;
  - títulos pequeños y espaciados;
  - un acento frío (Cosmos, acciones) y uno cálido (Abismo, avisos);
  - perillas de indicador fino.
- La red neuronal del fondo se ve completa sólo en el Cerebro y queda casi apagada en las demás pestañas.
- La "locura" sólo tiñe los colores.

## 6. Compatibilidad

- Los proyectos y presets de la 3.0 suenan igual. Se comprueba con 38 proyectos de referencia, con una diferencia de -106.7 dB (el criterio es ≤ -90 dB).
- Los parámetros nuevos se agregaron al final, con IDs estables.
- Los proyectos viejos sin semilla conservan su azar anterior (modo heredado).
