# Nebula 3.1: limitaciones conocidas

Esta lista es explícita a propósito. Lo que aparece aquí **no** está resuelto, o está resuelto pero sin verificar en las condiciones reales.

## Verificación

- **Nadie lo ha escuchado ni lo ha probado en Ableton.** Todo se verificó con pruebas automáticas:
  - compilación en macOS, con VST3, AU y app universales;
  - auval;
  - las pruebas de `tests/`.
  - El desarrollo se hizo en Linux, sin Ableton.
  - Para la prueba manual, sigue `docs/PRUEBA-ABLETON.md`.
- **No hay firma ni notarización de Apple.** El instalador está firmado *ad hoc* y macOS muestra una advertencia la primera vez. Hace falta una cuenta de Apple Developer.
- **pluginval** corre en el CI desde la rama `feature/nebula-0.4.0` (macOS: VST3 y AU; Windows: VST3, nivel 5), pero todavía como paso informativo. Hasta ver un resultado aprobado, no está verificado.
- **Windows:** el trabajo de CI compila y prueba con MSVC desde la rama `feature/nebula-0.4.0`. Nadie lo probó en un DAW de Windows.

## Resíntesis

- **Campanas y sonidos inarmónicos** dan alrededor de 50 % de parecido, y el color queda lejos (6 dB en la prueba). La nota raíz de una campana se detecta con error: una campana de 330 Hz se toma como E4 -43 ct.
- **808:** el sostenido y el tono están bien, pero los agudos del 808 recreado quedan unos 9 dB por debajo del original. Un bombo 808 sintético da 56 %.
- **Envolvente de los pads:** en el banco queda un error de 3 a 5 dB en pads largos (la forma exacta de la cola y el fundido final no siempre coinciden). En el % de parecido de la 3.1.0 se veía peor por un error de medición (la nota nunca se soltaba), corregido en la rama `forge/v4-dev`.
- **El estéreo de la resíntesis** se aproxima con un ancho y un retardo. No reconstruye la imagen estéreo exacta del original.
- **El % de parecido mide hasta 8 s** del sample.
  - Su ponderación de sonoridad usa los coeficientes oficiales a 48 kHz; con samples a 44.1 kHz hay una pequeña desviación.
  - Es una medida objetiva, no una prueba de escucha.

## ADN

- **Inarmonía** sólo actúa sobre los parciales. En un sonido sólo armónico la perilla aparece apagada.
- **Genes estructurales:** Brillo, Brillo en el tiempo, Pares/Impares, Armónicos, Formantes, Evolución y la cantidad de Cosmos/Abismo reconstruyen la tabla en el hilo de mensajes, unas 20 veces por segundo.
  - En tiempo real se oye bien.
  - En un render offline más rápido que el tiempo real (Freeze, Exportar), la automatización de esos genes se aplica a esa frecuencia, así que el render puede no coincidir exactamente con la reproducción.
  - Los genes en tiempo real no tienen este problema.
- **La mutación sólo cambia los genes del editor de ADN**, no el resto del patch (filtros, efectos). Para mutar el patch completo está el Cerebro.

## Cosmos y Abismo

- **Las partes estructurales sólo se aplican a sonidos recreados:**
  - expansión, fragmentación y pliegue del espectro;
  - recorrido lento o saltos dentro del modelo;
  - capa fantasma.
- **Con osciladores o con el motor de sample** actúa sólo la parte de audio: afinación, difusión, anillo, distorsión, fragmentación temporal y resolución.
- **Abismo al 100 %** salta de afinación hasta ±12 semitonos (por diseño). Para algo menos extremo, usa 30–60 % o Identidad.
- **CPU:** 8 voces de resíntesis pasan de ~14 % a ~27 % de un núcleo con Cosmos o Abismo, en la máquina de pruebas.
- **Memoria:** cada voz reserva unos 0.6 s de búfer estéreo para la fragmentación. Con Calidad Ultra (4x), unos 15 MB en total.

## MIDI y otros

- **La latencia reportada** es una estimación del oversampling: 3 muestras en 2x y 4 en 4x; lo medido fue 2 y 3.
- **Las semillas del Cerebro (Jardín) no son reproducibles:** "Al azar" usa el azar del sistema. Las mutaciones del ADN sí son reproducibles.
- **Interfaz:** no está acelerada por GPU. En el Cerebro el fondo se repinta seguido.
- **El % del imitador interno** (cuando no hay resíntesis) se mide igual que el de RECREAR al terminar. Durante la búsqueda, el optimizador usa su propia distancia interna, que no se muestra.

## FORGE (síntesis inversa, rama `feature/nebula-0.4.0`)

Las cifras están en `docs/0.4.0/P3-P6.md`.

- **El % de FORGE es fidelidad de recreación.** Es una medida objetiva, no una prueba de escucha.
- **Campanas FM y pads largos se recuperan peor** que bajos, 808 y plucks: en la recuperación interna, una FM_BELL quedó en 34 %.
- **Una receta parecida no es la receta original.** En pads, la receta encontrada puede estar tan lejos en parámetros como un patch al azar.
- **CMA-ES está implementado,** pero con el presupuesto actual la búsqueda local da mejores resultados. Por eso es la opción por defecto.
- **Cosmos/Abismo de la receta** cambia el patch nativo, de forma reversible. No es lo mismo que la transformación del motor, que procesa el audio. Las dos conviven en TRANSFORMAR.
- **Cambiar la Calidad mientras suena** reasigna el sobremuestreo en el hilo de audio y puede producir un corte. El resto de la automatización no produce picos (prueba `Realtime`).
