# Banco de pruebas de Nebula (Paso 0)

Referencia del motor actual: `baseline.json` (tras las correcciones de NaN estéreo y azar
reproducible). La referencia anterior, del motor v3.0.0, está en `baseline-v3.0-antes.json`.
Diferencias entre ambas: `COMPARACION-correcciones.md`.
No forma parte del plugin: solo se compila con `-DNEBULA_BUILD_TESTS=ON`.

## Ejecutar

```
cmake -B build -DNEBULA_BUILD_TESTS=ON && cmake --build build --target NebulaBenchTest
NebulaBenchTest <salida> [--quick] [--only 0,1,2,...] [--fixtures tests/fixtures/v3]
                 [--semilla N] [--regenerar-referencia]
```

Secciones: 0 autocomprobacion, 1 validacion de metricas, 2 fidelidad de RECREAR,
3 residuo de transitorio, 4 CPU, 5 latencia, 6 estabilidad, 7 clics, 8 compatibilidad.
Con `--fixtures tests/fixtures/v3` la seccion 8 hace una prueba nula (<= -90 dB) de los 38
proyectos v3 contra su audio de referencia (`fixtures/v3/renders/*.flac`). Los proyectos se abren
tal cual (modo legacy) y solo para medir se fuerza la semilla del banco (0x4E42).
`--regenerar-referencia` vuelve a grabar ese audio (nunca reescribe los .nebstate existentes).
Todo el banco usa la semilla 0x4E42: dos corridas dan los mismos valores (salvo tiempos de CPU).

Otras pruebas: `NebulaStereoTest` (regresion de NaN estereo) y
`NebulaDeterminismTest tests/fixtures/v3` (azar reproducible y modo legacy).

## Archivos

- `Corpus.h`: 20 sonidos generados por codigo con semilla fija (tonales, inarmonicos,
  percusivos, pads de 4-12 s con evolucion, transitorios de duracion conocida).
- `Metrics.h`: metricas con unidad (LU, dB, cents, ms, correlacion). Ningun porcentaje.
- `Transient.h`: estudio del residuo de transitorio (con y sin fase medida).
- `baseline-v3.json` / `baseline-v3.md`: resultados de referencia del motor actual.
- `../fixtures/v3/`: 38 proyectos v3 (34 presets + 4 recreados) y sus rasgos de referencia.

## Metricas validadas (ver seccion 1)

Usar como criterio: tono (nota y curva, cents), color (dB, ±250 ms), envolvente (dB),
energia temprana (dB), evolucion (correlacion del centroide), estereo (correlacion por banda).
No usar como criterio: mel cuadro a cuadro de 10 ms (lo mueven fases y semillas de ruido)
y tiempo de ataque 10-50 % (inestable).
