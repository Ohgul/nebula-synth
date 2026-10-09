# Nebula: sintetizador con modo Jardín

Sintetizador polifónico (VST3 / AU) para Ableton en Mac, con un modo para *cultivar* sonidos por mutación.

## Cómo se usa

**Pestaña JARDÍN**
- En el centro está la **semilla**, que es el sonido actual. Alrededor hay **8 brotes**, que son variaciones de la semilla. Cuanto más lejos está un brote, más distinto suena.
- **Clic** en un brote: suena y se vuelve el sonido activo.
- **Doble clic** en un brote: se planta como nueva semilla y germinan 8 variaciones nuevas.
- **Clic en la semilla**: regresas a ella.
- **Mutación**: cuánto cambian los brotes en la próxima tanda.
- **Otra tanda**: nuevas variaciones de la misma semilla.
- **Semilla al azar**: empieza de cero con un sonido aleatorio.
- **Atrás**: regresa a la generación anterior.
- **Fijar envolvente**: las mutaciones no tocan el ataque, el decaimiento, el sostenido ni la liberación.

La forma y el color de cada brote salen de su sonido:
- color: brillo del filtro, drive, FM y ruido
- número de pétalos: formas de onda
- picos: resonancia
- núcleo: sub-oscilador
- tamaño: sostenido

**Arrastrar un audio** (columna SAMPLE del Jardín)

Arrastra un WAV, AIFF, MP3, FLAC u OGG al Jardín, o haz clic en el recuadro para buscarlo. Nebula detecta su nota y te deja elegir:
- **Imitar con el sinte**: el sinte prueba unos mil patches durante 5–15 segundos y se queda con el que más se parece. Te muestra un % de parecido, y ese patch se vuelve la semilla. Funciona mejor con sonidos de sinte, bajos, pads, plucks y tonos sostenidos. Con voces, instrumentos acústicos o percusión el resultado es una aproximación.
- **Usar el sample**: tocas tu audio como instrumento, afinado según el teclado. Los brotes cambian inicio, afinación, reversa, loop, textura granular, filtro y envolventes. Con textura alta el sonido se estira y se congela, como un pad.

Ableton guarda la ruta del sample con el proyecto. Si mueves o borras el archivo, tendrás que volver a cargarlo.

**Pestaña EDITAR**: todos los knobs. Doble clic en un knob lo regresa a su valor por defecto.

Todo se guarda con tu proyecto de Ableton, incluido el jardín.

## Motor de sonido
Dos motores: **Sinte** (2 osciladores seno/triángulo/sierra/cuadrada con anti-aliasing, FM) y **Sample** (reproducción con afinación, reversa, loop y granular). En ambos: sub-oscilador, ruido, filtro de 3 tipos con resonancia y drive, 2 envolventes ADSR, LFO a filtro y a tono, 12 voces y 8 presets.

## Compilar (lo hace GitHub solo)
Cada vez que subes cambios, GitHub compila el plugin en una Mac en la nube y genera `Nebula-Instalador-x.y.z.pkg`. Lo encuentras en la pestaña **Actions**: abre la ejecución más reciente y baja hasta **Artifacts**.

Si cambias algo y quieres que Ableton lo note, sube la versión en `CMakeLists.txt` (`project(NebulaSynth VERSION 1.0.1)`).

### Compilar en tu Mac (opcional)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Licencia de JUCE
JUCE 8 es gratis para uso personal bajo su licencia AGPLv3 o la licencia "Starter" (proyectos con ingresos menores a 50k USD). Si algún día lo vendes, revisa juce.com/get-juce.
