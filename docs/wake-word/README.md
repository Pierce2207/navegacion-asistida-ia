# Palabra "Visión" detectada en la placa (TinyML con Edge Impulse)

Hoy el firmware detecta "Visión" subiendo cada ~1–4 s los últimos 3 s de audio a la búsqueda de
Deepgram (`searchForWakeWords` en `firmware/src/deepgram_client.cpp`, `wakeSearchTask` en
`main.cpp`). Tarda 1–4 s y ocupa el WiFi. Esta guía entrena un modelo pequeño que corre **dentro
de la ESP32-CAM** y lee el mismo buffer circular `wake_word_buffer` (PCM 16 bit, 16 kHz).

Resumen del camino:

1. Grabar muestras (sección 2) con `scripts/kws/`.
2. Crear el proyecto en Edge Impulse, subir, entrenar (secciones 3–4).
3. Medir precisión, falsos positivos por hora y tamaño (sección 5) y anotarlos en la tabla.
4. Exportar como librería Arduino, copiarla a `firmware/lib/` y subir el entorno `wake-local`
   (sección 6). Deepgram queda como respaldo.

---

## 1. Lo que ya está listo en el repo

| Archivo | Qué hace |
|---|---|
| `firmware/src/wake_word_local.h/.cpp` | Envuelve el modelo: clasifica 1 s de audio y aplica el suavizado (2 de 3 ventanas sobre el umbral, 1.5 s de pausa). |
| `firmware/src/main.cpp` | Con `-DWAKE_WORD_LOCAL`, cada 250 ms copia el último segundo de `wake_word_buffer` y lo clasifica en una tarea del Core 1 (`wakeLocalTask`). Sin ese `#define`, o si el modelo no arranca, usa Deepgram como antes. |
| `firmware/platformio.ini` | Entornos `wake-local` (modelo local) y `mic-capture` (grabar muestras con el micrófono de la placa). `esp32cam` y `demo` no cambian. |
| `firmware/src/mic_capture.cpp` | Firmware mínimo que manda el audio del INMP441 a la PC por USB. |
| `scripts/kws/*.py` | Grabar (`capturar_esp32.py`, `grabar_pc.py`), cortar en clips de 1 s (`cortar_clips.py`), subir (`subir_edge_impulse.py`) y medir falsos positivos por hora (`evaluar_modelo.py`). |

Instalar las dependencias de los scripts: `pip install -r scripts/kws/requirements.txt`.

---

## 2. Grabar las muestras

### Clases y cuántas

| Clase (etiqueta exacta) | Qué es | Mínimo | Ideal |
|---|---|---|---|
| `vision` | La palabra "visión" sola, dicha por distintas personas | 300 | 500+ |
| `otras_palabras` | Palabras que NO son "visión", sobre todo las parecidas | 300 | 500+ |
| `ruido` | Ambiente sin nadie diciendo "visión": calle, TV, cocina, viento, pasos, el ventilador de la laptop | 300 clips (5 min) | 30+ min |

Las tres clases deberían quedar con cantidades parecidas. Del ruido es fácil tener mucho: 10 min
de grabación dan 600 clips.

### Cómo grabar "vision"

- **Varias voces**: todo el equipo, y si se puede familiares (voces graves, agudas, mayores). Cuantas
  más personas, mejor generaliza. 60–100 repeticiones por persona.
- **Varias formas**: normal, rápido, lento, bajito, fuerte, "visión" con pregunta después
  ("visión, ¿qué ves?"), con acento en la "o" marcado y sin marcar.
- **Varias distancias**: con la placa colgada al pecho (como se usará), a 30 cm, 1 m y 2 m.
- **Con ruido de fondo**: una tanda en silencio, una con TV o música bajita, una en la calle.
- Dejar ~1.5–2 s entre una "visión" y la siguiente, así `cortar_clips.py` las separa bien.

### Micrófono: la placa primero

El modelo va a escuchar el INMP441 de la placa con la ganancia del firmware. Grabar con ese mismo
micrófono da mucho mejor resultado que con el celular o la laptop. **Al menos 1/3 de las muestras
de cada clase deberían salir de la placa.** Las de PC sirven para sumar voces.

Con la placa (sin WiFi ni cámara):

```
cd firmware
pio run -e mic-capture -t upload        # desconectar antes el TX del GPS de GPIO3
cd ..
python scripts/kws/capturar_esp32.py --puerto COM5 --salida grab/vision_sebas_pecho.wav
```

Con la laptop (modo guiado, avisa cuándo decir la palabra):

```
python scripts/kws/grabar_pc.py --salida grab/vision_ana_pc.wav --guiado --veces 60
python scripts/kws/grabar_pc.py --salida grab/ruido_tv.wav --segundos 600
```

Nombrar los archivos `clase_persona_condicion.wav` (p. ej. `vision_luis_calle.wav`): así luego se
puede apartar a una persona entera para prueba.

### Lista para "otras_palabras"

Las parecidas son las que más falsos positivos evitan:

> misión, división, decisión, revisión, versión, previsión, televisión, avión, camión, visita,
> visor, bisonte, pasión, ilusión, canción, atención, opción, isla, Lisa, Vicente, vino, visto,
> "y si no", "mi sillón", "dime", "oye", "hola", "sí", "no", "gracias", "dónde estoy", "llévame",
> "cuidado", "ayuda", "qué ves", nombres del equipo, números del uno al diez.

Una tanda por persona leyendo la lista dos veces, con pausa entre palabras. También sirve grabar
conversación normal y cortarla en modo `fijo` como `otras_palabras` (sin decir "visión").

### Cortar en clips de 1 s

```
python scripts/kws/cortar_clips.py grab/vision_*.wav --etiqueta vision --salida clips/vision
python scripts/kws/cortar_clips.py grab/otras_*.wav --etiqueta otras_palabras --salida clips/otras_palabras
python scripts/kws/cortar_clips.py grab/ruido_*.wav --etiqueta ruido --modo fijo --salida clips/ruido
```

Escuchar 20–30 clips al azar de cada carpeta. Borrar los que tengan la palabra cortada o no la
tengan. Si corta mal, ajustar `--factor` (más bajo = más sensible) o `--pausa`.

**No subir las grabaciones al repo** (son voces del equipo y pesan): dejarlas en `grab/` y `clips/`
fuera de git o en una carpeta compartida.

---

## 3. Proyecto en Edge Impulse

1. Crear cuenta gratis en <https://studio.edgeimpulse.com> y un proyecto llamado
   **`halo_vision_kws`** (el nombre define el nombre de la librería que exporta; si se usa otro,
   cambiar el `#include` de `wake_word_local.cpp` y `lib_ignore` en `platformio.ini`).
2. Tipo de proyecto: Audio / Keyword spotting. Dispositivo objetivo (Dashboard → Target):
   **Espressif ESP-EYE (ESP32 240 MHz)**, que es el mismo chip; así las estimaciones de
   tiempo y memoria salen para nuestra placa.
3. Copiar la API key (Dashboard → Keys) y subir los clips:

```
set EI_API_KEY=ei_...                     (Windows; en Linux/macOS: export EI_API_KEY=ei_...)
python scripts/kws/subir_edge_impulse.py clips/vision --etiqueta vision --prueba-si-contiene _luis_
python scripts/kws/subir_edge_impulse.py clips/otras_palabras --etiqueta otras_palabras --prueba-si-contiene _luis_
python scripts/kws/subir_edge_impulse.py clips/ruido --etiqueta ruido
```

`--prueba-si-contiene _luis_` deja todas las grabaciones de una persona solo para prueba: así la
precisión que se reporta es la de una voz que el modelo nunca oyó (más honesta que el 80/20 al
azar). La API key no va en el código ni en git.

Opcional: Edge Impulse ofrece un dataset público de ruido ("Keyword spotting pre-built dataset",
clases noise/unknown). Sus clips de `noise` se pueden sumar a `ruido`. El `unknown` está en inglés,
así que sirve poco para `otras_palabras`.

---

## 4. Impulso y entrenamiento

**Create impulse**

- Time series data: Window size **1000 ms**, Window increase **250 ms**, Frequency **16000 Hz**,
  Zero-pad data activado.
- Processing block: **MFCC** (si al final no entra en tiempo, probar **MFE**, que es más liviano).
- Learning block: **Classification**.

**MFCC**: dejar los valores por defecto y pulsar *Autotune parameters*. Luego *Generate features*:
en el gráfico, `vision` debería formar un grupo separado.

**Classifier**

- Arquitectura por defecto de keyword spotting (1D conv). 100 ciclos, learning rate 0.005.
- Activar **Data augmentation** (agregar ruido, enmascarar tiempo y frecuencia): baja mucho los
  falsos positivos en ambientes nuevos.
- Opciones avanzadas: activar el **EON Compiler** y elegir **Quantized (int8)**. Ambos reducen RAM y
  flash.

Si confunde `vision` con `otras_palabras`, el remedio casi siempre es más datos de esas palabras
parecidas, no un modelo más grande.

---

## 5. Medir y reportar

### Precisión

*Model testing → Classify all*. Anotar la precisión total y la de la clase `vision` (cuántas
"visión" reales detecta) en la tabla de abajo.

### Falsos positivos por hora (objetivo: menos de 1)

La precisión por clip no alcanza: un 99% de acierto puede dar decenas de falsas activaciones por
hora, porque el firmware clasifica 4 ventanas por segundo (14 400 por hora). Dos formas de medirlo:

- **En Edge Impulse**: *Performance calibration* (solo para proyectos de audio). Simula horas de
  audio con ruido y da *false activations per hour* y *false rejection rate* para cada umbral.
- **Con nuestro script**, igual que el firmware (ventana de 1 s cada 250 ms, 2 de 3, pausa de
  1.5 s). Exportar *Deployment → Linux (x86)* como `.eim` y, en Linux/macOS/WSL:

```
pip install edge_impulse_linux
python scripts/kws/evaluar_modelo.py modelo.eim --negativos grab/tv_1h.wav grab/calle_30min.wav \
    --positivos grab/vision_luis_prueba.wav --esperadas 60
```

  Hace falta al menos 1 hora de audio sin "visión" (TV, radio, conversación, calle), que NO se
  haya usado para entrenar. Imprime una tabla umbral → FP/hora → aciertos para elegir
  `WAKE_LOCAL_THRESHOLD`.

### Tamaño

En *Deployment*, al elegir Arduino library con EON + int8, Edge Impulse muestra RAM, flash y
latencia estimadas para la ESP32. Al compilar `pio run -e wake-local`, PlatformIO muestra RAM y
flash totales; restar los del entorno `esp32cam` (hoy RAM 16.8% = 55 208 B, flash 39.1% =
1 230 413 B) da lo que suma el modelo.

Memoria en la placa: el tensor arena y los buffers del DSP se piden con `malloc`, y `main.cpp` ya
manda todo `malloc` de más de 256 B a la PSRAM (`heap_caps_malloc_extmem_enable(256)`), así que
no le quitan RAM interna a TLS ni WiFi. La tarea del modelo usa 8 KB de stack, lo mismo que la de
Deepgram a la que reemplaza. Al arrancar, el monitor serie imprime cuánto tarda cada
clasificación en la placa (`[WakeLocal] Modelo listo: ... ms por clasificacion`).

### Tabla de resultados (completar)

| Métrica | Objetivo | Resultado |
|---|---|---|
| Muestras (vision / otras / ruido) | 300–500+ cada una | |
| Personas distintas en `vision` | 5+ | |
| Precisión total (prueba, voz no vista) | > 90% | |
| Recall de `vision` (prueba) | > 90% | |
| Falsos positivos por hora (umbral elegido) | < 1 | |
| Umbral elegido (`WAKE_LOCAL_THRESHOLD`) | | |
| RAM del modelo (EI, int8 + EON) | lo menor posible; va a PSRAM | |
| Flash del modelo | < 200 KB | |
| Tiempo por clasificación en la placa | < 250 ms | |

---

## 6. Llevarlo a la placa

1. *Deployment* → **Arduino library**, EON Compiler activado, **Quantized (int8)** → Build. Baja un
   `.zip`.
2. Descomprimirlo en `firmware/lib/` para que quede
   `firmware/lib/halo_vision_kws_inferencing/library.properties` (y `src/` adentro).
3. Compilar y subir:

```
cd firmware
pio run -e wake-local -t upload
pio device monitor
```

4. En el monitor debe aparecer `🧠 [WakeLocal] Modelo listo...`. Cada 30 s imprime el puntaje
   máximo de "vision" que vio; al decir "Visión" debe sonar el ding casi al instante y aparecer
   `✅ Wake word detectada en la placa`.

### Ajustes (en `build_flags` del entorno `wake-local`)

| Flag | Por defecto | Para qué |
|---|---|---|
| `-DWAKE_LOCAL_THRESHOLD=0.85` | 0.80 | Más alto = menos falsos positivos, más "no me escuchó". |
| `-DWAKE_LOCAL_MIN_HITS=2` / `-DWAKE_LOCAL_HISTORY=3` | 2 de 3 | Ventanas seguidas que deben superar el umbral. |
| `-DWAKE_LOCAL_HOP_MS=250` | 250 | Cada cuánto se clasifica. Si cada clasificación tarda más, subir a 500. |
| `-DWAKE_LOCAL_CONFIRM_DEEPGRAM` | apagado | Cada detección local se confirma con UNA búsqueda en Deepgram antes del ding: casi cero falsos positivos, ~1–2 s más solo cuando alguien dice algo parecido. |
| `-DWAKE_WORD_LABEL=\"vision\"` | `vision` | Si la clase se llamó distinto en Edge Impulse. |

### Volver a Deepgram

`pio run -e esp32cam -t upload` (el firmware de siempre). Además, si en `wake-local` el modelo no
arranca (sin PSRAM, sin la clase `vision`, error al clasificar), el firmware lo dice por el monitor
y usa Deepgram solo.

### Problemas conocidos

- *`fatal error: halo_vision_kws_inferencing.h: No such file`*: falta la librería en `firmware/lib/`
  o el proyecto de Edge Impulse tiene otro nombre (ver sección 3).
- *`multiple definition of ...`* al enlazar la librería de Edge Impulse: borrar `firmware/.pio` y
  volver a compilar; si sigue, avisar con el log completo.
- Detecta mal en la placa pero bien en Edge Impulse: casi siempre falta audio grabado con el
  micrófono de la placa (`mic-capture`).
