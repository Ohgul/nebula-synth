# Nebula - banco de pruebas (Paso 0)

Fecha: 10 Oct 2026 2:21:09pm | Intel(R) Xeon(R) Processor @ 2.80GHz | 2 CPU | modo completo

## 0. Autocomprobacion de los medidores

Seno 997 Hz a 0 dBFS (mono, contado en 2 canales): 0.00 LUFS (esperado 0.00 +-0.05)
YIN sobre sierra de 261.63 Hz: 0.11 ct (esperado 0 +-1)
Sonido contra si mismo: mel 0.000 dB, env 0.000 dB, tono 0.00 ct, corr flujo 1.000
Autocomprobacion: OK

## 1. Validacion de las metricas

Controles inaudibles (la metrica no debe moverse) y audibles (debe moverse >= 3x su ruido de fondo).

| Metrica | Control audible | Casos | Pasan (>=3x fondo) | Fondo max | Respuesta min |
| --- | --- | --- | --- | --- | --- |
| tono_cents | afinacion_+10ct | 7 | 7 | 1.734 | 9.910 |
| tono_cents | afinacion_+25ct | 7 | 7 | 1.734 | 24.897 |
| color_dB | agudos_-3dB_desde_1kHz | 8 | 6 | 0.438 | 0.563 |
| mel_10ms_dB | agudos_-3dB_desde_1kHz | 8 | 2 | 1.772 | 0.417 |
| centroide_bandas | agudos_-3dB_desde_1kHz | 8 | 7 | 0.486 | 0.045 |
| envolvente_dB | decaimiento_x0.8 | 3 | 3 | 0.259 | 1.306 |
| mel_ataque_dB | ataque_suave_30ms | 3 | 2 | 1.713 | 2.663 |
| energia_temprana_dB | ataque_suave_30ms | 3 | 3 | 1.964 | 15.837 |
| tiempo_ataque_ms | ataque_suave_30ms | 3 | 1 | 5.500 | 3.000 |
| evolucion_1-corrFlujo | evolucion_congelada | 3 | 2 | 0.030 | 0.027 |
| evolucion_1-corrCentroide | evolucion_congelada | 3 | 3 | 0.006 | 0.099 |
| estereo_corrDif | estereo_a_mono | 1 | 1 | 0.000 | 1.062 |

## 2. Fidelidad de RECREAR (motor actual)

| Sonido | Cat. | Frames | Parc. | Ruido | Arm. | Recrear s | % UI (heuristico) | Raiz err ct | Color dB | Mel 10ms atq/cuerpo/cola | Env dB | Nota ct | Curva tono ct | Corr centroide | Corr flujo | Temprana ref/rec dB | Estereo dif | LU rec | LU A/B | Repet. dB | NaN |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pluck_sierra | tonal | 64 | 0 | si | si | 0.86 | 100 | 0.0 | 0.29 | 0.9 / 0.1 / 0.1 | 0.2 | -0.1 | 0.2 / 0.4 | 1.00 | 1.00 | -9.6 / -11.6 | - | -11.5 | -0.0 | -200 | 0 |
| pad_vibrato | tonal | 64 | 0 | no | si | 1.37 | 92.2793 | -0.0 | 0.98 | 5.7 / 2.7 / 14.3 | 3.7 | 2.4 | 18.2 / 26.8 | -0.09 | 0.89 | -35.5 / -21.7 | - | -6.7 | 0.1 | -200 | 0 |
| voz_a | tonal | 64 | 0 | no | si | 0.96 | 88.5384 | 0.5 | 0.85 | 0.9 / 1.1 / 14.6 | 2.5 | -0.7 | 2.2 / 3.8 | 0.35 | 0.97 | -25.8 / -25.7 | - | -11.0 | 0.0 | -200 | 0 |
| piano_sint | tonal | 64 | 0 | si | si | 1.71 | 79.1891 | 8.5 | 2.15 | 2.6 / 2.7 / 2.0 | 0.2 | -5.4 | 5.2 / 10.8 | 0.99 | 0.49 | -10.3 / -10.9 | - | -12.4 | 0.1 | -200 | 0 |
| cuerda_frotada | tonal | 64 | 0 | no | si | 1.65 | 71.3183 | 0.0 | 0.70 | 12.0 / 1.2 / 10.4 | 3.7 | 0.2 | 7.3 / 25.1 | 0.98 | 0.99 | -29.5 / -16.2 | - | -8.4 | 0.2 | -200 | 0 |
| campana | inarmonico | 64 | 4 | si | no | 1.63 | 21.1087 | -2407.5 | 15.15 | 16.8 / 15.6 / 12.1 | 0.5 | 12.6 | 12.7 / 16.1 | -0.27 | 0.61 | -11.8 / -12.9 | - | -11.4 | -2.3 | -200 | 0 |
| gong_metal | inarmonico | 64 | 0 | si | no | 0.96 | 15.2101 | - | 19.68 | 20.9 / 18.5 / 13.1 | 0.9 | - | - / - | 0.22 | 0.43 | -10.1 / -11.6 | - | -0.7 | -5.6 | -200 | 0 |
| bombo_808 | percusivo | 64 | 1 | si | no | 0.66 | 99.8876 | - | 5.72 | 9.7 / 4.4 / 2.2 | 0.3 | - | - / - | 0.97 | 0.94 | -9.2 / -11.9 | - | -14.9 | -0.2 | -200 | 0 |
| caja | percusivo | 64 | 2 | si | no | 0.13 | 96.3162 | - | 1.79 | 3.0 / 2.5 / 2.4 | 0.5 | - | - / - | 1.00 | 0.93 | -6.7 / -10.8 | - | -17.2 | -0.0 | -200 | 0 |
| golpe_ruido | percusivo | 64 | 0 | si | no | 0.16 | 92.2532 | - | 2.66 | 3.7 / 3.3 / 3.3 | 0.5 | - | - / - | 0.22 | 0.99 | -7.8 / -10.3 | - | -11.3 | -2.8 | -200 | 0 |
| hihat | percusivo | 64 | 0 | si | no | 0.06 | 85.2998 | - | 6.05 | 6.5 / - / 4.9 | 1.3 | - | - / - | 0.22 | 1.00 | -3.8 / -6.9 | - | -17.5 | 1.9 | -200 | 0 |
| pad_evolutivo_12s | pad | 64 | 0 | no | si | 2.17 | 68.1586 | 0.7 | 7.69 | 12.0 / 8.6 / 12.7 | 5.4 | -0.8 | 0.6 / 1.4 | 0.23 | 0.33 | -18.0 / -12.1 | - | -13.3 | 0.1 | -200 | 0 |
| pad_vocal_8s | pad | 64 | 0 | no | si | 2.14 | 56.4709 | -0.3 | 3.60 | 12.0 / 3.5 / 12.3 | 4.6 | 0.1 | 3.4 / 5.0 | 0.61 | 0.55 | -23.6 / -14.5 | - | -9.3 | 0.4 | -200 | 0 |
| pad_estereo_4s | pad | 64 | 0 | no | si | 2.16 | 68.2003 | 0.0 | 2.62 | 11.1 / 4.3 / 15.1 | 3.8 | -0.0 | 0.0 / 0.2 | 0.95 | 0.90 | -29.5 / -18.9 | 1.06 | -13.0 | 0.4 | -200 | 0 |
| transitorio_0ms | transitorio | 64 | 0 | no | si | 0.84 | 85.4745 | 0.0 | 0.34 | 0.9 / 0.7 / 0.9 | 0.1 | 0.0 | 0.0 / 0.1 | 0.60 | 0.98 | -10.5 / -11.4 | - | -8.7 | -0.0 | -200 | 0 |
| transitorio_2ms | transitorio | 64 | 0 | no | si | 0.83 | 84.969 | 0.0 | 0.41 | 1.3 / 0.7 / 0.9 | 0.1 | 0.0 | 0.0 / 0.1 | 0.53 | 0.96 | -10.5 / -11.4 | - | -8.7 | -0.0 | -200 | 0 |
| transitorio_10ms | transitorio | 64 | 0 | no | si | 0.82 | 84.4199 | 0.0 | 0.72 | 1.6 / 0.7 / 0.9 | 0.1 | 0.0 | 0.0 / 0.1 | -0.64 | 0.95 | -10.4 / -11.4 | - | -10.0 | -0.0 | -200 | 0 |
| transitorio_50ms | transitorio | 64 | 0 | si | si | 0.83 | 84.1277 | 0.0 | 0.45 | 1.9 / 0.7 / 0.9 | 0.0 | 0.0 | 0.0 / 0.1 | 0.99 | 0.90 | -9.9 / -10.6 | - | -12.9 | -0.0 | -200 | 0 |
| transitorio_150ms | transitorio | 64 | 0 | si | si | 0.84 | 83.4744 | 0.0 | 0.48 | 1.9 / 0.8 / 0.9 | 0.1 | 0.0 | 0.0 / 0.1 | 0.99 | 0.95 | -9.8 / -10.4 | - | -13.6 | -0.0 | -200 | 0 |
| transitorio_400ms | transitorio | 64 | 0 | si | si | 0.83 | 75.3403 | 0.0 | 0.72 | 3.2 / 0.9 / 1.4 | 0.1 | -0.0 | 0.0 / 1.3 | -0.15 | 0.97 | -9.9 / -10.5 | - | -13.8 | -0.0 | -200 | 0 |

## 3. Viabilidad del residuo de transitorio

SDR del transitorio estimado (dB, mas alto = mejor) / fuga tonal tras el ataque (dB respecto al original, mas bajo = mejor).

| Sonido | Verdad ms | A tiempo (fase motor) | B magnitud (motor) | C tiempo (fase medida, 21 ms) | D magnitud (modelo) | E tiempo (fase medida, 11 ms) | Det. flujo ms | Det. residuo C a -20/-30/-40 dB ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| piano_sint | 28 | -20.9 / -0.0 | -20.9 / -0.0 | -2.4 / -75.9 | -1.9 / -88.7 | -15.6 / -53.7 | 0 | 11 / 18 / 22 |
| transitorio_0ms | 0 | falso -0.0 / -0.0 | falso -0.9 / -1.0 | falso -18.4 / -77.4 | falso -19.3 / -90.6 | falso -5.2 / -56.4 | 1 | 10 / 13 / 14 |
| transitorio_2ms | 2 | -28.3 / -0.0 | -27.4 / -1.0 | -27.7 / -77.4 | -5.5 / -90.6 | -28.5 / -56.4 | 0 | 13 / 17 / 17 |
| transitorio_10ms | 10 | -20.7 / -0.0 | -19.8 / -1.0 | -2.1 / -77.4 | -1.2 / -90.6 | -26.6 / -56.4 | 0 | 9 / 13 / 15 |
| transitorio_50ms | 50 | -16.8 / -0.0 | -15.9 / -1.0 | 3.9 / -19.9 | 4.5 / -60.8 | -28.1 / -18.2 | 0 | 23 / 35 / 45 |
| transitorio_150ms | 150 | -15.2 / -0.0 | -14.3 / -1.0 | 7.1 / -22.3 | 7.3 / -60.7 | -19.2 / -29.5 | 0 | 68 / 98 / 137 |
| transitorio_400ms | 400 | -13.7 / -0.9 | -9.7 / -5.0 | -4.0 / -12.8 | 7.8 / -52.2 | -18.8 / -11.2 | 0 | 175 / 267 / 385 |

## 4. CPU (un nucleo, % del tiempo real, mediana)

| Motor | Bloque | Calidad | Efectos | % tiempo real |
| --- | --- | --- | --- | --- |
| Resintesis | 128 | 1x | ninguno | 7.8 |
| Resintesis | 128 | 1x | todos | 8.9 |
| Resintesis | 128 | 2x | ninguno | 15.3 |
| Resintesis | 128 | 2x | todos | 17.9 |
| Resintesis | 128 | 4x | ninguno | 29.2 |
| Resintesis | 128 | 4x | todos | 30.5 |
| Resintesis | 512 | 1x | ninguno | 7.0 |
| Resintesis | 512 | 1x | todos | 8.7 |
| Resintesis | 512 | 2x | ninguno | 14.2 |
| Resintesis | 512 | 2x | todos | 17.6 |
| Resintesis | 512 | 4x | ninguno | 28.0 |
| Resintesis | 512 | 4x | todos | 29.9 |
| Sinte (Super Saw Lead) | 128 | 1x | ninguno | 2.6 |
| Sinte (Super Saw Lead) | 128 | 2x | ninguno | 5.0 |
| Sinte (Super Saw Lead) | 512 | 1x | ninguno | 2.8 |
| Sinte (Super Saw Lead) | 512 | 2x | ninguno | 5.1 |

## 5. Latencia

| Calidad | Reportada al DAW (muestras) | Medida (muestras) |
| --- | --- | --- |
| 1x | 0 | 0 |
| 2x | 0 | 2 |
| 4x | 0 | 3 |

## 6. Estabilidad (notas y automatizacion aleatorias, semilla fija)

| Motor | Segundos | NaN/Inf | Pico | Muestras >1.0 | Bloque mas lento (x tiempo real) | Silencio: bloque mas lento | Notas | Cambios de parametro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sinte | 200 | 0 | 1.500 | 3475 | 4.12 | 0.073 | 5912 | 29282 |
| Sample | 200 | 0 | 1.500 | 10140 | 4.11 | 0.025 | 5838 | 28804 |
| Resintesis | 200 | 0 | 1.500 | 58743 | 4.47 | 0.341 | 5803 | 28896 |
| Resintesis estereo ancho | 200 | 0 | 1.500 | 39241 | 6.20 | 0.266 | 5729 | 28956 |

## 7. Clics (pico de 2a derivada sobre su entorno, dB)

| Evento | Con evento | Control sin evento | Diferencia |
| --- | --- | --- | --- |
| Resintesis: nota nueva con ataque 300 ms | 7.8 | 7.8 | 0.0 |
| Resintesis: robo de voz (13a nota, limite 12 voces) | 26.9 | 7.8 | 19.1 |
| Sinte (Pad Nube): nota nueva con ataque 300 ms | 12.7 | 12.7 | -0.0 |
| Sinte (Pad Nube): robo de voz (13a nota, limite 12 voces) | 23.3 | 12.7 | 10.6 |
| Resintesis: nivel del 1er ms con ataque 300 ms (esperado < -40 dB) | -71.8 dB | | |
| Sinte (Pad Nube): nivel del 1er ms con ataque 300 ms (esperado < -40 dB) | -66.5 dB | | |
| salto de corte del filtro | 12.0 | 10.3 | 1.7 |
| salto de volumen master (-6 a -18 dB) | 11.8 | 10.3 | 1.5 |
| CONTROL escalon -40 dBFS | 24.6 | 10.3 | 14.3 |
| CONTROL sin nada (t=1.5 s) | 8.9 | 10.3 | -1.4 |
| soltar la nota | 14.7 | 10.4 | 4.3 |
| llegada al ultimo frame (3.97 s) | 22.3 | 14.7 | 7.7 |

## 8. Proyectos v3 de referencia (compatibilidad)

Referencia regenerada: 38 proyectos (38 se abren en modo legacy), audio en /home/claude/nebula-synth/tests/fixtures/v3/renders
Dos renders del mismo proyecto con la semilla del banco: peor diferencia -200.0 dB (-200 = identicos)

Duracion del banco: 472.454 s
