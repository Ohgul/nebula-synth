# Nebula - banco de pruebas (Paso 0)

Fecha: 11 Oct 2026 10:11:13pm | Intel(R) Xeon(R) Processor @ 2.80GHz | 2 CPU | modo completo

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

| Sonido | Cat. | Frames | Parc. | Ruido | Arm. | Recrear s | % UI (heuristico) | Raiz err ct | Color dB | Mel 10ms atq/cuerpo/cola | Env dB | Nota ct | Curva tono ct | Corr centroide | Corr flujo | Temprana ref/rec dB | Estereo dif | LU rec | LU A/B | Repet. dB | NaN | Sub dif dB | Agudos dif dB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pluck_sierra | tonal | 17 | 0 | si | si | 1.25 | 97.6121 | 0.0 | 0.16 | 0.9 / 0.2 / 0.1 | 0.2 | 0.0 | 0.0 / 0.1 | 1.00 | 1.00 | -9.6 / -12.5 | - | -11.5 | -0.0 | -200 | 0 | -0.15 | 0.09 |
| lead_sierra | tonal | 192 | 0 | si | si | 2.16 | 69.3871 | 0.5 | 1.34 | 2.4 / 1.9 / 1.4 | 0.1 | -0.3 | 4.5 / 12.5 | 1.00 | 0.89 | -17.4 / -16.7 | - | -13.1 | 0.1 | -200 | 0 | 0.00 | -0.26 |
| 808_limpio | 808 | 22 | 0 | si | si | 5.36 | 72.2607 | 0.1 | 3.89 | 4.6 / 0.3 / 0.2 | 0.2 | -0.1 | 0.0 / 0.3 | 1.00 | 1.00 | -10.0 / -10.2 | - | -15.3 | -0.0 | -200 | 0 | 0.00 | -9.44 |
| 808_saturado | 808 | 25 | 0 | si | si | 5.04 | 87.3049 | 0.0 | 2.22 | 2.9 / 0.1 / 2.5 | 0.0 | 0.0 | 0.0 / 0.2 | 0.97 | 0.86 | -10.1 / -10.4 | - | -16.3 | 0.0 | -200 | 0 | 0.00 | -9.05 |
| 808_glide | 808 | 21 | 0 | si | si | 4.88 | 58.7827 | 500.1 | 4.88 | 5.6 / 7.8 / 0.6 | 0.3 | -0.0 | 0.4 / 100.9 | 0.88 | -0.03 | -9.5 / -9.6 | - | -15.6 | 0.5 | -200 | 0 | -0.00 | -9.26 |
| 808_largo_dist | 808 | 33 | 0 | si | si | 8.67 | 83.208 | 0.0 | 2.10 | 3.7 / 0.2 / 0.2 | 0.0 | -0.0 | 3.6 / 10.2 | 1.00 | 1.00 | -10.5 / -11.0 | - | -15.4 | 0.0 | -200 | 0 | 0.00 | -9.20 |
| pad_vibrato | tonal | 86 | 0 | si | si | 2.20 | 68.6141 | 0.2 | 1.50 | 3.6 / 2.7 / 3.4 | 1.3 | -0.4 | 13.5 / 20.8 | 0.26 | 0.86 | -35.5 / -25.6 | - | -6.8 | 0.1 | -200 | 0 | 13.02 | -0.40 |
| voz_a | tonal | 189 | 0 | si | si | 1.80 | 78.9821 | 0.5 | 0.45 | 1.2 / 0.8 / 1.3 | 0.1 | -0.9 | 2.2 / 3.6 | 0.34 | 1.00 | -25.8 / -26.5 | - | -11.0 | -0.1 | -200 | 0 | 7.36 | -0.01 |
| piano_sint | tonal | 24 | 0 | si | si | 2.40 | 78.6775 | 8.5 | 2.36 | 2.6 / 2.9 / 2.6 | 0.1 | -6.1 | 6.0 / 12.3 | 0.82 | 0.62 | -10.3 / -10.9 | - | -12.5 | 0.2 | -200 | 0 | -3.47 | -2.05 |
| cuerda_frotada | tonal | 192 | 0 | si | si | 2.59 | 77.8127 | 0.0 | 0.73 | 10.9 / 1.1 / 12.0 | 5.1 | -0.0 | 6.6 / 25.0 | 0.47 | 0.98 | -29.5 / -17.0 | - | -8.4 | 0.1 | -200 | 0 | 0.15 | -0.06 |
| campana | inarmonico | 106 | 4 | si | no | 2.86 | 24.8882 | -2407.5 | 15.02 | 16.6 / 15.5 / 12.0 | 0.5 | 15.4 | 13.1 / 16.7 | -0.30 | 0.52 | -11.8 / -13.0 | - | -11.6 | -1.9 | -200 | 0 | 14.48 | 15.96 |
| gong_metal | inarmonico | 192 | 0 | si | no | 1.37 | 10.5212 | - | 19.67 | 20.7 / 18.5 / 12.6 | 0.6 | - | - / - | 0.26 | 0.53 | -10.1 / -11.4 | - | -0.6 | -5.8 | -200 | 0 | -5.85 | 38.73 |
| bombo_808 | percusivo | 14 | 1 | si | no | 2.39 | 55.9551 | - | 5.12 | 8.7 / 4.6 / 2.4 | 0.4 | - | - / - | 0.99 | 0.97 | -9.2 / -11.8 | - | -14.9 | -0.2 | -200 | 0 | 0.11 | 19.41 |
| caja | percusivo | 77 | 2 | si | no | 0.18 | 81.7822 | - | 1.78 | 3.0 / 2.5 / 2.4 | 0.5 | - | - / - | 1.00 | 0.93 | -6.7 / -10.9 | - | -17.2 | -0.0 | -200 | 0 | -6.78 | -1.00 |
| golpe_ruido | percusivo | 68 | 0 | si | no | 0.17 | 85.5702 | - | 2.66 | 3.7 / 3.2 / 3.3 | 0.5 | - | - / - | 0.23 | 0.99 | -7.8 / -10.4 | - | -11.3 | -2.8 | -200 | 0 | -3.34 | 0.14 |
| hihat | percusivo | 31 | 0 | si | no | 0.06 | 37.9101 | - | 6.07 | 6.5 / - / 6.0 | 1.3 | - | - / - | 0.20 | 1.00 | -3.8 / -6.9 | - | -17.6 | 2.0 | -200 | 0 | 4.74 | -0.01 |
| pad_evolutivo_12s | pad | 192 | 0 | si | si | 4.49 | 85.1953 | 0.1 | 2.61 | 9.8 / 6.6 / 7.3 | 4.1 | -0.0 | 0.4 / 1.6 | 0.47 | 0.60 | -18.0 / -12.5 | - | -13.6 | 0.3 | -200 | 0 | -0.05 | 0.10 |
| pad_vocal_8s | pad | 192 | 0 | si | si | 5.03 | 64.0456 | 0.0 | 2.58 | 9.7 / 2.7 / 5.3 | 3.4 | 0.0 | 3.4 / 4.9 | 0.96 | 0.79 | -23.6 / -15.2 | - | -8.7 | 0.2 | -200 | 0 | 0.09 | -0.23 |
| pad_estereo_4s | pad | 192 | 0 | si | si | 3.18 | 80.0671 | 0.0 | 2.89 | 10.2 / 4.1 / 5.8 | 3.0 | -0.0 | 0.1 / 0.3 | 0.97 | 0.92 | -29.5 / -19.5 | 1.06 | -12.9 | 0.1 | -200 | 0 | 0.06 | -0.43 |
| transitorio_0ms | transitorio | 12 | 0 | si | si | 1.14 | 99.0817 | 0.0 | 0.10 | 1.1 / 0.1 / 1.1 | 0.0 | 0.0 | 0.0 / 0.0 | 0.90 | 0.97 | -10.5 / -11.2 | - | -8.7 | -0.0 | -200 | 0 | -3.80 | 0.01 |
| transitorio_2ms | transitorio | 12 | 0 | si | si | 1.16 | 98.82 | 0.0 | 0.15 | 0.9 / 0.1 / 1.1 | 0.1 | 0.0 | 0.0 / 0.0 | 0.87 | 0.94 | -10.5 / -11.2 | - | -8.7 | -0.0 | -200 | 0 | -3.92 | 0.00 |
| transitorio_10ms | transitorio | 12 | 0 | si | si | 1.24 | 97.8003 | 0.0 | 0.24 | 1.3 / 0.1 / 1.1 | 0.1 | 0.0 | 0.0 / 0.0 | 0.13 | 0.90 | -10.4 / -11.1 | - | -10.0 | -0.0 | -200 | 0 | -4.29 | -0.02 |
| transitorio_50ms | transitorio | 19 | 0 | si | si | 1.15 | 98.3012 | 0.0 | 0.21 | 1.5 / 0.1 / 1.1 | 0.0 | 0.0 | 0.0 / 0.0 | 0.99 | 0.85 | -9.9 / -10.6 | - | -12.9 | -0.0 | -200 | 0 | -3.62 | -0.07 |
| transitorio_150ms | transitorio | 34 | 0 | si | si | 1.24 | 98.2302 | 0.0 | 0.23 | 1.7 / 0.1 / 1.1 | 0.1 | -0.0 | 0.0 / 0.1 | 0.99 | 0.92 | -9.8 / -10.4 | - | -13.6 | -0.0 | -200 | 0 | -3.17 | -0.08 |
| transitorio_400ms | transitorio | 57 | 0 | si | si | 1.22 | 95.7525 | 0.0 | 0.51 | 3.1 / 0.4 / 1.0 | 0.1 | 0.0 | 0.0 / 0.7 | -0.13 | 0.96 | -9.9 / -10.4 | - | -13.8 | -0.0 | -200 | 0 | 14.77 | -0.03 |

## 3. Viabilidad del residuo de transitorio

SDR del transitorio estimado (dB, mas alto = mejor) / fuga tonal tras el ataque (dB respecto al original, mas bajo = mejor).

| Sonido | Verdad ms | A tiempo (fase motor) | B magnitud (motor) | C tiempo (fase medida, 21 ms) | D magnitud (modelo) | E tiempo (fase medida, 11 ms) | Det. flujo ms | Det. residuo C a -20/-30/-40 dB ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 808_limpio | 5 | -0.1 / -35.4 | 5.1 / -43.1 | -66.6 / 46.2 | 0.1 / -159.4 | -70.3 / 50.0 | 389 | - / - / - |
| 808_saturado | 5 | -3.2 / -24.5 | 1.4 / -35.1 | -68.5 / 45.7 | 0.1 / -159.1 | -71.8 / 49.5 | 701 | - / - / - |
| 808_glide | 5 | -13.6 / -7.1 | -13.5 / -7.2 | -66.4 / 46.0 | 0.1 / -160.4 | -69.4 / 49.5 | 700 | - / - / - |
| 808_largo_dist | 5 | -3.7 / -27.6 | 2.1 / -33.1 | -68.7 / 47.3 | 0.1 / -158.3 | -72.4 / 50.5 | 449 | - / - / - |
| piano_sint | 28 | -20.9 / -0.0 | -20.8 / -0.1 | -2.4 / -75.9 | -1.9 / -88.7 | -15.6 / -53.7 | 0 | 11 / 18 / 22 |
| transitorio_0ms | 0 | falso -0.0 / -0.0 | falso -0.9 / -1.0 | falso -18.4 / -77.4 | falso -19.3 / -90.6 | falso -5.2 / -56.4 | 1 | 10 / 13 / 14 |
| transitorio_2ms | 2 | -28.3 / -0.0 | -27.4 / -1.0 | -27.7 / -77.4 | -5.5 / -90.6 | -28.5 / -56.4 | 0 | 13 / 17 / 17 |
| transitorio_10ms | 10 | -20.7 / -0.0 | -19.8 / -1.0 | -2.1 / -77.4 | -1.2 / -90.6 | -26.6 / -56.4 | 0 | 9 / 13 / 15 |
| transitorio_50ms | 50 | -16.8 / -0.0 | -15.9 / -1.0 | 3.9 / -19.9 | 4.5 / -60.8 | -28.1 / -18.2 | 0 | 23 / 35 / 45 |
| transitorio_150ms | 150 | -15.2 / -0.0 | -14.3 / -1.0 | 7.1 / -22.3 | 7.3 / -60.7 | -19.2 / -29.5 | 0 | 68 / 98 / 137 |
| transitorio_400ms | 400 | -13.7 / -0.9 | -9.7 / -5.0 | -4.0 / -12.8 | 7.8 / -52.2 | -18.8 / -11.2 | 0 | 175 / 267 / 385 |

## 4. CPU (un nucleo, % del tiempo real, mediana)

| Motor | Bloque | Calidad | Efectos | % tiempo real |
| --- | --- | --- | --- | --- |
| Resintesis | 128 | 1x | ninguno | 15.4 |
| Resintesis | 128 | 1x | todos | 16.5 |
| Resintesis | 128 | 2x | ninguno | 25.7 |
| Resintesis | 128 | 2x | todos | 30.4 |
| Resintesis | 128 | 4x | ninguno | 57.9 |
| Resintesis | 128 | 4x | todos | 57.1 |
| Resintesis | 512 | 1x | ninguno | 13.4 |
| Resintesis | 512 | 1x | todos | 15.6 |
| Resintesis | 512 | 2x | ninguno | 29.3 |
| Resintesis | 512 | 2x | todos | 32.3 |
| Resintesis | 512 | 4x | ninguno | 54.7 |
| Resintesis | 512 | 4x | todos | 65.8 |
| Sinte (Super Saw Lead) | 128 | 1x | ninguno | 3.0 |
| Sinte (Super Saw Lead) | 128 | 2x | ninguno | 6.0 |
| Sinte (Super Saw Lead) | 512 | 1x | ninguno | 3.1 |
| Sinte (Super Saw Lead) | 512 | 2x | ninguno | 5.8 |

## 5. Latencia

| Calidad | Reportada al DAW (muestras) | Medida (muestras) |
| --- | --- | --- |
| 1x | 0 | 0 |
| 2x | 3 | 2 |
| 4x | 4 | 3 |

## 6. Estabilidad (notas y automatizacion aleatorias, semilla fija)

| Motor | Segundos | NaN/Inf | Pico | Muestras >1.0 | Bloque mas lento (x tiempo real) | Silencio: bloque mas lento | Notas | Cambios de parametro |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sinte | 200 | 0 | 1.500 | 6327 | 7.88 | 0.508 | 5912 | 29282 |
| Sample | 200 | 0 | 1.250 | 315 | 12.63 | 0.086 | 5838 | 28804 |
| Resintesis | 200 | 0 | 1.478 | 109 | 10.76 | 0.917 | 5803 | 28896 |
| Resintesis estereo ancho | 200 | 0 | 1.500 | 13173 | 10.02 | 0.308 | 5729 | 28956 |

## 7. Clics (pico de 2a derivada sobre su entorno, dB)

| Evento | Con evento | Control sin evento | Diferencia |
| --- | --- | --- | --- |
| Resintesis: nota nueva con ataque 300 ms | 11.5 | 11.5 | 0.0 |
| Resintesis: robo de voz (13a nota, limite 12 voces) | 11.2 | 11.5 | -0.3 |
| Sinte (Pad Nube): nota nueva con ataque 300 ms | 12.7 | 12.7 | -0.0 |
| Sinte (Pad Nube): robo de voz (13a nota, limite 12 voces) | 11.7 | 12.7 | -0.9 |
| Resintesis: nivel del 1er ms con ataque 300 ms (esperado < -40 dB) | -77.0 dB | | |
| Sinte (Pad Nube): nivel del 1er ms con ataque 300 ms (esperado < -40 dB) | -66.5 dB | | |
| salto de corte del filtro | 11.5 | 8.0 | 3.5 |
| salto de volumen master (-6 a -18 dB) | 13.7 | 8.0 | 5.7 |
| CONTROL escalon -40 dBFS | 23.8 | 8.0 | 15.7 |
| CONTROL sin nada (t=1.5 s) | 8.3 | 8.0 | 0.3 |
| soltar la nota | 29.4 | 9.9 | 19.4 |
| llegada al ultimo frame (11.80 s) | 11.6 | 10.4 | 1.2 |

## 8. Proyectos v3 de referencia (compatibilidad)

Compatibilidad: 38 de 38 proyectos v3 suenan igual que su referencia (peor diferencia -106.7 dB; criterio <= -90 dB)

Duracion del banco: 1021.79 s
