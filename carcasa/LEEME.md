# Carcasa MURDOCK (impresión 3D)

Caja de pecho para el asistente: dos piezas, sin tornillos para cerrar.

| Archivo | Qué es |
|---|---|
| `murdock_frente.stl` | Cuerpo principal: cámara, texto MURDOCK grabado, parlante, micrófono, estantes internos |
| `murdock_tapa.stl` | Tapa trasera deslizable con traba y presillas para correa de pecho de 25 mm |
| `murdock_carcasa.py` | Diseño paramétrico (todas las medidas al inicio del archivo) |
| `vistas_previas.py` | Genera las imágenes de `vistas/` |
| `visor.html` | Página del visor 3D interactivo (publicada aparte; usa los modelos convertidos a JSON) |

## Imprimir
- Material **PETG**, capa 0.2 mm, 3 perímetros, relleno 20 %, **sin soportes**.
- Frente: con la cara frontal (MURDOCK) apoyada en la cama.
- Tapa: con la cara lisa en la cama, presillas hacia arriba.
- Si la tapa entra muy dura, subir `HOLGURA` a 0.4 y volver a generar.

## Cambiar medidas y volver a generar
Medir con regla o vernier los módulos reales (portapilas, parlante, placa soldada, altura de la lente
de la cámara, interruptor) y cambiar los números en la sección PARÁMETROS de `murdock_carcasa.py`.

```
pip install manifold3d numpy matplotlib
python murdock_carcasa.py      # escribe los dos STL
python vistas_previas.py       # actualiza las imágenes de vistas/
```

## Dónde va cada cosa (derecha/izquierda de quien lo lleva puesto)
- Frente: cámara arriba, MURDOCK al centro, parlante y micrófono abajo.
- Arriba: botón; por dentro, el marco de la antena del GPS (mirando al cielo).
- Derecha: USB-C del cargador. Izquierda: interruptor y ranuras de aire del LM2596.
- Abajo: salida del cable de los motores. Atrás: tapa y presillas inclinadas 8° hacia abajo.
- Dentro: postes de la placa soldada (capa delantera), cuna del portapilas y dos estantes
  (LM2596 + cargador; BMS + MAX98357A + GPS).
