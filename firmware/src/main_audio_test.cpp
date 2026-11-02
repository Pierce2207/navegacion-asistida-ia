// /*
//  * Audio Only Test Main (MAX98357A + altavoz)
//  *
//  * Prueba el amplificador y el altavoz SOLOS, sin camara, sin microfono,
//  * sin GPS y sin el modulo haptico. Solo necesitas: ESP32, el MAX98357A
//  * y el altavoz conectados.
//  *
//  * Como usarlo:
//  *   1. Renombra (o copia) temporalmente el main.cpp real a otro nombre
//  *      (ej. main_full.cpp.bak) para que PlatformIO no compile dos
//  *      funciones setup()/loop() a la vez.
//  *   2. Renombra este archivo de main_audio_test.cpp a main.cpp.
//  *   3. Compila y flashea (PlatformIO: Upload).
//  *   4. Cuando termines de probar, deshaz el paso 1 y 2 para volver
//  *      al firmware real.
//  *
//  * Conexiones (segun TTS.h / TTS.cpp, I2S_NUM_1):
//  *   MAX98357A BCLK  -> ESP32 GPIO 12
//  *   MAX98357A LRC   -> ESP32 GPIO 13
//  *   MAX98357A DIN   -> ESP32 GPIO 2
//  *   MAX98357A GND   -> GND
//  *   MAX98357A VIN   -> 3.3V (o 5V si tu modulo lo soporta, revisa su
//  *                      serigrafia; la mayoria de breakouts MAX98357A
//  *                      aceptan 2.5-5.5V)
//  *   Altavoz         -> salida +/- del MAX98357A
//  *
//  * Necesita WiFi para funcionar: TTS.speakText() llama a Deepgram (o al
//  * fallback gratuito de Google Translate TTS) por internet para generar
//  * el audio. Sin WiFi no va a sonar nada.
//  *
//  * Nota: TTS::playTone() esta deshabilitado en el codigo actual
//  * (tiene un "if (true) return;" al inicio), por eso esta prueba usa
//  * speakText() en vez de un tono simple.
//  */

// #include <Arduino.h>
// #include <WiFi.h>
// #include "TTS.h"
// #include "secrets.h"

// TTS tts;

// void setup() {
//     Serial.begin(115200);
//     delay(1000);

//     Serial.println("========================================");
//     Serial.println("Audio Test (MAX98357A + altavoz) - ESP32");
//     Serial.println("========================================");

//     Serial.printf("Conectando a WiFi: %s\n", WIFI_SSID);
//     WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

//     int attempts = 0;
//     while (WiFi.status() != WL_CONNECTED && attempts < 30) {
//         delay(500);
//         Serial.print(".");
//         attempts++;
//     }

//     if (WiFi.status() != WL_CONNECTED) {
//         Serial.println("\n❌ No se pudo conectar a WiFi. Revisa WIFI_SSID/WIFI_PASSWORD en secrets.cpp");
//         while (true) { delay(1000); }
//     }
//     Serial.println("\n✅ WiFi conectado, IP: " + WiFi.localIP().toString());

//     Serial.println("Inicializando TTS (Deepgram)...");
//     if (!tts.initialize(DEEPGRAM_API_KEY)) {
//         Serial.println("❌ TTS.initialize() fallo. Revisa DEEPGRAM_API_KEY en secrets.cpp");
//         while (true) { delay(1000); }
//     }
//     Serial.println("✅ TTS inicializado");

//     Serial.println("🔊 Reproduciendo frase de prueba...");
//     bool ok = tts.speakText("Hola, esto es una prueba del altavoz y el amplificador.");
//     Serial.println(ok ? "✅ speakText devolvio true (deberias haber escuchado algo)"
//                        : "❌ speakText devolvio false, revisa el cableado y el log de arriba");
// }

// void loop() {
//     // Repite la frase cada 15 segundos para poder probar volumen/cableado
//     // sin tener que reflashear cada vez.
//     static unsigned long lastSpeak = 0;
//     if (millis() - lastSpeak > 15000) {
//         lastSpeak = millis();
//         Serial.println("🔊 Repitiendo prueba de audio...");
//         tts.speakText("Probando otra vez.");
//     }
//     delay(100);
// }
