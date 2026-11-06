# Guía para agentes de código (OpenCode, Codex, Claude Code)

Asistente portátil de navegación para personas con baja visión (proyecto universitario, Lima, Perú).
Basado en Halo (IEEE SSCS Arduino Contest 2025). El equipo habla español: responder en español.
Estado al 6 nov 2026: todo el hardware probado junto sobre protoboard y funcionando; la placa se
entrega para soldar. Ver "Pendientes" al final.

## Reglas
- Secretos en `firmware/src/secrets.cpp` y `firmware/src/secrets.h` (ambos en .gitignore;
  plantilla `secrets.cpp.example`) y en `app/.env`. **Nunca** imprimir, copiar ni subir sus valores.
- Antes de cada commit, revisar el diff buscando claves (`AIza`, `sk-`, contraseñas).
- Commit y push solo cuando el usuario lo pida. Hacer push a `main` despliega el backend en Vercel.
- No cambiar pines del firmware: la placa soldada sigue `docs/esquema-conexiones.html`.

## Estructura
```
firmware/   ESP32-CAM (PlatformIO, Arduino). Entornos: esp32cam (real) y demo (GPS simulado)
  src/main.cpp            loop, botón/SOS, motores, alertas, glue de navegación, red de peligros
  src/vision_assistant.*  Gemini Live por WebSocket: frames cada 2 s, audio de la pregunta, WiFi
  src/gemini_config.h     prompt del sistema y herramientas (systemAction, getDirections, stopNavigation)
  src/navigation.*        guía giro a giro con la ruta de api/navigation.js
  src/TTS.cpp             voz Deepgram aura-2-celeste-es (MP3 -> PCM 16 kHz -> MAX98357A)
  src/deepgram_client.cpp búsqueda de la palabra "vision" (8 kHz)
  src/settings_manager.*  lee /settings (idioma, volumen); por defecto español
  lib/haptic/             PCF8574: 3 motores (P0 izq, P1 centro, P2 der) y botón (P3)
api/        backend en Vercel: index.ts (/settings, /notifications, /alerts), hazards.js, navigation.js
sql/        esquemas de Supabase (app_schema.sql, hazards_schema.sql con PostGIS)
app/        app "Murdock" del acompañante (Expo / React Native, Firebase push, Supabase), en español
docs/       guía de construcción, hoja de conexiones pin por pin, guion del video
```

## Cómo funciona
- El ESP32-CAM habla directo con **Gemini Live** (video + audio, modelo `gemini-3.1-flash-live-preview`)
  y **Deepgram** (palabra de activación y voz). Gemini no responde con audio: responde con llamadas
  a herramientas que el ESP ejecuta (hablar, vibrar, pedir ruta).
- Todo lo que necesita base de datos o claves de servidor pasa por la **API de Vercel**
  (`https://navegacion-asistida-ia.vercel.app`): Supabase guarda alertas, ajustes, tokens y peligros;
  Firebase manda la notificación push al celular.
- Rutas a pie: `api/navigation.js` usa OpenStreetMap (Nominatim para buscar/geocodificar, OSRM foot
  para la ruta), une giros a menos de 15 m y devuelve las indicaciones en español.
- Peligros compartidos: un `obstacle_alert` de un peligro fijo (hueco, escalón, poste, obra) con GPS
  real se reporta (POST /api/hazards); 3 reportes cercanos lo verifican; cada 15 s el ESP consulta los
  verificados a <15 m y avisa una vez por peligro.

## Compilar y subir el firmware
```
cd firmware
pio run -e esp32cam -t upload    # real
pio run -e demo -t upload        # GPS simulado en Plaza San Martín, camina a la siguiente esquina cada 20 s
pio device monitor               # 115200 baudios
```
- Si `pio` no está en el PATH: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.
- **Antes de subir, desconectar el TX del GPS de GPIO3** (comparte pin con la programación). Si no:
  "Serial data stream stopped". Si falla por ruido, bajar la velocidad: `PLATFORMIO_UPLOAD_SPEED=115200`.
- "Could not open COM6, port is busy": cerrar el monitor serie.
- Flags de prueba (vía `PLATFORMIO_BUILD_FLAGS`): `TEST_FAKE_GPS_LAT/LON`, `TEST_FAKE_WALK`,
  `TEST_FAKE_WALK_MS`, `TEST_HAPTICS`, `TEST_COMMAND_AFTER_MS` + `TEST_COMMAND_TEXT`.

## Hardware (resumen; detalle en docs/esquema-conexiones.html)
AI-Thinker ESP32-CAM (4 MB PSRAM, cámara OV2640) · MAX98357A: BCLK 12, LRC 13, DIN 2 · INMP441:
SCK 14, WS 13, SD 2 (I2S STAND_MSB) · GPS NEO-6M: TX -> GPIO3 · PCF8574 0x20: SDA 15, SCL 4, a 3.3 V ·
motores con PNP 2N3906 (activo en LOW) · alimentación: 2×18650 + BMS HX-2S-JH20 + LM2596 a 5.0 V al pin 5V.

## Problemas conocidos y lecciones
- **Bajones de voltaje (brownout) y WiFi que se cae** con buena señal: casi siempre es la
  alimentación (cable o puerto USB débil). Con baterías + LM2596 + condensador de 1000 µF se evita.
  El motor se apaga antes de que suene el parlante por la misma razón.
- **Memoria TLS:** cada conexión HTTPS usa ~40 KB de RAM interna; no abrir dos a la vez
  (`priority_request`, `navNetwork()`, `deviceBusyForNetwork()` coordinan esto).
- Al editar C++ con scripts, `\n` dentro de strings se convierte en un salto de línea real y rompe la
  compilación: usar un editor o escapar bien.
- Gemini a veces narra un peligro sin usar `obstacle_alert`; `handleSystemAction` lo detecta por
  palabras ("muy cerca", "bloqueado", "cuidado") y lo convierte en alerta.
- Si el WiFi falla al arrancar, los ajustes se reintentan 3 veces y el idioma queda en español.
- Supabase (plan gratis) se pausa por inactividad: entrar al dashboard y darle "Resume project".

## Pendientes
- Probar la alimentación con baterías (LM2596 ya ajustado a 5.0 V) y agregar el condensador de 1000 µF.
- Cambiar el módulo cargador por uno **2S (8.4 V), 1 A**: el comprado era 3S (daba 13.3 V).
- Prueba real en la calle con el hotspot del celular y el GPS.
- Placa soldada (ESP32-CAM en zócalo hembra, jumper en el TX del GPS) y carcasa 3D (cámara 10–15°
  hacia abajo, antena del GPS hacia arriba).
- APK de la app: `eas build -p android --profile preview` (subir antes las variables `EXPO_PUBLIC_*`).
- Palabra de activación local con Edge Impulse (un compañero) para reemplazar la búsqueda en Deepgram.
- Opcional: antena IPX (mover la resistencia de 0 Ω junto al conector).
