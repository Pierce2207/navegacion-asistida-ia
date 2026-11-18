#ifndef WAKE_WORD_LOCAL_H
#define WAKE_WORD_LOCAL_H

// Detección LOCAL de la palabra "visión" con un modelo TinyML de Edge Impulse.
//
// Se activa compilando con -DWAKE_WORD_LOCAL (entorno "wake-local" de platformio.ini). Sin ese
// #define el firmware usa la búsqueda en Deepgram de siempre. Si el modelo no se puede iniciar
// (por ejemplo, sin PSRAM), main.cpp vuelve solo a Deepgram.
//
// El modelo lee la misma señal que antes iba a Deepgram: el último segundo del buffer circular
// wake_word_buffer (PCM 16 bit, 16 kHz, mono). Ver docs/wake-word/README.md para entrenarlo.

#include <Arduino.h>

// Ajustes (se pueden cambiar con -D en platformio.ini sin tocar el código)
#ifndef WAKE_WORD_LABEL
#define WAKE_WORD_LABEL "vision"          // nombre exacto de la clase en Edge Impulse
#endif
#ifndef WAKE_LOCAL_THRESHOLD
#define WAKE_LOCAL_THRESHOLD 0.80f        // probabilidad mínima de "vision" en una ventana
#endif
#ifndef WAKE_LOCAL_HOP_MS
#define WAKE_LOCAL_HOP_MS 250             // cada cuánto se clasifica el último segundo
#endif
#ifndef WAKE_LOCAL_MIN_HITS
#define WAKE_LOCAL_MIN_HITS 2             // ventanas sobre el umbral necesarias...
#endif
#ifndef WAKE_LOCAL_HISTORY
#define WAKE_LOCAL_HISTORY 3              // ...dentro de las últimas N (2 de 3 = ~0.5 s de "visión")
#endif
#ifndef WAKE_LOCAL_COOLDOWN_MS
#define WAKE_LOCAL_COOLDOWN_MS 1500       // tras una detección, ignora las ventanas siguientes
#endif

#ifdef WAKE_WORD_LOCAL

// Prepara el clasificador. false si el modelo no calza con el audio del firmware (16 kHz).
bool wakeWordLocalBegin();

// Muestras que necesita una ventana (16000 = 1 s a 16 kHz para el modelo recomendado).
size_t wakeWordLocalWindowSamples();

// Clasifica una ventana (en orden cronológico, wakeWordLocalWindowSamples() muestras).
// Devuelve la probabilidad de WAKE_WORD_LABEL (0..1), o -1 si hubo un error.
float wakeWordLocalScore(const int16_t* window, size_t samples);

// Suavizado: recibe el puntaje de cada ventana y dice si cuenta como detección
// (WAKE_LOCAL_MIN_HITS de las últimas WAKE_LOCAL_HISTORY, fuera del cooldown).
bool wakeWordLocalUpdate(float score);

// Olvida el historial (al empezar o terminar una grabación).
void wakeWordLocalReset();

// Milisegundos que tardó la última clasificación (DSP + red), para el monitor serie.
uint32_t wakeWordLocalLastInferenceMs();

#endif  // WAKE_WORD_LOCAL
#endif  // WAKE_WORD_LOCAL_H
