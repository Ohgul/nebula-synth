# Correcciones críticas previas al Paso 1: antes y después

Fecha: 10 oct 2026. Máquina de medición: Xeon 2.8 GHz, 2 núcleos (nube).

- Antes: `baseline-v3.0-antes.json` (motor v3.0.0, commit aad1831).
- Después: `baseline.json` (commits 40808f3 y 302ab76).

## Corrección 1: NaN con samples estéreo anchos

**Causa (confirmada con AddressSanitizer).**

- `ResynthPlayer::next` (`Source/ResynthModel.h`, línea 318) recibe un ancho que va de 0 a 2. Ese valor es: ancho del modelo × 2 × "Resintesis Ancho".
- El retardo Haas se calculaba como `ancho × (largo del búfer - 2)`, sin límite.
- Con un ancho mayor que 1, el índice salía del búfer (heap-buffer-overflow al leer). La basura leída entraba como NaN en la salida.
- Ese NaN quedaba atrapado en el bloqueo de DC y en la reverb, y el plugin quedaba mudo hasta recargarlo.

**Arreglo.** El retardo se satura en su máximo de diseño (20 ms). Con un ancho de 1 o menos, el cálculo es idéntico al de antes.

**Prueba de regresión:** `tests/StereoTest.cpp`.

- Samples: mono, estéreo moderado, estéreo ancho, fuera de fase, solo izquierda y ruido estéreo independiente.
- Ancho de resíntesis: 0, 0.5 y 1. Calidad: 1x, 2x y 4x.
- También se prueba con efectos y con el proyecto recargado.

| | Antes | Después |
| --- | --- | --- |
| StereoTest | 17 fallos de 228 | 0 fallos de 228 |
| AddressSanitizer | heap-buffer-overflow en ResynthModel.h:318 | sin errores |
| pad_estereo_4s, muestras NaN | 263 878 | 0 |
| pad_estereo_4s, color | (salida NaN) | 2.62 dB |
| pad_estereo_4s, % de la interfaz | 12 en una corrida, 68 en otra | 68 estable |
| Fuzz de 200 s, resíntesis de un pad estéreo ancho | no se medía | 0 NaN/Inf |

## Corrección 2: azar reproducible por nota

**Cómo funciona.** Cada nota reinicia el generador aleatorio de su voz con una semilla. La semilla sale de tres datos:

- la semilla de la instancia, que se guarda en el proyecto;
- el número de nota-on desde el último prepare/reset;
- la nota.

`reset()` deja las voces, los efectos y el oversampling como recién preparados.

**Compatibilidad.**

- Los proyectos guardados antes de esta versión no tienen `randomMode`. Se abren en modo **legacy**, con exactamente el código de antes (azar libre), y siguen en legacy al volver a guardarlos.
- Las instancias nuevas usan el modo reproducible y cada una recibe su propia semilla, así que dos Nebula iguales no suenan clonadas.
- No hay parámetros nuevos: los IDs del plugin y de los parámetros no cambian.

| | Antes | Después |
| --- | --- | --- |
| Dos renders del mismo modelo (sonidos con ruido o parciales) | de −23.3 a +4.4 dB de diferencia | idénticos bit a bit (20 de 20 sonidos) |
| Misma configuración en dos procesadores (34 presets, sample, resíntesis; 1x/2x/4x) | distintos | 48 de 48 idénticos |
| Mismo procesador tras `reset()` | no se repetía | 38 de 38 idénticos |
| Dos golpes iguales seguidos | distintos | siguen distintos |
| Proyecto v3 abierto (modo legacy) | azar libre | azar libre (3.4 dB entre dos cargas, como antes) |
| Compatibilidad con los 38 proyectos v3 | 34–35 de 38 contra sí mismo, por tolerancia | 38 de 38 en prueba nula, peor −106.7 dB (criterio ≤ −90 dB) |
| Dos corridas completas del banco (secciones 0, 1, 2, 3, 5 y 7) | no repetibles | 3737 valores idénticos |
| % de la interfaz para el mismo sample | cambiaba entre corridas | estable |

## Hallazgo extra durante la validación: lectura fuera de rango en `frameAt`

AddressSanitizer encontró, al correr la prueba de reproducibilidad, un segundo error de memoria que ya existía:

- `ResynthModel::frameAt` comparaba el tiempo en `double` y buscaba en `float`.
- Justo antes del último frame, el redondeo hacía que `upper_bound` devolviera el final del vector, y se leía un elemento fuera de él.
- Esa basura puede romper la reproducibilidad, así que se corrigió dentro de la corrección 2: ahora se compara en `float` y un tiempo que no sea un número da el frame 0.

En las 3737 mediciones del banco no cambió ningún valor, y las dos pruebas nuevas pasan limpias con AddressSanitizer.

## Lo que no cambió

- **CPU.** Las diferencias caen dentro del ruido de medición del servidor. Resintesis con 8 voces: 1x 7.0–8.9 %, 2x 14.2–17.9 %, 4x 28.0–30.5 % (antes: 7.5–8.9 %, 13.8–16.6 % y 28.2–32.1 %).
- **Latencia:** sigue reportando 0 y midiendo 2 y 3 muestras. Queda para más adelante.
- **Clics:** el robo de voz en resíntesis sigue en +19.1 dB. Queda para más adelante.
- **Fidelidad de RECREAR.** Mismos valores salvo la parte aleatoria de los sonidos con ruido (ver la tabla siguiente) y el pad estéreo, que ahora funciona.

## Cuánto varían las métricas con la semilla

Rango en 6 semillas del banco (1, 2, 3, 4, 5 y 0x4E42). Los sonidos que no aparecen dan el mismo valor con cualquier semilla.

| Sonido | Color dB | Envolvente dB | Evolución | Energía temprana dB |
| --- | --- | --- | --- | --- |
| pluck_sierra | 0.26–0.33 | 0.19 | 0.996–0.999 | −12.0 a −10.8 |
| campana | 15.09–15.21 | 0.51–0.60 | −0.27 a −0.08 | −13.7 a −11.7 |
| bombo_808 | 4.86–5.73 | 0.33–0.49 | 0.957–0.974 | −12.8 a −11.2 |
| caja | 1.66–1.85 | 0.36–0.54 | 0.995–0.997 | −10.8 a −8.4 |
| golpe_ruido | 1.30–2.87 | 0.44–2.14 | 0.13–0.42 | −11.0 a −10.3 |
| hihat | 5.94–6.40 | 1.30–1.71 | 0.22–0.79 | −7.5 a −6.8 |

Reglas para comparar versiones a partir de ahora:

1. Comparar siempre con la misma semilla (0x4E42), porque así la diferencia se debe solo al cambio de código.
2. En sonidos con ruido, una mejora solo cuenta si supera el rango de esta tabla.

## Pruebas después de cada corrección

| Prueba | Tras corrección 1 | Tras corrección 2 |
| --- | --- | --- |
| Render | OK | OK |
| Imitate | OK | OK |
| V2 | OK | OK |
| V21 | OK | OK |
| V3 | OK | OK |
| UI | OK | OK |
| Stereo (nueva) | OK, 228 de 228 | OK, 228 de 228 |
| Determinism (nueva) | — | OK, 10 de 10 |

La prueba Render falló 1 de 4 veces antes de las correcciones. La causa es que las mutaciones del Cerebro (Evolucionar) usan el reloj como semilla, y eso no se tocó.
