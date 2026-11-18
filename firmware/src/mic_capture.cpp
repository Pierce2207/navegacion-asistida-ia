// Firmware mínimo para GRABAR MUESTRAS DE ENTRENAMIENTO con el micrófono real de la placa
// (INMP441) y mandarlas a la PC por USB. Así el modelo de "visión" aprende con el mismo micrófono,
// la misma ganancia y el mismo ruido que va a oír en el aparato.
//
//   Subir:    pio run -e mic-capture -t upload     (desconectar antes el TX del GPS de GPIO3)
//   Grabar:   python scripts/kws/capturar_esp32.py --puerto COM5 --salida vision_sebas.wav
//   Volver:   pio run -e esp32cam -t upload
//
// Solo se compila en el entorno mic-capture (build_src_filter en platformio.ini).
// Protocolo: bloques "AUD0" + uint16 (cantidad de muestras) + muestras int16 little-endian,
// 16 kHz mono, convertidas igual que process_audio() en main.cpp (>>14, ganancia x2, recorte).
#ifdef MIC_CAPTURE_SERIAL

#include <Arduino.h>
#include "microphone.h"

static const int BLOCK_SAMPLES = 512;
static int32_t raw_buffer[BLOCK_SAMPLES];
static int16_t pcm_buffer[BLOCK_SAMPLES];

void setup() {
    Serial.setTxBufferSize(16384);
    Serial.begin(921600);  // 16 kHz x 16 bit = 32 KB/s; a 115200 baudios no alcanza
    delay(500);
    Serial.println("\n[mic-capture] Iniciando microfono...");
    while (!setup_microphone()) {
        Serial.println("[mic-capture] Microfono no responde, reintentando...");
        delay(1000);
    }
    delay(1000);  // el INMP441 arranca con ruido; se descarta
    Serial.println("[mic-capture] Transmitiendo audio (bloques AUD0)");
}

void loop() {
    size_t bytes_read = 0;
    if (read_microphone_data(raw_buffer, sizeof(raw_buffer), &bytes_read) != ESP_OK || bytes_read == 0) {
        return;
    }
    uint16_t n = bytes_read / sizeof(int32_t);
    for (int i = 0; i < n; i++) {
        int32_t sample = (raw_buffer[i] >> 14) * 2;  // misma conversion que process_audio()
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        pcm_buffer[i] = (int16_t)sample;
    }
    Serial.write((const uint8_t*)"AUD0", 4);
    Serial.write((const uint8_t*)&n, sizeof(n));
    Serial.write((const uint8_t*)pcm_buffer, n * sizeof(int16_t));
}

#endif  // MIC_CAPTURE_SERIAL
