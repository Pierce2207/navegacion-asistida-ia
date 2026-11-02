# SSCS-2025-Arduino
🏆 1st Place, IEEE SSCS Arduino Contest 2025 (Post-Secondary)

Meet Halo | IEEE SSCS Arduino Contest 2025
https://www.youtube.com/watch?v=oZpIQhcpVok&t=1s

Halo is a wearable smart assistant that helps people with vision loss navigate the world safely using live video, AI, and natural voice interaction. It detects hazards in real time, answers questions with contextual awareness, supports over 130 languages, and connects to a mobile companion app for alerts and settings.

Made by Aayan Karmali and Jacob Tian, representing the University of Waterloo.

Hardware:
- ESP32-Wrover-E CAM board
- GY-NEO6MV2 GPS module
- INMP441 microphone
- MAX98357A speaker amplifier
- Push button for SOS/push-to-talk
- LED for areas with low light to assist with vision and Halo functionality
- Battery with power switch

Software:
- Arduino source code: github.com/ilikecandy/SSCS-2025-Arduino
- Live video analysis via Gemini Live API through WebSocket connections
- Speech-to-text and wake word detection via Deepgram API
- Multilingual text-to-speech via Deepgram and Google Translate API (130+ languages)
- Navigation & place information via Google Maps Platform API
- Supabase for realtime alerts and preferences (language, wake word, etc.)
- Firebase Cloud Messaging for push notifications
- Vercel API backend for communication between the ESP32, the database, and the app
- Source code: github.com/StockerMC/SSCS-2025-API
- Companion app built with Expo and React Native for managing settings, reviewing alerts, and SOS notifications
- Source code: github.com/StockerMC/SSCS-2025-App

Design & Prototyping:
- Interactive design: p3d.in/jKlt1
- Developed through five hardware iterations: from a breadboard prototype to a 3D-printed wearable model with a neck strap design
- Improvements included a camera cover for privacy, better strap comfort, dedicated space for components and ports, and integrated battery housing

## Conexiones para la AI-Thinker ESP32-CAM (OV3660)

El firmware está configurado para la **AI-Thinker ESP32-CAM** (`board = esp32cam`).
No usar tarjeta microSD: comparte los pines 2, 4, 12, 13, 14 y 15.

| Módulo | Pin del módulo | ESP32-CAM |
|---|---|---|
| Mic INMP441 | SCK / WS / SD | GPIO 14 / 13 / 2 |
| | L/R, GND / VDD | GND / 3.3V |
| MAX98357A | BCLK / LRC / DIN | GPIO 12 / 13 / 2 (LRC y DIN compartidos con el mic) |
| | VIN / GND | 5V / GND |
| GPS (NEO-6M) | TX | GPIO 3 (U0R) — desconectar al flashear |
| | RX | sin conectar |
| PCF8574 (0x20) | SDA / SCL | GPIO 15 / GPIO 4 |
| | VCC / GND | 3.3V (no 5V) / GND |
| | P0 / P1 / P2 | motores izquierda / centro / derecha (vía PNP, activo en LOW) |
| | P3 | botón push-to-talk/SOS a GND |
| Monitor serie | — | GPIO 1 (U0T), 115200 baudios |

Notas:
- Alimentar con 5V / 2A por el pin 5V y poner un condensador de 470–1000 µF entre 5V y GND.
- GPIO 12 debe estar en bajo al arrancar; si la placa no bootea, desconectar el BCLK del MAX y probar.
- GPIO 4 también maneja el LED flash: puede parpadear tenue con el tráfico I2C.
