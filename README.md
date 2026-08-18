# Sistema portátil de navegación asistida con IA


## Descripción

Este proyecto parte del hardware y firmware de **Halo**, un asistente portátil que ayuda a personas con baja visión a moverse con más seguridad usando cámara en vivo, IA (Gemini Live), reconocimiento y síntesis de voz, y GPS:

- Firmware original: [ilikecandy/SSCS-2025-Arduino](https://github.com/ilikecandy/SSCS-2025-Arduino)
- Backend original: [StockerMC/SSCS-2025-API](https://github.com/StockerMC/SSCS-2025-API)
- App original: [StockerMC/SSCS-2025-App](https://github.com/StockerMC/SSCS-2025-App)
- Desarrollado por Aayan Karmali y Jacob Tian (Universidad de Waterloo), 1er lugar en el IEEE SSCS Arduino Contest 2025.

Sobre esa base, nuestro equipo añade dos componentes propios:

1. **Retroalimentación háptica direccional** (`firmware/haptic/`) — 3 motores de vibración (izquierda/centro/derecha) que avisan peligros sin depender del canal de voz, activados por las alertas de Gemini y por la red de peligros compartidos.
2. **Red comunitaria de peligros compartidos** (`api/hazards.js`, `sql/hazards_schema.sql`) — los peligros detectados por un dispositivo, una vez confirmados por varios usuarios independientes, se comparten con otros dispositivos Halo cercanos antes de que su propia cámara los detecte.

Ver `docs/` para la guía de construcción completa (lista de materiales, conexiones, costos) y el detalle de diseño de ambas mejoras.

## Estructura del repositorio

```
firmware/     código del ESP32 (PlatformIO) + módulo háptico nuevo
api/          backend serverless (Vercel) + endpoint de peligros compartidos
sql/          esquema de base de datos para la red de peligros (Supabase/PostGIS)
app/          app móvil de acompañamiento (Expo/React Native)
docs/         guía de construcción y ficha de inscripción del proyecto
```

## Cómo correrlo

Ver `docs/HaloPlus_Guia_de_Construccion.docx`, secciones 4 a 6, para el detalle de configuración de firmware, backend y app. Para la red de peligros compartidos:

1. Ejecutar `sql/hazards_schema.sql` en el SQL Editor de Supabase (requiere la extensión PostGIS, se activa en la primera línea del script).
2. `api/hazards.js` usa las mismas variables de entorno que el resto del backend (`SUPABASE_URL`, `SUPABASE_ANON_KEY`).

## Licencia

Este repositorio construye sobre proyectos de código abierto de terceros (ver enlaces arriba). El código propio de este equipo (`firmware/haptic/`, `api/hazards.js`, `sql/`) se distribuye bajo los mismos términos que el proyecto base, salvo que el equipo indique lo contrario.
ta bien esta prueba :V