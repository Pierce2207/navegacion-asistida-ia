# Cómo conectar el módulo háptico al firmware existente

`haptic_driver.h/.cpp` vive en `firmware/lib/haptic/` (convención de PlatformIO: cualquier
carpeta dentro de `lib/` se compila e incluye automáticamente, sin rutas relativas ni
configuración extra en `platformio.ini`).

**Estado: ya integrado en `main.cpp`, `gemini_config.h` y `secrets.h`/`secrets.cpp.example`.**
Esta guía documenta qué se hizo y por qué, para quien retome el código.

## 1. En `main.cpp`

```cpp
#include "haptic_driver.h" // no hace falta "haptic/" al prefijo, PlatformIO ya expone lib/haptic/

HapticDriver haptics;
bool hapticsAvailable = false;

void setup() {
    // ... codigo original (setup_microphone, visionAssistant.initialize(), etc.) ...

    // GPIO0/GPIO3: unicos pines libres, ya que camera_pins.h usa casi todo el resto
    // y gps_module.h usa 32/33. GPIO0 es pin de boot-strapping - riesgo aceptado,
    // ver guia de construccion seccion 8.1. Si el boot se vuelve poco confiable,
    // considerar un coprocesador aparte (ej. un ATtiny/segundo ESP32 mas simple).
    hapticsAvailable = haptics.initialize(/*sda=*/0, /*scl=*/3);
}

void loop() {
    // ... codigo original ...
    if (hapticsAvailable) haptics.update(); // apaga los motores cuyo pulso ya termino
}
```

## 2. Fuente 1 — alertas de Gemini (`obstacle_alert`)

En `gemini_config.h`, el `TOOLS_JSON` de `systemAction` ya tiene el campo `direction`
(propiedad opcional, string: `left`/`center`/`right`), y el `SYSTEM_PROMPT` ya le indica
a Gemini que lo incluya siempre que `intent=obstacle_alert`.

En `main.cpp`, dentro de `handleSystemAction()` (rama `intent == "obstacle_alert"`), se lee
`doc["direction"]` y se llama a `haptics.triggerDirection(...)`. No hizo falta tocar
`vision_assistant.cpp`: como `handleSystemAction` ya recibe el `JsonDocument` completo con
todos los argumentos que mandó Gemini, `direction` llega solo una vez que está en el schema.

## 3. Fuente 2 — red de peligros compartidos

`main.cpp` tiene una función `checkNearbyHazards()`, llamada en cada `loop()` con un
throttle propio de 5 segundos (independiente del `GPS_UPDATE_INTERVAL` de 1s de
`VisionAssistant`, para no saturar la API). Consulta
`GET {HAZARDS_API_URL}/api/hazards?lat=..&lon=..&radius=100` (ver `api/hazards.js`) y,
si el peligro verificado más cercano está a menos de 15m, dispara un pulso háptico.

**Limitación conocida:** este firmware no tiene brújula/magnetómetro. El curso de
`TinyGPS++` (`course-over-ground`) solo es confiable con movimiento sostenido y no se
expone en `GPSData` (ver `gps_module.h`), así que a diferencia de las alertas de Gemini
(que sí saben de qué lado del encuadre viene el peligro), la red de peligros compartidos
**siempre dispara `MOTOR_CENTER`** como aviso general de "peligro cerca", en vez de
calcular izquierda/derecha por rumbo GPS.

Si más adelante se agrega un magnetómetro (ej. QMC5883L/HMC5883L), reemplazar el trigger
de `checkNearbyHazards()` con el cálculo de rumbo original:

```cpp
// pseudocodigo, una vez que haya una fuente de heading confiable:
float bearingUsuario = compass.getHeading();
float bearingPeligro = calculateBearing(gpsActual, hazard.location); // formula de Haversine,
                                                                       // igual que VisionAssistant::calculateDistance
float diff = normalizeAngle(bearingPeligro - bearingUsuario);

HazardDirection d;
if (diff > -30 && diff < 30) d = HazardDirection::CENTER;
else if (diff <= -30) d = HazardDirection::LEFT;
else d = HazardDirection::RIGHT;
```

## Variables de entorno / secretos nuevos

`secrets.h` y `secrets.cpp.example` ya declaran `HAZARDS_API_URL`. Falta que cada quien
cree su propio `firmware/src/secrets.cpp` (gitignored, nunca se sube) con el valor real:

```cpp
const char* HAZARDS_API_URL = "https://navegacion-asistida-ia.vercel.app";
```
