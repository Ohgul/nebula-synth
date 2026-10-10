# Nebula: lista para la versión 2

Ideas y cambios acordados para la siguiente versión. Se marcan con [x] cuando estén hechos.

**Estado (v2.0.0):** todo lo marcado con [x] ya está en la versión 2. Lo que sigue en [ ] queda para una próxima versión.

Detalles de cómo quedó:
- La Super Saw usa la perilla **Forma** como desafinación y mezcla de laterales; el estéreo lo da la sección **Unísono** (Voces, Desafinar, Ancho).
- La Onda libre hace hasta **64 cuadros** por tabla (8, 16, 32 o 64). La perilla **Forma** es la Posición, y en la matriz es el destino "Forma 1/2". La tabla se guarda con el proyecto. Todavía no hay una biblioteca de tablas para reusar en otros proyectos.
- Filtros: pasa bajos, pasa banda, pasa altos y notch. El "morph" queda pendiente.
- Matriz de 8 fuentes (Env 2, Env 3, LFO 1, LFO 2, velocidad, rueda, aftertouch, nota) por 10 destinos (tono, forma 1, forma 2, mezcla, FM, corte 1, reso 1, corte 2, volumen, paneo). La Env 1 maneja el volumen directo.
- Las envolventes tienen modos Normal y Loop; el modo Beat queda pendiente. Los LFOs tienen formas, sync a tempo, retrigger y fade-in; la fase inicial queda pendiente.
- Las fuentes y la matriz están juntas en la pestaña **Modulación**. Las pestañas MIDI y MPE quedan pendientes.
- En la ventana **Elegir sección**, el doble clic usa todo el sonido y la rueda del mouse hace zoom. Los samples pueden durar hasta 120 s, y se guardan dentro del proyecto si duran 60 s o menos.
- El modo Tempo usa granos alineados (WSOLA) para que el sonido estirado no suene hueco ni metálico.

## Prioridad: que sea intuitivo
- [x] **Íconos** en vez de texto: formas de onda, tipos de filtro, fuentes de modulación y botones del Jardín. Al pasar el mouse sale una explicación.
- [x] **Animaciones en el Jardín**: los brotes crecen al germinar, y la forma se transforma suavemente de un sonido a otro al elegir un brote.
- [x] **Osciloscopio en vivo** dentro del Jardín, para ver cómo cambia la onda mientras suena.

## Osciladores (modo Sinte)
- [x] **Super Saw**: 7 sierras desafinadas entre sí, con controles de **Desafinación** y **Mezcla** (centro contra laterales) y abiertas en estéreo. Para leads y pads de trance y EDM.

- [x] **Onda libre (wavetable desde audio)**: arrastras cualquier sonido (voz, guitarra, ruido, un loop) y Nebula lo convierte en una tabla de ondas tocable.
  - Detecta la nota del audio y lo corta en ciclos de una sola onda (hasta 256 cuadros) a lo largo del sonido.
  - La perilla **Posición** recorre esos cuadros, así que el timbre va cambiando como cambiaba el sonido original. Se puede modular con LFO o envolvente.
  - Si el audio no tiene tono claro (ruido, percusión), lo corta en ventanas fijas y lo vuelve "ondas" igual.
  - Opciones al importar: **Normalizar**, **Suavizar** entre cuadros y elegir **cuántos cuadros**.
  - **Modo dibujo**: también puedes dibujar la onda a mano con el mouse.
  - Las tablas creadas se guardan con el proyecto, y como tablas de usuario para reutilizarlas.

Sugerencias (por confirmar):
- [x] **Pulso con PWM**: cuadrada de ancho variable que se puede modular con un LFO. Da un sonido "vivo" y clásico de pads y bajos analógicos.
- [x] **Sync**: sierra con hard sync a otro oscilador. El típico lead agresivo que "chilla" al mover el tono.
- [x] **Wavetable**: tablas de ondas que se recorren con una perilla de **Posición**. Es lo que mueve el "Osc 1 Pos" de tu captura.
- [ ] **Super Square**: lo mismo que la Super Saw, pero con cuadradas. Más hueco, estilo chiptune o "hoover" suave.
- [ ] **Formante / vocal**: ondas que suenan a vocales (a, e, i, o, u), con una perilla para pasar de una a otra.
- [ ] **Aditivo**: se dibujan los armónicos uno por uno. Bueno para órganos y campanas.
- [ ] **Ruidos de colores**: blanco, rosa y café, más ruido "digital" (crush) como oscilador propio.
- [ ] **Warp** del oscilador: deforma la onda (doblar, espejo, sincronía) para sacar más timbres de cualquier forma.

## Filtros
- [x] **Dos filtros** en vez de uno, cada uno con su propio encendido.
- [x] Ruteo **Serie / Paralelo** entre los dos filtros.
- [x] Pendiente seleccionable: **12 dB o 24 dB** por octava.
- [ ] Más tipos de filtro: pasa bajos, pasa altos, pasa banda, notch y morph.
- [x] **Carácter** del filtro: Limpio / Cálido / Saturado.
- [x] **Gráfica de la curva** del filtro que se mueve en tiempo real, con puntos que se puedan arrastrar para cambiar frecuencia y resonancia.

## Modulación
- [x] **Matriz de modulación**, una cuadrícula de fuentes contra destinos:
  - Fuentes: Envolvente de amplitud, Envolvente 2, Envolvente 3, LFO 1, LFO 2, velocidad, rueda de modulación, aftertouch.
  - Destinos: volumen, tono, posición y "warp" del oscilador, frecuencia y resonancia del filtro, mezcla, FM, ruido, etc.
- [x] **Tercera envolvente** y **segundo LFO**.
- [x] Pestaña **Fuentes de modulación** (Mod Sources), donde se editan todas las fuentes:
  - **Envolventes 1–3** con gráfica editable (arrastrar los puntos), curvatura de cada tramo y modos **Trigger / Loop / Beat**.
  - **LFO 1–2** con formas (seno, triángulo, sierra, cuadrada, aleatorio), **sync a tempo** (1/16, 1/8, 1/4…) o en Hz, fase, retrigger por nota, fade-in y gráfica de la forma.
- [x] Controles globales de la matriz: **Tiempo** (acelera o frena todas las envolventes y LFOs) y **Cantidad** (escala toda la modulación).
- [ ] Pestañas: **Fuentes de modulación / Matriz / MIDI / MPE**.

## Voces y control global
- [x] **Poly / Mono** con número de voces seleccionable.
- [x] **Glide** (portamento).
- [x] **Unísono**, con modo, número de voces y cantidad (desafinación y ancho estéreo).
- [ ] Soporte **MPE**.

## Sample: elegir la sección
- [x] Al cargar un audio, una **forma de onda grande con zoom** y dos marcadores, **Inicio** y **Fin**, que se arrastran para elegir qué parte del sonido se usa. Con doble clic se selecciona todo.
- [x] La sección elegida se usa en todo:
  - **Imitar con el sinte**: analiza solo esa parte. Hoy toma automáticamente los primeros 2.5 s.
  - **Usar el sample**: toca solo esa parte. Las mutaciones de Inicio, Loop, Reversa y Textura se quedan dentro de ella.
  - **Hacer onda**: la wavetable sale solo de esa parte.
- [x] Botón **Escuchar selección** para oír la parte elegida antes de usarla.
- [x] **Detectar automáticamente**: sugiere la sección, por ejemplo saltando el silencio inicial o tomando la nota principal.
- [x] Permitir samples más largos que 20 s, ya que ahora se puede elegir una parte.
- [x] La selección se guarda con el proyecto.

## Sample sincronizado al tempo
- [x] Modo de reproducción **Clásico / Tempo**. En Tempo, la sección elegida se estira o comprime para durar N tiempos (1/4 a 32, o **Auto**) al BPM del proyecto. El tono de la tecla ya no cambia la velocidad, así que los sonidos largos y los loops quedan sincronizados.

## Pendientes de la versión 1
- [x] La detección de nota falla con sonidos inarmónicos (campanas). Agregar un control para corregir la nota raíz a mano, en semitonos y cents.
- [x] Guardar el sample dentro del proyecto, para que no se pierda si mueves el archivo.
- [x] Presets de usuario: guardar y cargar tus propios sonidos.
