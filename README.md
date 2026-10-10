# Nebula: sintetizador con modo Jardín

Sintetizador polifónico (VST3 / AU) para Ableton en Mac, con un modo para *cultivar* sonidos por mutación.

## Novedades de la versión 3
- **Concepto neuronal**: el Jardín ahora es un **Cerebro**.
  - Tu sonido es el cerebro del centro y las variaciones son neuronas conectadas por axones que disparan pulsos.
  - Todo el fondo es una red viva.
  - Entre más te alejas del sonido original o más subes **LOCURA**, más se transforma la interfaz: colores más calientes, neuronas que tiemblan, glitches.
- **Recrear de verdad (resíntesis nueva)**:
  - Nebula reconstruye el sample con armónicos que cambian en el tiempo, parciales sueltos (campanas, metales, golpes, el barrido de un 808) y ruido por bandas.
  - Sigue la curva de tono real (vibratos, glides o la melodía) y el ancho estéreo.
  - Todo queda **afinado a C**: en la tecla C suena C.
- **A/B**: compara el original contra lo recreado, al mismo volumen.
- **Panel del sample simplificado**: el audio, un botón **RECREAR**, el % de parecido y "¿Cómo se hizo?". Sección y Limpiar quedan abajo, chiquitos.
- **Perillas estilo Moog con anillo de LEDs**, más chicas y con mejor respuesta: la FM, la resonancia, el drive y el corte trabajan desde el principio de la perilla.
- **Botones de encendido** en el osc 1, osc 2, sub, ruido, unísono, filtro 1, filtro 2, LFOs, granos y cada efecto.
- **Pestaña Efectos**: EQ, compresor, chorus, delay (a tempo o en ms, ping pong) y reverb, más el ancho estéreo.
- **Macros** siempre a la mano: Brillo, Cuerpo, Movimiento, Espacio y **LOCURA**.
- **Cerebro**: **Mezclar** (de tu sonido a la neurona elegida, poco a poco), **Cruzar** dos neuronas y una memoria de generaciones.
- **Arrastra a Ableton**: las notas de una frase como **MIDI**, el sonido como **audio** y la **onda** (para Wavetable, Serum o Vital). Con un clic se guardan en Música/Nebula/Exportados.
- **Explorador de sonidos**: 34 presets con categorías, búsqueda y favoritos.
- **Deshacer y rehacer**.
- **Calidad** Normal, Alta (2x) o Ultra (4x), contra el aliasing.
- **Deriva analógica** y **MPE**.

## Novedades de la versión 2.1
- **Recrear sonido (resíntesis)**: Nebula analiza tu audio y lo reconstruye con sus propios osciladores.
  - Saca la nota exacta (con cents), una tabla de ondas que cambia en el tiempo (un cuadro por momento, con todos sus armónicos), el ruido con su envolvente y el vibrato.
  - Lo puedes tocar en cualquier nota y en acordes, y mutarlo en el Jardín.
  - Si el sonido no tiene nota clara o es inarmónico (campana, metal), lo arma con osciladores y FM.
- **Receta**: el botón del libro explica qué es el sonido (nota, tipo, timbre, ruido, envolvente, vibrato), cómo lo reconstruyó Nebula, cómo hacerlo en cualquier sinte y cómo está hecho el patch actual. Se puede copiar.
- **Tono exacto**: la nota del sample se detecta con cents y se corrige, así que tocar C# suena C#. Si el audio es una frase, se detecta su **tonalidad** (por ejemplo, Re menor) y la tónica queda como raíz.
- **Limpiar**: para audios que son un conjunto, te da varias versiones para elegir:
  - Nota principal, Parte tonal, Solo golpes, Sin ruido, Graves, Medios y Agudos.
  - Las escuchas, rescatas la que quieres y puedes seguir limpiando sobre ella.
  - El **historial** te deja regresar a cualquier versión. Lo rescatado se guarda dentro del proyecto.
- **Filtros nuevos**:
  - **Limpio** es transparente: totalmente abierto no toca la señal, y no pierde graves.
  - **Moog** es una escalera de 4 polos con compensación de graves.
  - **Moog+** colorea y satura más.
  - En el Jardín, **Fijar filtro** evita que las mutaciones cambien los filtros.
- **LFOs con botón de encendido**: apagado no mueve nada.
- **Arrastra un audio a un oscilador**, o usa el botón de carpeta junto al lápiz: se vuelve su onda (tabla Usuario).
- **Granos en el Sample**: **Granos** (cantidad) y **Tipo de grano** (Suave, Nítido, Disperso, Reversa, Octava), encima del sonido.
- **Color del ruido**: de oscuro a blanco. El ruido también es un destino de la matriz.

## Novedades de la versión 2
- Interfaz con **íconos** en lugar de texto; pasa el mouse sobre cualquier cosa para ver qué hace. Tiene un selector de **tamaño de ventana** (80–130 %).
- **Animaciones** en el Jardín (los brotes crecen y se transforman) y un **osciloscopio circular** alrededor de la semilla.
- Ondas nuevas: **Pulso con PWM**, **Super Saw**, **Sync** y **Wavetable**. La Wavetable incluye la **Onda libre**, hecha desde tu audio o dibujada a mano.
- **Unísono** de hasta 7 voces, con desafinación y ancho estéreo.
- **Dos filtros** de 12 o 24 dB, con carácter Limpio, Cálido o Saturado, en serie o en paralelo. Tienen una gráfica con puntos que se arrastran.
- **3 envolventes** con gráfica editable y modo Loop, **2 LFOs** con sync a tempo, y una **matriz de modulación** de 8 × 10.
- **Poli / Mono / Legato**, glide, número de voces, rango del pitch bend y sensibilidad a la velocidad.
- Sample: **Elegir sección**, con zoom, Escuchar, Auto y corrección de nota. El modo **Tempo** sincroniza los sonidos largos al BPM. El audio **se guarda dentro del proyecto**.
- **Presets propios**: se guardan en `~/Music/Nebula/Presets`.

## Cómo se usa

### Pestaña JARDÍN
- En el centro está la **semilla**, que es el sonido actual. Alrededor hay **8 brotes**, que son variaciones de la semilla.
- **Clic** en un brote: suena y se vuelve el sonido activo.
- **Doble clic** en un brote, o el botón **Plantar**: se vuelve la nueva semilla y germinan 8 variaciones.
- **Clic en la semilla**: regresas a ella.
- **Mutación**: cuánto cambian los brotes.
- Botones:
  - **Otra tanda**: nuevas variaciones de la misma semilla.
  - **Al azar**: empieza desde un sonido aleatorio.
  - **Atrás**: regresa a la generación anterior.
  - **Fijar env**: las mutaciones no tocan la envolvente de volumen.
  - **Escuchar**: suena al elegir un brote.
- La forma y el color de cada brote salen de su sonido. Cuando el sonido cambia, la figura se transforma suavemente.

**Arrastrar un audio** (columna SAMPLE)

Arrastra un WAV, AIFF, MP3, FLAC u OGG de hasta 120 s, o usa el botón de la carpeta. Luego:
- **Elegir sección** (tijeras): abre una vista grande para escoger qué parte del audio se usa.
  - Arrastra las banderitas de inicio y fin.
  - La rueda del mouse hace zoom y arrastrar mueve la vista. El doble clic usa todo el audio.
  - **Escuchar** reproduce solo la sección. **Auto** quita los silencios. **Nota** corrige la nota raíz si se detectó mal.
- **Imitar con el sinte** (varita): el sinte busca el patch que más se parece a la sección elegida.
- **Usar el sample** (onda): tocas la sección como instrumento.
- **Hacer onda** (círculo): convierte la sección en una wavetable para el oscilador 1. Eliges cuántos cuadros en la ventana de Elegir sección.

### Sonidos largos sincronizados al tempo
En **Editar → Sample**, cambia de **Clásico** a **Tempo** (metrónomo) y elige la **Duración**:
- Auto, o de 1/4 de tiempo a 8 compases.
- La sección se estira o se encoge para durar exactamente eso al BPM de Ableton, sin cambiar el tono.
- Si cambias el BPM del proyecto, se ajusta solo en la siguiente nota.

### Pestaña EDITAR
- **Osciladores**: un ícono por onda, con su vista previa. **Forma** cambia según la onda:
  - Pulso: el ancho.
  - Super Saw: la desafinación.
  - Sync: la intensidad.
  - Wavetable: la posición.

  Con Wavetable aparece el selector de tabla y el **lápiz** para dibujar tu propia onda.
- **Mezcla** (osc 1/2, sub, FM, ruido) y **Unísono**.
- **Filtros**: arrastra los puntos de la gráfica. Izquierda y derecha cambian el corte; arriba y abajo, la resonancia. Doble clic en un punto lo regresa al valor por defecto. El botón al lado del filtro 2 lo enciende, y los íconos de cadena eligen serie o paralelo.
- **Envolvente de volumen**: arrastra los puntos A, D/S y R. La rueda del mouse cambia la curva.
- **Voces y salida**: Poli, Mono o Legato; número de voces, glide, bend, velocidad y volumen.

Doble clic en cualquier perilla la regresa a su valor por defecto.

### Pestaña MODULACIÓN
- **Envolventes 1–3** con gráfica y botón **Loop**. La Env 2 también mueve los filtros con las perillas "Env".
- **LFO 1 y 2**: forma, velocidad libre o a tempo (Sync), entrada suave (fade-in) y reinicio con cada nota.
- **Matriz**:
  - Las columnas son fuentes y las filas, destinos.
  - Arrastra una casilla hacia arriba o hacia abajo para modular en positivo o en negativo. Doble clic la borra.
  - **Tiempo** acelera o frena todas las envolventes y LFOs. **Cantidad** escala toda la modulación.

### Presets
Usa las flechas para recorrer los presets.
- El **disquete** guarda tu sonido con un nombre.
- La **carpeta** abre donde se guardan.
- Tus presets aparecen en la lista bajo "Tus presets".

Todo se guarda con tu proyecto de Ableton: el sonido, el jardín, el sample (hasta 60 s), la sección y la onda dibujada.

## Compilar (lo hace GitHub solo)
Cada vez que subes cambios, GitHub compila el plugin en una Mac en la nube y genera `Nebula-Instalador-x.y.z.pkg`. Lo encuentras en la pestaña **Actions**: abre la ejecución más reciente y baja hasta **Artifacts**.

Si cambias algo y quieres que Ableton lo note, sube la versión en `CMakeLists.txt` (`project(NebulaSynth VERSION 2.0.1)`).

### Compilar en tu Mac (opcional)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Licencia de JUCE
JUCE 8 es gratis para uso personal bajo su licencia AGPLv3 o la licencia "Starter" (proyectos con ingresos menores a 50k USD). Si algún día lo vendes, revisa juce.com/get-juce.
