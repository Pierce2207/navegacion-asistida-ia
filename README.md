
# MURDOCK: Asistente portátil de navegación con inteligencia artificial

## Descripción

**MURDOCK** es un asistente portátil de navegación con inteligencia artificial multimodal, diseñado para apoyar la movilidad y orientación de personas con baja visión.

El dispositivo integra una ESP32-CAM, reconocimiento y síntesis de voz, navegación GPS, retroalimentación háptica direccional y un sistema de comunicación de emergencias. Además, incorpora una aplicación móvil para acompañantes y una red comunitaria de peligros georreferenciados.

Su arquitectura combina sistemas embebidos, procesamiento de imágenes y audio mediante inteligencia artificial, servicios en la nube y herramientas de navegación basadas en OpenStreetMap.

MURDOCK busca proporcionar información accesible del entorno mediante voz y vibraciones, permitiendo que el usuario mantenga las manos libres durante sus desplazamientos. Se plantea como una herramienta complementaria al bastón blanco y otros medios de asistencia.

## Funcionalidades principales

### 1. Visión asistida mediante inteligencia artificial

- Captura de imágenes del entorno mediante la cámara OV2640 de la ESP32-CAM.
- Procesamiento multimodal de imágenes y consultas habladas mediante Gemini Live.
- Interpretación de obstáculos, objetos y elementos del entorno.
- Respuestas habladas en español mediante Deepgram.
- Tiempo de respuesta observado de aproximadamente 2 segundos durante las pruebas de banco.
- Activación mediante botón o reconocimiento de la palabra "Visión".
- Desarrollo experimental de reconocimiento local con Edge Impulse, pendiente de validación en el dispositivo.

### 2. Detección de peligros y retroalimentación háptica

- Identificación de obstáculos mediante inteligencia artificial.
- Tres motores vibratorios: izquierdo, central y derecho.
- Alertas direccionales según la ubicación del peligro.
- Tres pulsos cortos para advertir obstáculos.
- Prioridad de las alertas de peligro sobre otras indicaciones de voz.
- Control de motores mediante el expansor de entradas y salidas PCF8574.

### 3. Navegación peatonal inteligente

- Posicionamiento mediante GPS NEO-6M.
- Búsqueda de destinos mediante Nominatim.
- Cálculo de rutas peatonales utilizando OSRM y OpenStreetMap.
- Navegación giro a giro con instrucciones habladas en español.
- Vibraciones direccionales durante los giros.
- Recálculo de rutas cuando el usuario se desvía.
- Consulta de ubicación actual y cancelación de navegación.
- Modo de demostración con GPS simulado para pruebas en interiores.

### 4. Sistema de alertas SOS

- Activación de emergencia mediante cinco pulsaciones del botón.
- Confirmación háptica mediante los tres motores vibratorios.
- Registro de eventos de emergencia en el backend.
- Envío de notificaciones a la aplicación del acompañante.
- Integración con Firebase Cloud Messaging.
- Inclusión de información de ubicación en las alertas.
- Reintentos automáticos cuando no existe conectividad.

### 5. Red comunitaria de peligros

- Registro de obstáculos georreferenciados.
- Almacenamiento de categoría, ubicación y nivel de confianza.
- Identificación de reportes coincidentes mediante consultas geoespaciales.
- Uso de PostgreSQL y PostGIS en Supabase.
- Verificación de peligros mediante tres reportes coincidentes.
- Consulta de peligros verificados cercanos a la ubicación del dispositivo.
- Avisos anticipados por voz y vibración.
- Visualización de peligros mediante mapas Leaflet y OpenStreetMap.

### 6. Aplicación móvil MURDOCK

Aplicación desarrollada con Expo y React Native para familiares y acompañantes.

Funciones principales:

- Recepción y visualización de alertas SOS.
- Consulta de fecha, hora, ubicación y prioridad de las emergencias.
- Gestión de alertas activas, atendidas y resueltas.
- Visualización de peligros cercanos.
- Configuración del dispositivo.
- Ajustes de idioma, sensibilidad y retroalimentación háptica.
- Configuración de notificaciones y comportamiento del botón de emergencia.

### 7. Conectividad y servicios en la nube

- Conectividad WiFi de 2,4 GHz.
- Cambio automático entre la red configurada y el punto de acceso del celular.
- Comunicación mediante HTTPS y WebSocket.
- Backend serverless desarrollado con Node.js y desplegado en Vercel.
- Base de datos Supabase con PostgreSQL y PostGIS.
- Integración con Gemini Live, Deepgram y Firebase.
- Gestión de rutas, alertas, peligros y configuraciones del dispositivo.

### 8. Hardware y alimentación

- ESP32-CAM AI-Thinker con cámara OV2640.
- Micrófono digital INMP441.
- Amplificador MAX98357A y altavoz.
- GPS NEO-6M.
- Expansor PCF8574 mediante I2C.
- Tres motores vibratorios.
- Botón multifunción para comandos y emergencias.
- Dos baterías recargables 18650.
- Sistema de protección BMS 2S.
- Regulador LM2596 ajustado a 5 V.
- Sistema de recarga mediante USB-C.
- Componentes electrónicos montados y soldados sobre placa perforada.
- Carcasa diseñada y fabricada mediante impresión 3D.

## Estructura del repositorio

```text
firmware/     Firmware ESP32-CAM en PlatformIO y control háptico
api/          Backend serverless desplegado en Vercel
sql/          Esquema y funciones de base de datos Supabase/PostGIS
app/          Aplicación móvil Expo y React Native
docs/         Documentación de construcción, conexiones y demostración
```

## Cómo ejecutarlo

Consultar la documentación disponible en `docs/` para los detalles de configuración, montaje y conexiones electrónicas.

### Firmware

El firmware está desarrollado en C++ utilizando Arduino y PlatformIO.

Compilación y carga con GPS real:

```bash
pio run -e esp32cam -t upload
```

Compilación y carga en modo demostración:

```bash
pio run -e demo -t upload
```

Antes de cargar el firmware, desconectar temporalmente el TX del GPS del GPIO3, ya que comparte el pin con la programación.

Si `pio` no se reconoce, utilizar:

```text
%USERPROFILE%\.platformio\penv\Scripts\pio.exe
```

Las credenciales se configuran en `firmware/src/secrets.cpp`, utilizando `secrets.cpp.example` como plantilla. No deben publicarse claves de acceso en el repositorio.

### Aplicación móvil

Desde la carpeta `app/`:

```bash
npx expo start --dev-client
```

Para generar un APK de Android:

```bash
eas build -p android --profile preview
```

### Backend

El backend se despliega automáticamente en Vercel con cada actualización de la rama `main`.

### Red de peligros

1. Ejecutar `sql/hazards_schema.sql` desde el SQL Editor de Supabase, con la extensión PostGIS habilitada.
2. Configurar las variables de entorno `SUPABASE_URL` y `SUPABASE_ANON_KEY`.
3. Utilizar los endpoints del backend para registrar y consultar los peligros georreferenciados.

## Estado del proyecto

MURDOCK cuenta con un prototipo electrónico soldado, sistema de alimentación recargable, carcasa impresa en 3D, firmware, aplicación móvil y servicios backend.

Se han realizado pruebas funcionales de interacción por voz, navegación con GPS simulado, activación de alertas SOS y consultas a la red comunitaria de peligros.

Entre las mejoras pendientes se encuentran la validación en recorridos reales con usuarios, la medición de autonomía, la validación del reconocimiento local mediante Edge Impulse y el perfeccionamiento de la verificación de reportes entre dispositivos diferentes.

## Licencia

Este repositorio integra componentes de código abierto de terceros junto con desarrollos del equipo MURDOCK. Se deben conservar los avisos de atribución y cumplir los términos de las licencias correspondientes.

## Créditos y atribuciones

MURDOCK reutiliza componentes del proyecto **Halo**, desarrollado por Aayan Karmali y Jacob Tian, de la Universidad de Waterloo, ganadores del IEEE SSCS Arduino Contest 2025. Repositorios originales: [Firmware](https://github.com/ilikecandy/SSCS-2025-Arduino) · [Backend](https://github.com/StockerMC/SSCS-2025-API) · [Aplicación](https://github.com/StockerMC/SSCS-2025-App).
