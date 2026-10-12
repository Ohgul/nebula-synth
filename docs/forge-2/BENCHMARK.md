# FORGE 2.0 — banco de referencia y puertas de calidad

Rama: `feature/forge-2-benchmark`. Base: `feature/nebula-0.4.0`.
Este documento define la **primera entrega verificable**; no proclama que FORGE 2.0 esté terminado.

## Punto de partida verificado en documentación anterior

Según `docs/0.4.0/P3-P6.md`, `NebulaTests Forge recuperacion 2 2`
evaluó 14 patches fuera de la partición de búsqueda (2 por categoría)
y obtuvo fidelidad media 70.7%. Hubo casos FM_BELL 32%, PAD 56%.
No son resultados de escucha ni una comparación con Synplant 2.

## Protocolo reproducible

1. Conservar la partición test fuera del índice que propone semillas.
2. Fijar semilla de búsqueda, nota MIDI, duración de hold y release,
   sample rate, modo de optimización y presupuesto de evaluaciones.
3. Ejecutar primero el baseline y registrar commit, compilador, OS, CPU,
   sample rate, duración, total de evaluaciones y resultados por ejemplo.
4. Ejecutar el cambio en la misma máquina y condiciones. No cambiar el
   banco de evaluación ni recalibrar el porcentaje entre versiones.
5. Reportar mediana, media, peor caso y desviación entre semillas,
   además de tiempo y CPU. Incluir fallas, no solamente mejores ejemplos.
6. Escuchar A/B nivelado, con orden aleatorio y sin conocer cuál es el
   original o la recreación. La fidelidad automática no reemplaza esto.

## Comandos existentes para establecer la línea base

```sh
# En una compilación que tenga el ejecutable NebulaTests:
NebulaTests Forge recuperacion 2 2
NebulaTests Forge optimizadores
NebulaTests Realtime 60
NebulaTests Forge
```

Los comandos anteriores ya existen en el repositorio; no se ejecutaron
al crear este documento. Guardar salida original y commit de cada corrida.

## Puertas de aceptación

- Ninguna regresión de las pruebas deterministas, MIDI, estado, compatibilidad
  y tiempo real existentes.
- Cero asignaciones dinámicas dentro de `processBlock` en el test Realtime.
- Para evaluar mejoras de FM_BELL/PAD: al menos 10 sonidos **externos** por
  categoría, distintos de los patches nativos empleados para las semillas,
  y un conjunto de control BASS/808/LEAD/PLUCK.
- Ningún incremento de similitud basado únicamente en ejemplos de entrenamiento.
- Medir el peor tiempo por bloque al cambiar calidad; es un riesgo ya conocido.
- Añadir escucha de 2 personas, nivelada, antes de afirmar calidad profesional.
- Objetivo orientativo: fidelidad media >= 85% en banco congelado, sin
  retroceso material en BASS/808. No es una capacidad alcanzada.

## Orden de implementación posterior

1. Añadir casos adversos de FM inarmónica, pads largos y texturas de ruido,
   y registrar métricas y semillas en salida estructurada.
2. Revisar función de pérdida rápida: transitorios, cola y color espectral
   multirresolución. Comparar con la función anterior en igualdad de costo.
3. Ampliar arquitecturas candidatas de FM; cualquier motor DSP nuevo
   requiere tests offline, tiempo real, guardado y automatización.
4. Reducir el costo del bloque durante cambios de calidad.
5. Hacer A/B en un DAW antes de etiquetar una versión como estable.

## Registro de experimentos

Cada experimento debe incluir:
`commit`, `dataset_version`, `partition`, `seed`, `category`,
`example_id`, `reference_sr`, `note`, `hold_s`, `total_s`,
`optimizer`, `eval_budget`, `best_fidelity`, `mean_fidelity`,
`best_loss`, `elapsed_s`, `cpu_percent`, `notes`.

No se copiará código, binarios ni presets propietarios de Synplant 2.
