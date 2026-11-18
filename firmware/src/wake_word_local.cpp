// Detección local de "visión" con el modelo de Edge Impulse. Ver wake_word_local.h.
#include "wake_word_local.h"

#ifdef WAKE_WORD_LOCAL

// Librería Arduino exportada desde Edge Impulse (Deployment -> Arduino library), descomprimida en
// firmware/lib/. El nombre sale del nombre del proyecto en Edge Impulse: si el proyecto se llama
// distinto de "halo_vision_kws", cambiar esta línea (y lib_ignore en platformio.ini).
#include <halo_vision_kws_inferencing.h>

#if EI_CLASSIFIER_FREQUENCY != 16000
#error "El modelo debe entrenarse a 16 kHz: el buffer wake_word_buffer del firmware es de 16 kHz"
#endif

// Ventana que se está clasificando (la lee get_window_data mientras corre run_classifier)
static const int16_t* s_window = nullptr;

static int get_window_data(size_t offset, size_t length, float* out_ptr) {
    // Edge Impulse espera las muestras crudas como float (sin normalizar)
    return ei::numpy::int16_to_float(s_window + offset, out_ptr, length);
}

static int s_labelIndex = -1;
static bool s_history[WAKE_LOCAL_HISTORY] = {false};
static int s_historyPos = 0;
static unsigned long s_lastDetection = 0;
static uint32_t s_lastInferenceMs = 0;

// Busca la clase WAKE_WORD_LABEL entre los resultados (las etiquetas vienen del modelo)
static int findLabel(const ei_impulse_result_t& result) {
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].label && strcmp(result.classification[i].label, WAKE_WORD_LABEL) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool wakeWordLocalBegin() {
    // Primera clasificacion con un segundo de silencio: comprueba que el modelo corre, que tiene la
    // clase "vision" y mide cuanto tarda en esta placa.
    int16_t* silence = (int16_t*)ps_calloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT, sizeof(int16_t));
    if (!silence) {
        Serial.println("❌ [WakeLocal] Sin PSRAM para la prueba inicial del modelo");
        return false;
    }
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &get_window_data;
    s_window = silence;
    ei_impulse_result_t result = {0};
    unsigned long t0 = millis();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    s_lastInferenceMs = millis() - t0;
    s_window = nullptr;
    free(silence);

    if (err != EI_IMPULSE_OK) {
        Serial.printf("❌ [WakeLocal] El modelo no corre (error %d)\n", (int)err);
        return false;
    }
    s_labelIndex = findLabel(result);
    if (s_labelIndex < 0) {
        Serial.printf("❌ [WakeLocal] El modelo no tiene la clase \"%s\"\n", WAKE_WORD_LABEL);
        return false;
    }

    Serial.printf("🧠 [WakeLocal] Modelo listo: %d clases, ventana de %d muestras (%d ms) a %d Hz, "
                  "%u ms por clasificacion (DSP %d ms + red %d ms)\n",
                  (int)EI_CLASSIFIER_LABEL_COUNT, (int)EI_CLASSIFIER_RAW_SAMPLE_COUNT,
                  (int)(EI_CLASSIFIER_RAW_SAMPLE_COUNT * 1000 / EI_CLASSIFIER_FREQUENCY),
                  (int)EI_CLASSIFIER_FREQUENCY, s_lastInferenceMs,
                  (int)result.timing.dsp, (int)result.timing.classification);
    Serial.printf("🧠 [WakeLocal] Umbral %.2f, %d de %d ventanas cada %d ms\n", WAKE_LOCAL_THRESHOLD,
                  WAKE_LOCAL_MIN_HITS, WAKE_LOCAL_HISTORY, WAKE_LOCAL_HOP_MS);
    if (s_lastInferenceMs > WAKE_LOCAL_HOP_MS) {
        Serial.printf("⚠️ [WakeLocal] Clasificar tarda mas que WAKE_LOCAL_HOP_MS (%d ms): se saltaran ventanas\n",
                      WAKE_LOCAL_HOP_MS);
    }
    wakeWordLocalReset();
    return true;
}

size_t wakeWordLocalWindowSamples() {
    return EI_CLASSIFIER_RAW_SAMPLE_COUNT;
}

float wakeWordLocalScore(const int16_t* window, size_t samples) {
    if (!window || samples != EI_CLASSIFIER_RAW_SAMPLE_COUNT || s_labelIndex < 0) {
        return -1.0f;
    }

    signal_t signal;
    signal.total_length = samples;
    signal.get_data = &get_window_data;
    s_window = window;

    // La memoria del DSP y del tensor arena se pide con malloc: main.cpp activa
    // heap_caps_malloc_extmem_enable(256), así que va a la PSRAM y no le quita RAM interna a TLS/WiFi.
    ei_impulse_result_t result = {0};
    unsigned long t0 = millis();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    s_lastInferenceMs = millis() - t0;
    s_window = nullptr;

    if (err != EI_IMPULSE_OK) {
        Serial.printf("❌ [WakeLocal] run_classifier falló (%d)\n", (int)err);
        return -1.0f;
    }
    return result.classification[s_labelIndex].value;
}

bool wakeWordLocalUpdate(float score) {
    s_history[s_historyPos] = (score >= WAKE_LOCAL_THRESHOLD);
    s_historyPos = (s_historyPos + 1) % WAKE_LOCAL_HISTORY;

    if (s_lastDetection != 0 && millis() - s_lastDetection < WAKE_LOCAL_COOLDOWN_MS) {
        return false;
    }

    int hits = 0;
    for (int i = 0; i < WAKE_LOCAL_HISTORY; i++) {
        if (s_history[i]) hits++;
    }
    if (hits >= WAKE_LOCAL_MIN_HITS) {
        s_lastDetection = millis();
        for (int i = 0; i < WAKE_LOCAL_HISTORY; i++) s_history[i] = false;
        return true;
    }
    return false;
}

void wakeWordLocalReset() {
    for (int i = 0; i < WAKE_LOCAL_HISTORY; i++) s_history[i] = false;
    s_historyPos = 0;
}

uint32_t wakeWordLocalLastInferenceMs() {
    return s_lastInferenceMs;
}

#endif  // WAKE_WORD_LOCAL
