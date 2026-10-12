# Cómo probar Nebula 3.1 en Ableton Live (Mac)

## Instalar

1. **Descarga el instalador** desde GitHub → Actions → la ejecución más reciente de `nebula-v3-dev` → artefacto **Nebula-Instalador-3.1.0**. Descomprímelo.
2. **Ábrelo con clic derecho → Abrir.** El instalador está firmado *ad hoc*, no por Apple, así que macOS advierte la primera vez. Si lo bloquea: Ajustes del Sistema → Privacidad y seguridad → "Abrir de todos modos".
3. **Revisa que se instalaron los tres:**
   - el VST3 en `/Library/Audio/Plug-Ins/VST3`;
   - el AU en `/Library/Audio/Plug-Ins/Components`;
   - la app en `/Applications`.
4. **En Ableton:** Preferencias → Plug-ins → activa VST3 y/o Audio Units → **Volver a escanear**. Si Nebula no aparece, reinicia Ableton.

## Lista de pruebas

Marca cada punto que suene bien y anota los que no.

### A. 808 (lo más importante)

1. Arrastra un 808 limpio (un G1 o F1 de 1 a 2 s) al Cerebro y pulsa **RECREAR**.
2. Toca la tecla de su nota y compara con **A/B: original**:
   - ¿suena sin ruido extra en el sostenido?
   - ¿el golpe tiene el mismo ataque?
3. Repite con un 808 con glide y con un 808 saturado.
4. Mira el % de parecido y su detalle (color, envolvente, tono).
   - Un 808 bien recreado suele dar 55–75 %.
   - Si sale menos de 45 %, guarda el sample para revisarlo.

### B. Pads largos

1. Recrea un pad de 6 a 12 s.
2. Mantén una nota más tiempo que el sample:
   - el timbre debe seguir moviéndose, no congelarse;
   - al soltar debe sonar la cola real del sonido.
3. Toca acordes de 4 notas: deben sonar las 4.

### C. MIDI

1. Haz un clip con acordes y notas encadenadas → debe sonar en **Poli**.
2. Prueba **Mono** y **Legato** en Editar → Voces.
3. Prueba el pedal de sustain (CC64): las notas quedan sonando hasta soltar el pedal.
4. Prueba el pitch bend (rango por defecto: 2 semitonos).
5. Pon 13 notas o más a la vez: al robar voces no debe oírse un clic.

### D. ADN

1. En **Cerebro → ADN**, mueve cada perilla mientras tocas una nota larga. Todas cambian el sonido; **Inarmonía** sólo funciona si el sonido tiene parciales.
2. **Mutar** con la semilla 5, **Deshacer** y **Mutar** otra vez con la semilla 5: debe dar exactamente el mismo resultado.
3. Pon candado a un gen, muta y comprueba que ese gen no cambió.
4. Usa **Guardar A**, muta, usa **Guardar B** y alterna entre A y B.
5. **Copiar** en una pista y **Pegar** en otra instancia de Nebula.
6. **Automatización:** graba en el clip un movimiento de *ADN Brillo*. Al reproducir debe cambiar el sonido de la nota que ya suena, sin clics.
7. Guarda el set, ciérralo y ábrelo de nuevo: el ADN, los candados y A/B deben seguir ahí.

### E. Cosmos y Abismo

1. Recrea un pad y lleva la barra a **Cosmos 100 %**: debe abrirse, moverse y expandirse sin perder volumen.
2. Pulsa el botón Mono del Utility de Ableton: debe seguir sonando bien.
3. Lleva la barra a **Abismo 100 %**: debe sonar fragmentado y caótico, pero sin picos fuertes. Baja **Salida** a -12 dB y comprueba que el nivel baja.
4. Con **Identidad** al 100 % debe volver casi al original.
5. Automatiza la barra de punta a punta en 2 compases con un acorde: no debe haber clics fuertes ni silencios repentinos.
6. **Exporta el audio** dos veces: los dos renders deben ser idénticos (el caos es reproducible).

### F. Compatibilidad

1. Abre un set guardado con Nebula 3.0: debe sonar igual que antes.
2. Carga presets de fábrica y presets propios.

### G. Rendimiento

1. Con 8 voces de un pad recreado y Cosmos al 100 %, mira el medidor de CPU de Ableton.
2. Prueba **Calidad Alta** (2x) y **Ultra** (4x): la latencia se compensa sola.

## Si algo falla

Anota estos datos:

- Ableton (versión);
- macOS (versión);
- Mac Intel o Apple Silicon;
- VST3 o AU;
- el sample usado;
- qué hiciste.

Si puedes, guarda el set con el sample incluido (Archivo → Recopilar todo y guardar).
