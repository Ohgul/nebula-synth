# Nebula: sintetizador con modo Jardín

Sintetizador polifónico (VST3 / AU) para Ableton en Mac, con un modo para *cultivar* sonidos por mutación.

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
