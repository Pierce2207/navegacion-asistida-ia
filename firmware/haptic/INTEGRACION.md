# Cómo conectar el módulo háptico al firmware existente

`haptic_driver.h/.cpp` es independiente — no modifica nada del firmware original. Estos son los tres puntos donde hay que llamarlo.

## 1. En `main.cpp`

```cpp
#include "haptic/haptic_driver.h"

HapticDriver haptics;

void setup() {
    // ... codigo original (setup_microphone, visionAssistant.initialize(), etc.) ...

    // Pines libres sugeridos en la guia de construccion (seccion 8.1):
    // usar un coprocesador aparte, o GPIO0/GPIO3 si se asume el riesgo del boot pin.
    haptics.initialize(/*sda=*/0, /*scl=*/3);
}

void loop() {
    // ... codigo original ...
    haptics.update(); // apaga los motores cuyo pulso ya termino
}
```

## 2. Fuente 1 — alertas de Gemini (`obstacle_alert`)

En `gemini_config.h`, el `TOOLS_JSON` de `systemAction` necesita un campo nuevo `direction` para que Gemini indique de que lado del encuadre viene el peligro:

```cpp
// dentro de TOOLS_JSON, en las properties de systemAction:
"direction": {
  "type": "string",
  "description": "Solo si intent=obstacle_alert: de que lado viene el peligro. Valores: left, center, right."
}
```

Y en el `SYSTEM_PROMPT`, agregar a la seccion de DANGER DETECTION:

```
- Incluye siempre 'direction' (left/center/right) cuando intent=obstacle_alert, segun en que parte del encuadre aparece el objeto.
```

Luego, donde el firmware ya procesa el `toolCall` de Gemini (en `vision_assistant.cpp`, dentro de `handleWebSocketMessage`, junto al `toolCallback` existente), se lee ese campo y se llama al driver:

```cpp
// junto al manejo existente de "systemAction" en el toolCall
if (strcmp(toolName, "systemAction") == 0) {
    JsonObjectConst args = funcCall["args"];
    const char* intent = args["intent"];
    if (intent && strcmp(intent, "obstacle_alert") == 0 && args.containsKey("direction")) {
        const char* dir = args["direction"];
        HazardDirection d = HazardDirection::UNKNOWN;
        if (strcmp(dir, "left") == 0) d = HazardDirection::LEFT;
        else if (strcmp(dir, "center") == 0) d = HazardDirection::CENTER;
        else if (strcmp(dir, "right") == 0) d = HazardDirection::RIGHT;
        haptics.triggerDirection(d);
    }
    // ... resto del manejo original (toolCallback, sendToolResponse) ...
}
```

## 3. Fuente 2 — red de peligros compartidos

Cada vez que se actualiza el GPS (mismo ciclo que ya existe en `VisionAssistant::run()`, cada `GPS_UPDATE_INTERVAL`), se consulta `GET /api/hazards?lat=..&lon=..&radius=100` (ver `api/hazards.js`). Con la respuesta:

```cpp
// pseudocodigo: al recibir la lista de peligros verificados cercanos
for (hazard in peligrosCercanos) {
    float bearingUsuario = gps.getCourse();               // rumbo de viaje actual
    float bearingPeligro = calculateBearing(gpsActual, hazard.location);
    float diff = normalizeAngle(bearingPeligro - bearingUsuario);

    HazardDirection d;
    if (diff > -30 && diff < 30) d = HazardDirection::CENTER;
    else if (diff <= -30) d = HazardDirection::LEFT;
    else d = HazardDirection::RIGHT;

    if (hazard.distanceMeters < 15) {
        haptics.triggerDirection(d);
    }
}
```

`calculateBearing` se puede implementar igual que `VisionAssistant::calculateDistance` en `vision_assistant.cpp` (formula de Haversine), solo que devolviendo el angulo en vez de la distancia.
