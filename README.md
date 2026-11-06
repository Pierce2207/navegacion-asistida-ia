# Sistema portátil de navegación asistida con IA


## Descripción

Este proyecto parte del hardware y firmware de **Halo**, un asistente portátil que ayuda a personas con baja visión a moverse con más seguridad usando cámara en vivo, IA (Gemini Live), reconocimiento y síntesis de voz, y GPS:

- Firmware original: [ilikecandy/SSCS-2025-Arduino](https://github.com/ilikecandy/SSCS-2025-Arduino)
- Backend original: [StockerMC/SSCS-2025-API](https://github.com/StockerMC/SSCS-2025-API)
- App original: [StockerMC/SSCS-2025-App](https://github.com/StockerMC/SSCS-2025-App)
- Desarrollado por Aayan Karmali y Jacob Tian (Universidad de Waterloo), 1er lugar en el IEEE SSCS Arduino Contest 2025.

Sobre esa base, nuestro equipo añade dos componentes propios:

1. **Retroalimentación háptica direccional** (`firmware/lib/haptic/`) — 3 motores de vibración (izquierda/centro/derecha) que avisan peligros sin depender del canal de voz, activados por las alertas de Gemini y por la red de peligros compartidos.
2. **Red comunitaria de peligros compartidos** (`api/hazards.js`, `sql/hazards_schema.sql`) — los peligros detectados por un dispositivo, una vez confirmados por varios usuarios independientes, se comparten con otros dispositivos Halo cercanos antes de que su propia cámara los detecte.

Ver `docs/` para la guía de construcción completa (lista de materiales, conexiones, costos) y el detalle de diseño de ambas mejoras.

## Qué hace el aparato

- **Voz:** mantener el botón y hablar (o decir "Visión, …"). La pregunta va como audio a Gemini Live y la respuesta llega en ~1 s, en español (voz de Deepgram).
- **Peligros con la cámara:** si algo está en el camino y cerca, vibra tres pulsos en ese lado (izquierda, centro o derecha) y dice "Cuidado, …". El aviso tiene prioridad sobre cualquier otra frase.
- **Navegación a pie (OpenStreetMap):** "¿dónde estoy?", "llévame a la Plaza de Armas", "cancela la ruta". Guía giro a giro con voz y un pulso largo del lado del giro; recalcula si el usuario se desvía. Mientras hay ruta, narra el camino ("acera libre, pared a tu derecha").
- **SOS:** 5 toques al botón → notificación en el celular del acompañante ("SOS: necesita ayuda"). Si no hay internet, se reintenta cada 10 s.
- **Red de peligros compartidos:** los peligros fijos que ve un aparato (hueco, escalón, poste, obra) se reportan con su GPS; al confirmarse con 3 reportes se avisan a los demás usuarios al pasar cerca.
- **WiFi:** red de casa y, como respaldo, el hotspot del celular (2.4 GHz); cambia sola si una red falla.

## Estructura del repositorio

```
firmware/     código del ESP32 (PlatformIO) + módulo háptico nuevo
api/          backend serverless (Vercel) + endpoint de peligros compartidos
sql/          esquema de base de datos para la red de peligros (Supabase/PostGIS)
app/          app móvil de acompañamiento (Expo/React Native)
docs/         guía de construcción, hoja de conexiones (esquema-conexiones.html) y guion del video
```

## Cómo correrlo

Ver `docs/HaloPlus_Guia_de_Construccion.docx`, secciones 4 a 6, para el detalle de configuración de firmware, backend y app, y `docs/esquema-conexiones.html` para las conexiones pin por pin.

**Firmware** (carpeta `firmware/`, PlatformIO). Hay dos versiones:

```
pio run -e esp32cam -t upload   # real, con GPS
pio run -e demo -t upload       # GPS simulado en la Plaza San Martín (para exponer en interiores)
```

Antes de subir, desconectar el TX del GPS de GPIO3 (comparte el pin con la programación) y conectarlo después. Si `pio` no se reconoce, usar `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`. Las claves van en `firmware/src/secrets.cpp` (plantilla: `secrets.cpp.example`; nunca subirlo a git).

**App** (carpeta `app/`): `npx expo start --dev-client` con el celular en la misma WiFi; para un APK, `eas build -p android --profile preview`.

**Backend:** se despliega solo en Vercel con cada push a `main`.

Para la red de peligros compartidos:

1. Ejecutar `sql/hazards_schema.sql` en el SQL Editor de Supabase (requiere la extensión PostGIS, se activa en la primera línea del script).
2. `api/hazards.js` usa las mismas variables de entorno que el resto del backend (`SUPABASE_URL`, `SUPABASE_ANON_KEY`).

## Licencia

Este repositorio construye sobre proyectos de código abierto de terceros (ver enlaces arriba). El código propio de este equipo (`firmware/lib/haptic/`, `api/hazards.js`, `sql/`) se distribuye bajo los mismos términos que el proyecto base, salvo que el equipo indique lo contrario.
