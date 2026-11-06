# Contexto del proyecto (para Claude Code)

Proyecto: asistente portátil de navegación para personas con baja visión, basado en Halo
(IEEE SSCS Arduino Contest 2025). Mejoras propias: hápticos direccionales (3 motores) y red
comunitaria de peligros compartidos. Ver README.md.

## Estructura
- `firmware/` — ESP32 con PlatformIO (cámara, mic I2S, parlante MAX98357A, GPS, Gemini Live,
  Deepgram, Google TTS, hápticos en `lib/haptic/`). Secretos en `firmware/src/secrets.cpp`
  (gitignored; plantilla en `secrets.cpp.example`). NUNCA subir ni imprimir sus valores.
- `api/` — backend en Vercel (`index.ts`: /settings, /notifications, /alerts; `hazards.js`).
  Producción: https://navegacion-asistida-ia.vercel.app (despliega solo con push a `main`).
- `sql/` — esquemas de Supabase (`app_schema.sql`, `hazards_schema.sql`).
- `app/` — app móvil "Murdock" (Expo / React Native, Firebase para push, Supabase).

## Cómo se conecta todo
El ESP32 NO habla con Supabase directo. Por WiFi habla con:
- Gemini Live (WebSocket), Deepgram (voz a texto), Google TTS y Google Maps.
- La API de Vercel: `GET /settings` (config de la app), `POST /notifications` (guarda en la tabla
  `alerts` de Supabase y manda push por Firebase a la app), `GET /api/hazards` (peligros cercanos).
En la calle, el ESP32 usa el hotspot del celular (SSID/clave en `secrets.cpp`).

## Estado (2 nov 2026)
- Supabase (proyecto navegacion-asistida-ia) estaba PAUSADO por inactividad del plan gratis;
  hay que darle "Resume project" en el dashboard. Mientras esté pausado no llegan notificaciones.
- Vercel: deployment de producción en Ready, desde el commit 889443b.
- App: instalada en el celular como build de DESARROLLO (EAS, perfil `development`). Para abrirla
  hay que correr `npx expo start --dev-client` en `app/` con el celular en la misma WiFi
  (o `--tunnel`). Para una app independiente: `eas build -p android --profile preview`, subiendo
  antes las variables EXPO_PUBLIC_* con `eas env:create` (el `.env` está gitignored y EAS no lo sube).
- Firmware: se adaptó (sin commitear aún) de la Freenove ESP32-WROVER CAM a la
  **AI-Thinker ESP32-CAM (OV3660, 4MB PSRAM)** con grabador ESP32-CAM-MB. Compila OK (2 nov 2026; RAM 16.5%, flash 38.2%; solo warnings de
  APIs obsoletas de ArduinoJson/I2S). Falta probarlo en la placa.
  Cambios: `platformio.ini` (board = esp32cam, env:esp32cam), `camera_pins.h` (mapa AI-Thinker),
  `gps_module.h` (RX en GPIO3, TX = -1), `main.cpp` (I2C hápticos en SDA 15 / SCL 4, botón leído
  desde PCF8574 P3 con `haptics.readPin(3)`), `lib/haptic` (nuevo `readPin()`).
- También hay cambios sin commitear en `app/` (íconos, app.json, eas.json, package.json) y
  `firmware/src/vision_assistant.cpp`, más archivos nuevos (docs/Guion_Video_Presentacion.docx,
  firmware/src/main_audio_test.cpp, scripts/, sql/app_schema.sql).

## Conexiones (AI-Thinker ESP32-CAM, sin tarjeta microSD)
| Módulo | Pin del módulo | ESP32-CAM |
|---|---|---|
| Mic INMP441 | SCK / WS / SD / L-R / VDD | GPIO 14 / 13 / 2 / GND / 3.3V |
| MAX98357A | BCLK / LRC / DIN / VIN | GPIO 12 / 13 / 2 / 5V (LRC y DIN compartidos con el mic) |
| GPS NEO-6M | TX / RX / VCC | GPIO 3 / sin conectar / 5V |
| PCF8574 (0x20) | SDA / SCL / VCC | GPIO 15 / GPIO 4 / 3.3V (nunca 5V) |
| PCF8574 | P0 / P1 / P2 | motores izq / centro / der vía PNP (activo en LOW) |
| PCF8574 | P3 | botón push-to-talk/SOS a GND |
| Monitor serie | — | GPIO 1 (U0T), 115200 baudios |

Circuito de cada motor: emisor PNP (2N3906/S8550) a 3.3V, base con 470Ω a la salida del PCF8574,
colector al motor (+), motor (−) a GND, diodo 1N4148 en paralelo al motor (cátodo al colector).
Se usa PNP (no NPN como decía la guía) porque el código enciende con LOW y el PCF8574 arranca
con salidas en alto: así los motores quedan apagados al encender.

## Cuidados de hardware
- GPIOs del ESP32 solo toleran 3.3V. Una sola fuente de 5V/2A a la vez + condensador 470–1000µF.
- Parlante solo a los bornes +/− del MAX (salida en puente: nunca − a GND).
- Desconectar el TX del GPS al flashear (comparte GPIO3 con el RX de programación).
- GPIO12 debe estar en bajo al arrancar; si no bootea, desconectar BCLK del MAX.
- Si el expansor es PCF8574A, su dirección es 0x38: cambiar `PCF8574_ADDR` en haptic_driver.h.

## Plan de armado (probar cada paso antes del siguiente)
1. ESP32-CAM sola: compilar y subir (`pio run -t upload` en `firmware/`), revisar WiFi y cámara
   con `pio device monitor`.
2. MAX98357A + parlante. 3. Micrófono. 4. GPS (probar afuera). 5. PCF8574 sin motores (debe
   imprimir "[Haptic] PCF8574 inicializado"). 6. Botón en P3. 7. Un motor, luego los otros dos.

## Pendientes
- Subir el firmware a la ESP32-CAM y seguir el plan de armado.
- Corregir el README raíz: dice `firmware/haptic/`, pero el módulo está en `firmware/lib/haptic/`;
  y quitar la línea de prueba "ta bien esta prueba :V" al final del README.
- Reactivar Supabase y probar notificaciones de punta a punta (ESP32 → Vercel → Supabase → app).

## Estado (4 nov 2026) — esto reemplaza lo anterior donde se contradiga
- Todo el hardware probado JUNTO en la ESP32-CAM (sobre la MB, cable USB bueno): cámara OV2640 (la
  OV3660 se dañó), parlante, mic INMP441 (formato I2S STAND_MSB), GPS en GPIO3/U0R (módulo multi-
  constelación $GN, funciona), PCF8574 en SDA=GPIO15/SCL=GPIO4 con 3 motores PNP, botón en P3.
- Voz: palabra "Visión" (búsqueda Deepgram a 8 kHz); la pregunta va como AUDIO directo a Gemini Live
  (activityStart/End manual) -> respuesta en ~1 s; voz de salida Deepgram aura-2-celeste-es en MP3.
- Botón (tarea propia): mantener >350 ms = hablar; 5 toques = SOS -> llega notificación al celular.
- Alertas de peligro: vibra primero el motor del lado (left/center/right) y luego habla.
- Navegación (OpenStreetMap, api/navigation.js + firmware/src/navigation.*): "llévame a X",
  "¿dónde estoy?", guía giro a giro con voz y motores, recálculo. Backend desplegado y probado;
  EN LA PLACA AÚN NO PROBADO (siguiente paso: build con -DTEST_FAKE_GPS_LAT=-12.0517
  -DTEST_FAKE_GPS_LON=-77.0345 -DTEST_FAKE_WALK y pedir "llévame a la Plaza de Armas").
- WiFi: WIFI_SSID_2/WIFI_PASSWORD_2 en secrets.cpp = hotspot del celular (2.4 GHz) para la calle;
  el usuario debe llenarlos. secrets.h tiene los extern nuevos (secrets.h está en .gitignore).
- Lecciones: el USB débil de la laptop causaba brownout y cortes de WiFi; fuente siempre a 5V/VIN,
  nunca a 3V3 (así se dañó una DevKit). Antes de subir firmware, desconectar el TX del GPS (GPIO3).
- Pendiente: prueba de navegación en la mesa y luego prueba real en la calle (power bank + hotspot);
  alimentación con baterías (BMS 2S + LM2596S + capacitor 1000 µF); placa y carcasa (compañero).

## Estado (6 nov 2026) — reemplaza lo anterior donde se contradiga
- La guía completa y al día está en `AGENTS.md` (también la leen OpenCode y Codex).
- Hecho: navegación probada en la placa (modo demo), modo guía que narra el camino, peligros cercanos
  con tres pulsos y prioridad en la voz, SOS que se reintenta sin conexión, red de peligros que ahora
  también reporta, app y alertas en español, entorno `demo` en platformio.ini, README corregido.
- La placa quedó con el firmware real (`esp32cam`) y se entrega para soldar.
- Pendientes: ver la sección "Pendientes" de `AGENTS.md`.
