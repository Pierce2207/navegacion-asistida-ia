#include <Arduino.h>
#include <math.h>
#include <HTTPClient.h>
#include "vision_assistant.h"
#include "TTS.h"
#include "secrets.h"
#include "microphone.h"
#include "deepgram_client.h"
#include "settings_manager.h"
#include <ArduinoJson.h>
#include "haptic_driver.h"

VisionAssistant visionAssistant;
TTS tts;
DeepgramClient deepgramClient(DEEPGRAM_API_KEY);
SettingsManager settingsManager(NOTIFICATIONS_API_URL);
HapticDriver haptics;
bool ttsAvailable = false;
bool hapticsAvailable = false;
// The PCF8574 (motors + button) is now used from two tasks: the main loop and the button task
SemaphoreHandle_t i2cMutex = NULL;
volatile bool sos_requested = false;  // set by the button task, sent from the main loop

// GPS state for the shared hazard network check (separate from the places-API tracker)
GPSData last_hazard_check_gps;
unsigned long last_hazard_check_time = 0;

// Button for Push-to-Talk and SOS.
// En la AI-Thinker ESP32-CAM no quedan GPIO libres, asi que el boton va
// entre la entrada P3 del PCF8574 (el mismo expansor de los hapticos) y GND.
const uint8_t BUTTON_PCF_PIN = 3;

// Wake words (optimized for Deepgram's acoustic search - longer phrases work better)
const char* WAKE_WORDS[] = {
    // "vision" (no accent, plain ASCII for the URL) matches spoken Spanish "visión" with 1.0 confidence
    // in Deepgram's es search. "halo" was often heard as "aló"/"hello" and fired on unrelated words.
    "vision",
};
const int WAKE_WORDS_COUNT = sizeof(WAKE_WORDS) / sizeof(WAKE_WORDS[0]);

// Audio buffer for wake word detection (3 seconds)
const int WAKE_WORD_BUFFER_SECONDS = 3;
const int SAMPLE_RATE = 16000;
const int BITS_PER_SAMPLE = 16;
const int CHANNELS = 1;
const int WAKE_WORD_BUFFER_SIZE = WAKE_WORD_BUFFER_SECONDS * SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;

// Audio buffer for command recording (15 seconds maximum)
const int COMMAND_BUFFER_SECONDS = 15;
const int COMMAND_BUFFER_SIZE = COMMAND_BUFFER_SECONDS * SAMPLE_RATE * (BITS_PER_SAMPLE / 8) * CHANNELS;

uint8_t* wake_word_buffer = nullptr;
uint8_t* command_buffer = nullptr;
uint8_t* stt_temp_buffer = nullptr;
volatile int wake_word_buffer_index = 0;  // Made volatile for dual-core access
volatile int command_buffer_index = 0;    // For command recording
volatile bool is_recording = false;       // Made volatile for dual-core access
volatile bool wake_word_buffer_has_wrapped = false;  // Track if wake word buffer has wrapped around
volatile uint32_t buffer_sequence = 0;    // Sequence number to track buffer updates
volatile float baseline_audio_level = 0.0f; // Baseline audio level for silence detection
volatile bool baseline_calculated = false;  // Whether baseline has been calculated
// End-of-command detection state, reset by calculateBaselineAudioLevel() at the start of each recording
float noise_floor = 0.0f;       // quietest 300 ms window seen in this recording (AC RMS)
bool speech_detected = false;   // the user has started talking
int silent_windows = 0;         // consecutive quiet 200 ms checks after speech
volatile bool is_speaking = false; // Flag to prevent TTS overlap

// GPS and Places API
GPSData last_checked_gps_data;
unsigned long last_places_check_time = 0;

// Core synchronization
TaskHandle_t AudioTaskHandle = NULL;
SemaphoreHandle_t audioMutex;
QueueHandle_t commandQueue;
QueueHandle_t audioCommandQueue; // For sending commands to the audio task

// Wake word search runs in its own task: the Deepgram request takes seconds, and doing it inline
// in the audio task stopped mic reads for that whole time (~20% of audio was being captured)
TaskHandle_t WakeSearchTaskHandle = NULL;
volatile bool wakeSearchBusy = false;  // stt_temp_buffer is in use by the search task
volatile bool wakeSearchHit = false;   // result of the last finished search, consumed by audioTask
volatile bool sos_in_progress = false; // no new wake word searches while an SOS is being sent

// Spoken command streaming: the audio task records into command_buffer, and the main loop
// (which owns the Gemini WebSocket) forwards it as 8 kHz audio while the user is still talking.
volatile bool voice_stream_active = false;    // a command is being (or about to be) streamed
volatile bool voice_stream_finished = false;  // recording ended; send the rest and activityEnd
volatile int voice_stream_sent = 0;           // bytes of command_buffer already sent
const int VOICE_STREAM_SKIP_BYTES = 3200 * 2; // first 200 ms: the mic's restart burst after the ding

// Called by the audio task right after it resets command_buffer for a new recording
void startVoiceStream() {
    voice_stream_sent = VOICE_STREAM_SKIP_BYTES;
    voice_stream_finished = false;
    voice_stream_active = true;
}

// Enum for audio task commands
enum class AudioCommandType {
    SPEAK_TEXT,
    PLAY_DING,
    PLAY_BUTTON_DING,
    START_RECORDING,
    STOP_RECORDING_AND_PROCESS
};

// Struct for audio commands
struct AudioCommand {
    AudioCommandType type;
    char text[256]; // For SPEAK_TEXT command
};

// Struct for command messages (safer than String objects)
struct CommandMessage {
    char command[256]; // Fixed size buffer instead of String
};

// Function declarations
void audioTask(void *pvParameters);
void wakeSearchTask(void *pvParameters);
void process_audio();
void playDingSound();
void playButtonDingSound();
String cleanTextForWakeWord(const String& text);
void sendEmergencyAlert(const String& alertType, const String& description);
void handleSystemAction(const JsonDocument& doc);
void initializeLanguageSettings();
void calculateBaselineAudioLevel();
float recentCommandLevel();
void processRecordedCommand();
void handleButton();
void buttonTask(void *pvParameters);
void hapticsDirection(HazardDirection d, uint16_t durationMs = 300);
void checkAndAnnounceNearbyPlaces();
void checkNearbyHazards();
String stripHtmlTags(const String& html);

// Tool call handler for system actions
void toolHandler(const String& toolName, const String& jsonParams) {
    if (toolName == "systemAction") {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonParams);
        if (error) {
            Serial.printf("Failed to parse systemAction JSON: %s\n", error.c_str());
            return;
        }
        
        // Conditionally print based on shouldSpeak to reduce log noise
        if (doc["shouldSpeak"].as<bool>()) {
            Serial.printf("systemAction call received with params: %s\n", jsonParams.c_str());
        }
        
        handleSystemAction(doc);
    } else if (toolName == "getDirections") {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, jsonParams);
        if (error) {
            Serial.printf("Failed to parse getDirections JSON: %s\n", error.c_str());
            return;
        }
        String destination = doc["destination"].as<String>();
        Serial.printf("getDirections call received with destination: %s\n", destination.c_str());

        GPSData origin = visionAssistant.getCurrentGPSData();
        if (!origin.isValid) {
            String message = "Sorry, I can't get directions without a valid GPS location.";
            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0';
            xQueueSend(audioCommandQueue, &cmd, 0);
            return;
        }

        HTTPClient http;
        String url = "https://maps.googleapis.com/maps/api/directions/json?origin=" +
                     String(origin.latitude, 6) + "," + String(origin.longitude, 6) +
                     "&destination=" + destination +
                     "&key=" + String(GEMINI_API_KEY);
        
        http.begin(url);
        int httpResponseCode = http.GET();
        String directions = "";

        if (httpResponseCode == 200) {
            String payload = http.getString();
            JsonDocument dirDoc;
            deserializeJson(dirDoc, payload);

            if (dirDoc["status"] == "OK") {
                JsonArray steps = dirDoc["routes"][0]["legs"][0]["steps"];
                directions = "Starting route. ";
                for (int i = 0; i < steps.size() && i < 3; i++) { // Read out first 3 steps
                    String instruction = steps[i]["html_instructions"];
                    directions += stripHtmlTags(instruction) + ". ";
                }
            } else {
                directions = "Sorry, I could not find directions to " + destination;
            }
        } else {
            directions = "Sorry, there was an error getting directions.";
        }
        http.end();

        AudioCommand cmd;
        cmd.type = AudioCommandType::SPEAK_TEXT;
        strncpy(cmd.text, directions.c_str(), sizeof(cmd.text) - 1);
        cmd.text[sizeof(cmd.text) - 1] = '\0';
        if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
            Serial.println("❌ Failed to queue SPEAK_TEXT command for directions");
        }
     } else {
         Serial.printf("Tool call handler invoked for unknown tool: %s\n", toolName.c_str());
     }
}

void playDingSound() {
    Serial.println("🔔 Playing wake word confirmation ding...");
    
    // Generate a pleasant ding sound (two-tone chime)
    const int dingDuration = 300;  // Total duration in ms
    const int sampleRate = 16000;
    const int samplesPerTone = (sampleRate * dingDuration / 2) / 1000;  // Split into two tones
    const int totalSamples = samplesPerTone * 2;
    
    // Allocate buffer for ding sound
    uint8_t* dingBuffer = (uint8_t*)malloc(totalSamples * 2);  // 16-bit samples
    if (!dingBuffer) {
        Serial.println("❌ Failed to allocate ding buffer");
        return;
    }
    
    int16_t* samples = (int16_t*)dingBuffer;
    
    // Generate first tone (800 Hz)
    for (int i = 0; i < samplesPerTone; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.3f * (1.0f - t * 2);  // Fade out
        samples[i] = (int16_t)(amplitude * 8000 * sin(2 * PI * 800 * t));
    }
    
    // Generate second tone (1000 Hz)
    for (int i = 0; i < samplesPerTone; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.3f * (1.0f - t * 2);  // Fade out
        samples[samplesPerTone + i] = (int16_t)(amplitude * 8000 * sin(2 * PI * 1000 * t));
    }
    
    // Use TTS to play the ding sound (TTS handles I2S management properly)
    bool dingSuccess = false;
    if (ttsAvailable) {
        dingSuccess = tts.playAudioData(dingBuffer, totalSamples * 2);
        if (!dingSuccess) {
            Serial.println("❌ Failed to play ding sound via TTS");
        }
    } else {
        Serial.println("❌ TTS not available for ding sound");
    }
    
    // Clean up
    free(dingBuffer);
    
    if (dingSuccess) {
        Serial.println("🔔 Ding sound complete - ready for command!");
    } else {
        Serial.println("🔔 Ding sound failed but continuing - ready for command!");
    }
}

void playButtonDingSound() {
    Serial.println("🔘 Playing command transcribed button ding...");
    
    // Generate a single-tone button click sound (shorter than wake word ding)
    const int dingDuration = 150;  // Shorter duration for button feedback
    const int sampleRate = 16000;
    const int totalSamples = (sampleRate * dingDuration) / 1000;
    
    // Allocate buffer for button ding sound
    uint8_t* dingBuffer = (uint8_t*)malloc(totalSamples * 2);  // 16-bit samples
    if (!dingBuffer) {
        Serial.println("❌ Failed to allocate button ding buffer");
        return;
    }
    
    int16_t* samples = (int16_t*)dingBuffer;
    
    // Generate single tone (1200 Hz - higher pitch than wake word ding)
    for (int i = 0; i < totalSamples; i++) {
        float t = (float)i / sampleRate;
        float amplitude = 0.25f * (1.0f - t * 6.67f);  // Quick fade out
        if (amplitude < 0) amplitude = 0;
        samples[i] = (int16_t)(amplitude * 6000 * sin(2 * PI * 1200 * t));
    }
    
    // Use TTS to play the button ding sound
    bool dingSuccess = false;
    if (ttsAvailable) {
        dingSuccess = tts.playAudioData(dingBuffer, totalSamples * 2);
        if (!dingSuccess) {
            Serial.println("❌ Failed to play button ding sound via TTS");
        }
    } else {
        Serial.println("❌ TTS not available for button ding sound");
    }
    
    // Clean up
    free(dingBuffer);
    
    if (dingSuccess) {
        Serial.println("🔘 Button ding complete - command transcribed!");
    } else {
        Serial.println("🔘 Button ding failed but continuing - command transcribed!");
    }
}

// Helper function to format ISO timestamp
String getISOTimestamp() {
    // Real UTC time once NTP has synced (configTime in connectToWiFi)
    time_t now = time(nullptr);
    if (now > 1700000000) {  // a 2023+ date means the clock is set
        struct tm utc;
        gmtime_r(&now, &utc);
        char iso[32];
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return String(iso);
    }

    // Fallback before NTP sync: placeholder date + device uptime
    unsigned long currentMillis = millis();
    unsigned long totalSeconds = currentMillis / 1000;
    
    // Calculate hours, minutes, and seconds from device uptime
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long seconds = totalSeconds % 60;
    
    // Format as ISO timestamp (using current date as base)
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "2025-08-02T%02lu:%02lu:%02luZ", hours, minutes, seconds);
    return String(timestamp);
}

// Helper function to determine severity based on alert type
String getSeverityLevel(const String& alertType) {
    if (alertType == "fall_detection" || alertType == "medical_emergency" || alertType == "panic_button") {
        return "high";
    } else if (alertType == "obstacle_alert" || alertType == "lost_device") {
        return "medium";
    } else {
        return "low";
    }
}

void sendEmergencyAlert(const String& alertType, const String& description) {
    Serial.println("🚨 Emergency protocol activated!");
    Serial.printf("Alert Type: %s\n", alertType.c_str());
    Serial.printf("Description: %s\n", description.c_str());
    
    // Get current GPS location if available
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    
    // Create location object
    JsonDocument locationObj;
    if (gpsData.isValid) {
        locationObj["latitude"] = gpsData.latitude;
        locationObj["longitude"] = gpsData.longitude;
        locationObj["address"] = "GPS Location"; // Could be enhanced with reverse geocoding
    } else {
        locationObj["latitude"] = nullptr;
        locationObj["longitude"] = nullptr;
        locationObj["address"] = "Location unavailable";
    }
    
    // Determine appropriate title and message based on alert type
    String title, message;
    if (alertType == "fall_detection") {
        title = "Emergency Alert - Fall Detected";
        message = description.isEmpty() ? "Fall detected by wearable device sensors" : description;
    } else if (alertType == "medical_emergency") {
        title = "Medical Emergency Alert";
        message = description.isEmpty() ? "Medical emergency detected" : description;
    } else if (alertType == "panic_button") {
        title = "Panic Alert";
        message = description.isEmpty() ? "Panic button activated by user" : description;
    } else if (alertType == "lost_device") {
        title = "Device Location Alert";
        message = description.isEmpty() ? "Device location tracking activated" : description;
    } else {
        title = "Safety Alert";
        message = description.isEmpty() ? "Emergency situation detected" : description;
    }
    
    // Create notification JSON in the specified format
    JsonDocument notificationDoc;
    notificationDoc["title"] = title;
    notificationDoc["message"] = message;
    notificationDoc["timestamp"] = getISOTimestamp();
    notificationDoc["severity"] = getSeverityLevel(alertType);
    notificationDoc["location"] = locationObj;
    // The API only knows help/fall/sos/emergency (anything else is stored as "help")
    notificationDoc["alert_type"] = (alertType == "panic_button") ? "sos" : alertType;
    notificationDoc["device_id"] = "esp32_camera";
    
    // Convert to string
    String jsonString;
    serializeJson(notificationDoc, jsonString);
    
    Serial.println("📡 Sending emergency notification to API...");
    Serial.printf("URL: %s/notifications\n", NOTIFICATIONS_API_URL);
    Serial.printf("JSON: %s\n", jsonString.c_str());

    // An SOS needs its own TLS connection, and with Gemini's connection open plus a wake word
    // search or a speech download in flight there wasn't memory for it ("SSL - Memory allocation
    // failed", 3 out of 3 attempts). So: stop new searches, let the ones in flight finish, and if
    // that's still not enough, drop the Gemini connection (it reconnects by itself).
    sos_in_progress = true;
    unsigned long waitStart = millis();
    while ((wakeSearchBusy || is_speaking) && millis() - waitStart < 8000) {
        delay(50);
    }

    const int MAX_ATTEMPTS = 3;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        HTTPClient http;
        http.begin(String(NOTIFICATIONS_API_URL) + "/notifications");
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(jsonString);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.printf("✅ Emergency notification sent successfully! Response code: %d\n", httpResponseCode);
            Serial.printf("Response: %s\n", response.c_str());
            http.end();
            sos_in_progress = false;
            return;
        }
        Serial.printf("❌ Emergency notification attempt %d/%d failed: %s (heap %u, largest block %u)\n",
                      attempt, MAX_ATTEMPTS, http.errorToString(httpResponseCode).c_str(),
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        http.end();
        if (attempt == 1) {
            visionAssistant.dropConnectionForMemory();
        }
        delay(1500);
    }
    Serial.println("❌ Could not send the emergency notification");
    sos_in_progress = false;
}

void handleSystemAction(const JsonDocument& doc) {
    // Extract parameters
    String intent = doc["intent"].as<String>();
    bool shouldSpeak = doc["shouldSpeak"].as<bool>();
    String message = doc["message"].as<String>();
    String logEntry = doc["logEntry"].as<String>();
    // String routeTo = doc["routeTo"].as<String>();
    String routeParams = doc["routeParams"].as<String>();
    
    if (shouldSpeak) {
        Serial.printf("Intent: %s\n", intent.c_str());
        Serial.printf("Should Speak: %s\n", shouldSpeak ? "true" : "false");
        Serial.printf("Message: %s\n", message.c_str());
        Serial.printf("Log Entry: %s\n", logEntry.c_str());
        Serial.printf("Route Params: %s\n", routeParams.c_str());
    }

    // Hazards vibrate first and always: speech takes 5-8 s to download and play, and is dropped
    // below if something is already being said. The vibration used to come after speakText()
    // returned, or not at all when TTS was busy.
    if (intent == "obstacle_alert") {
        Serial.println("⚠️ Obstacle alert detected!");
        if (hapticsAvailable) {
            String direction = doc["direction"].as<String>();
            HazardDirection d = HazardDirection::UNKNOWN;
            if (direction == "left") d = HazardDirection::LEFT;
            else if (direction == "center") d = HazardDirection::CENTER;
            else if (direction == "right") d = HazardDirection::RIGHT;
            if (d != HazardDirection::UNKNOWN) {
                hapticsDirection(d);
            } else {
                Serial.println("⚠️ obstacle_alert had no valid direction - skipping haptic pulse");
            }
        }
        // Obstacle alerts should always be spoken for safety
        shouldSpeak = true;
    }

    // Handle speaking if required
    if (shouldSpeak && !message.isEmpty()) {
        if (is_speaking) {
            Serial.println("🗣️ TTS is already active, dropping new speak request.");
            return;
        }
        if (ttsAvailable) {
            is_speaking = true; // Set flag before sending
            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0'; // Ensure null termination
            
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue SPEAK_TEXT command");
                is_speaking = false; // Reset flag if queueing failed
            }
        } else {
            Serial.println("TTS not available to speak message.");
        }
    }
    
    // Log the entry if provided
    if (!logEntry.isEmpty()) {
        Serial.printf("LOG: %s\n", logEntry.c_str());
    }
    
    // Handle specific intents
    if (intent == "emergency_protocol") {
        Serial.println("🚨 Emergency protocol detected!");
        
        // Determine alert type based on message content and context
        String alertType = "fall_detection"; // Default type
        String description = message.isEmpty() ? "Emergency detected by vision assistant" : message;
        
        // Analyze message content to determine specific emergency type
        String lowerMessage = message;
        lowerMessage.toLowerCase();
        
        if (lowerMessage.indexOf("fall") != -1 || lowerMessage.indexOf("fell") != -1 || 
            lowerMessage.indexOf("down") != -1 || lowerMessage.indexOf("trip") != -1) {
            alertType = "fall_detection";
            if (description == message) {
                description = "Fall detected - " + message;
            }
        }
        else if (lowerMessage.indexOf("medical") != -1 || lowerMessage.indexOf("hurt") != -1 || 
                 lowerMessage.indexOf("pain") != -1 || lowerMessage.indexOf("sick") != -1 ||
                 lowerMessage.indexOf("emergency") != -1) {
            alertType = "medical_emergency";
            if (description == message) {
                description = "Medical emergency - " + message;
            }
        }
        else if (lowerMessage.indexOf("help") != -1 || lowerMessage.indexOf("panic") != -1 || 
                 lowerMessage.indexOf("scared") != -1 || lowerMessage.indexOf("danger") != -1) {
            alertType = "panic_button";
            if (description == message) {
                description = "Panic alert - " + message;
            }
        }
        else if (lowerMessage.indexOf("lost") != -1 || lowerMessage.indexOf("find") != -1 || 
                 lowerMessage.indexOf("location") != -1) {
            alertType = "lost_device";
            if (description == message) {
                description = "Location assistance - " + message;
            }
        }
        else if (lowerMessage.indexOf("unresponsive") != -1 || lowerMessage.indexOf("no response") != -1 ||
                 lowerMessage.indexOf("unconscious") != -1) {
            alertType = "medical_emergency";
            if (description == message) {
                description = "User unresponsive - " + message;
            }
        }
        
        // Log the emergency details
        if (!logEntry.isEmpty()) {
            Serial.printf("EMERGENCY LOG: %s\n", logEntry.c_str());
        }
        
        // Send emergency alert with determined type
        sendEmergencyAlert(alertType, description);
    }
    else if (intent == "obstacle_alert") {
        // Vibration and speech were handled at the top (vibrate first, speak via the audio queue)
        if (!logEntry.isEmpty()) {
            Serial.printf("OBSTACLE LOG: %s\n", logEntry.c_str());
        }
    }
    else if (intent == "contextual_assistance") {
        Serial.println("🗺️ Contextual assistance provided");
        // Log context information
        if (!logEntry.isEmpty()) {
            Serial.printf("CONTEXT LOG: %s\n", logEntry.c_str());
        }
    }
    else if (intent == "voice_query") {
        Serial.println("🎤 Voice query received");
        // Log the query for processing
        if (!logEntry.isEmpty()) {
            Serial.printf("QUERY LOG: %s\n", logEntry.c_str());
        }
        // Voice queries typically get responses
    }
    else if (intent == "memory_store") {
        Serial.println("💾 Memory storage request");
        // Log memory item
        if (!logEntry.isEmpty()) {
            Serial.printf("MEMORY STORED: %s\n", logEntry.c_str());
        }
        // Confirm storage if requested
    }
    else if (intent == "navigation_query") {
        Serial.println("🧭 Navigation query received");
        // Log navigation request
        if (!logEntry.isEmpty()) {
            Serial.printf("NAVIGATION LOG: %s\n", logEntry.c_str());
        }
    }
    else if (intent == "hand_gesture") {
        Serial.println("👋 Hand gesture detected");
        // Log gesture information
        if (!logEntry.isEmpty()) {
            Serial.printf("GESTURE LOG: %s\n", logEntry.c_str());
        }
    }
    else {
        // Handle unknown intents
        Serial.printf("❓ Unknown intent: %s\n", intent.c_str());
        if (!logEntry.isEmpty()) {
            Serial.printf("UNKNOWN INTENT LOG: %s\n", logEntry.c_str());
        }
    }
    
    // Handle routing if specified
    // if (!routeTo.isEmpty()) {
    //     Serial.printf("Routing to: %s with params: %s\n", routeTo.c_str(), routeParams.c_str());
    //     // Add routing logic here as needed
    // }
}

String cleanTextForWakeWord(const String& text) {
    String cleaned = text;
    cleaned.toLowerCase();
    
    // Remove common punctuation and extra spaces
    cleaned.replace(".", "");
    cleaned.replace(",", "");
    cleaned.replace("!", "");
    cleaned.replace("?", "");
    cleaned.replace(";", "");
    cleaned.replace(":", "");
    cleaned.replace("-", "");
    cleaned.replace("_", "");
    cleaned.replace("'", "");
    cleaned.replace("\"", "");
    
    // Replace multiple spaces with single space
    while (cleaned.indexOf("  ") != -1) {
        cleaned.replace("  ", " ");
    }
    
    // Trim leading/trailing spaces
    cleaned.trim();
    
    return cleaned;
}

// RMS with the mean removed. The INMP441 sits a few thousand counts off zero, so plain RMS is
// dominated by that offset and barely changes when someone speaks.
static float acRms(const int16_t* samples, int count) {
    if (count <= 0) {
        return 0.0f;
    }
    float mean = 0.0f;
    for (int i = 0; i < count; i++) {
        mean += samples[i];
    }
    mean /= count;
    float sum_squares = 0.0f;
    for (int i = 0; i < count; i++) {
        float d = samples[i] - mean;
        sum_squares += d * d;
    }
    return sqrt(sum_squares / count);
}

// Level of the last 300 ms of the command recording (call with audioMutex held)
float recentCommandLevel() {
    int total_samples = command_buffer_index / 2;
    int window = min(4800, total_samples); // 300 ms at 16 kHz
    if (!command_buffer || window <= 0) {
        return 0.0f;
    }
    return acRms((int16_t*)command_buffer + (total_samples - window), window);
}

void calculateBaselineAudioLevel() {
    if (!command_buffer || command_buffer_index < 1600) { // Need at least 0.1 seconds of audio
        return;
    }
    
    // Level of the first 0.5 seconds of recording, skipping the first 200 ms: the mic is
    // re-initialized after the ding and the INMP441 outputs full-scale garbage while it wakes up.
    const int skip_samples = 3200; // 200 ms at 16 kHz
    int total_samples = command_buffer_index / 2; // Total number of 16-bit samples available
    int samples_to_analyze = min(total_samples - skip_samples, 8000 - skip_samples);

    if (samples_to_analyze <= 0) {
        return; // Not enough audio past the startup transient yet; retried on the next loop
    }

    baseline_audio_level = acRms((int16_t*)command_buffer + skip_samples, samples_to_analyze);
    baseline_calculated = true;

    // Start of a new recording: the noise floor only goes down from here (the user may already be
    // talking during this first window, so it isn't trusted as "silence" on its own)
    noise_floor = baseline_audio_level;
    speech_detected = false;
    silent_windows = 0;
    
    Serial.printf("📊 Baseline audio level calculated: %.2f (from %d samples)\n", baseline_audio_level, samples_to_analyze);
}

void initializeLanguageSettings() {
    Serial.println("🌐 Initializing language settings...");
    
    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ WiFi not connected, using default language (en)");
        return;
    }
    
    // Fetch settings from API
    UserSettings settings = settingsManager.getSettings();
    if (settings.isValid) {
        String language = settings.language;
        Serial.printf("✅ Language setting retrieved: %s\n", language.c_str());
        
        // Set language for both STT and TTS
        deepgramClient.setDefaultLanguage(language);
        tts.setDefaultLanguage(language);
        visionAssistant.setResponseLanguage(language);

        Serial.printf("🎤 Deepgram STT language set to: %s\n", language.c_str());
        Serial.printf("🔊 Deepgram TTS language set to: %s\n", language.c_str());

        // App volume 0-100 -> TTS software gain 0.0-2.0 (50 = unchanged)
        tts.setVolume(settings.volume / 100.0f);
    } else {
        Serial.println("⚠️ Failed to fetch language settings, using defaults");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting setup...");
    
    // Print core info
    Serial.printf("Setup running on core: %d\n", xPortGetCoreID());
    
    // Create synchronization primitives
    audioMutex = xSemaphoreCreateMutex();
    commandQueue = xQueueCreate(5, sizeof(CommandMessage)); // Use CommandMessage struct instead of String
    audioCommandQueue = xQueueCreate(5, sizeof(AudioCommand)); // Create audio command queue
    
    if (!audioMutex || !commandQueue || !audioCommandQueue) {
        Serial.println("CRITICAL: Failed to create synchronization primitives!");
        while (true) delay(1000);
    }
    
    // Print memory info at startup
    Serial.printf("🔧 Startup memory info:\n");
    Serial.printf("   Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("   Heap size: %d bytes\n", ESP.getHeapSize());
    if (psramFound()) {
        Serial.printf("   PSRAM found: %d bytes free\n", ESP.getFreePsram());
    } else {
        Serial.println("   PSRAM: Not found");
    }
    
    // Check if PSRAM is available and allocate buffers
    if (!psramFound()) {
        Serial.println("PSRAM not found! Using regular malloc instead.");
        wake_word_buffer = (uint8_t*)malloc(WAKE_WORD_BUFFER_SIZE);
        command_buffer = (uint8_t*)malloc(COMMAND_BUFFER_SIZE);
    } else {
        Serial.println("PSRAM found, using ps_malloc.");
        // Send ordinary malloc() above 256 bytes (Strings, JSON docs, HTTP buffers...) to PSRAM.
        // Left in internal RAM they fragmented it over a few minutes until no TLS connection could
        // be opened ("SSL - Memory allocation failed" with ~110KB free). TLS, WiFi and DMA allocate
        // internal memory explicitly, so they're unaffected and get the internal heap to themselves.
        heap_caps_malloc_extmem_enable(256);
        wake_word_buffer = (uint8_t*)ps_malloc(WAKE_WORD_BUFFER_SIZE);
        command_buffer = (uint8_t*)ps_malloc(COMMAND_BUFFER_SIZE);
        stt_temp_buffer = (uint8_t*)ps_malloc(WAKE_WORD_BUFFER_SIZE);
    }
    
    if (!wake_word_buffer || !command_buffer || !stt_temp_buffer) {
        Serial.println("CRITICAL: Failed to allocate audio buffers!");
        Serial.printf("Tried to allocate wake word buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
        Serial.printf("Tried to allocate command buffer: %d bytes\n", COMMAND_BUFFER_SIZE);
        Serial.printf("Tried to allocate stt temp buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        if (psramFound()) {
            Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
        }
        while (true) {
            delay(1000);
        }
    }
    
    // Initialize buffers to zero
    memset(wake_word_buffer, 0, WAKE_WORD_BUFFER_SIZE);
    memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
    Serial.printf("✅ Successfully allocated wake word buffer: %d bytes\n", WAKE_WORD_BUFFER_SIZE);
    Serial.printf("✅ Successfully allocated command buffer: %d bytes\n", COMMAND_BUFFER_SIZE);
    Serial.printf("Wake word buffer address: %p\n", wake_word_buffer);
    Serial.printf("Command buffer address: %p\n", command_buffer);

    // Initialize Deepgram client now that we know PSRAM is available
    if (!deepgramClient.begin()) {
        Serial.println("CRITICAL: Failed to initialize DeepgramClient!");
        while(true) delay(1000);
    }

    // Initialize the vision assistant (includes GPS initialization) on Core 1
    Serial.println("Initializing Vision Assistant on Core 1...");
    while (!visionAssistant.initialize()) {
        Serial.println("Failed to initialize Vision Assistant! Retrying in 2 seconds...");
        delay(2000);
    }
    Serial.println("Vision Assistant initialized successfully!");

    // Initialize haptic feedback module (PCF8574 I2C expander + 3 vibration motors
    // + push button on P3). AI-Thinker ESP32-CAM: GPIO15 = SDA, GPIO4 = SCL
    // (GPIO4 tambien maneja el LED flash, que puede parpadear tenue con el I2C).
    // Requiere NO usar tarjeta microSD, que comparte estos pines.
    i2cMutex = xSemaphoreCreateMutex();
    hapticsAvailable = haptics.initialize(/*sda=*/15, /*scl=*/4);
    if (!hapticsAvailable) {
        Serial.println("Warning: Haptic driver not found on I2C bus - continuing without haptic feedback");
    } else {
        Serial.println("Haptic driver initialized successfully!");
    }

    // Initialize language settings after WiFi is connected
    initializeLanguageSettings();
    
    // Wake word search task on Core 1 so it never competes with mic reads on Core 0.
    // 8KB like the audio task that used to run this search: every KB of stack comes out of the
    // internal heap that the TLS connections (Gemini WS + Deepgram) need.
    xTaskCreatePinnedToCore(wakeSearchTask, "WakeSearch", 8192, NULL, 1, &WakeSearchTaskHandle, 1);
    if (WakeSearchTaskHandle == NULL) {
        Serial.println("CRITICAL: Failed to create wake word search task!");
    }

    // Start audio task on Core 0 (microphone will be initialized there)
    Serial.println("Starting audio task on Core 0...");
    xTaskCreatePinnedToCore(
        audioTask,           // Task function
        "AudioTask",         // Task name
        8192,               // Stack size (increased for audio processing)
        NULL,               // Parameters
        2,                  // Priority (higher than main loop)
        &AudioTaskHandle,   // Task handle
        0                   // Core 0
    );
    
    if (AudioTaskHandle == NULL) {
        Serial.println("CRITICAL: Failed to create audio task!");
        Serial.println("This could be due to insufficient memory or system resources.");
        Serial.println("Retrying audio task creation in 3 seconds...");
        delay(3000);
        
        // Try to create the task again in a loop until it succeeds
        while (AudioTaskHandle == NULL) {
            Serial.println("Retrying audio task creation...");
            xTaskCreatePinnedToCore(
                audioTask,           // Task function
                "AudioTask",         // Task name
                8192,               // Stack size (increased for audio processing)
                NULL,               // Parameters
                2,                  // Priority (higher than main loop)
                &AudioTaskHandle,   // Task handle
                0                   // Core 0
            );
            
            if (AudioTaskHandle == NULL) {
                Serial.printf("Audio task creation failed. Free heap: %d bytes. Retrying in 5 seconds...\n", ESP.getFreeHeap());
                delay(5000);
            } else {
                Serial.println("Audio task created successfully on retry!");
            }
        }
    }
    
    // Give the audio task time to initialize (increased like working demo)
    Serial.println("Waiting for audio task to initialize...");
    delay(5000);
    
    // Print GPS status
    Serial.println("GPS Status at startup:");
    GPSData gpsData = visionAssistant.getCurrentGPSData();
    if (gpsData.isValid) {
        Serial.println("GPS: " + visionAssistant.getGPSString());
    } else {
        Serial.println("GPS: Searching for satellites...");
    }
    
    // Try to initialize TTS with Deepgram API key
    tts.setGoogleTtsApiKey(GOOGLE_TTS_API_KEY);
    if (tts.initialize(DEEPGRAM_API_KEY)) {
        Serial.println("TTS initialized successfully!");
        ttsAvailable = true;
    } else {
        Serial.println("Initial TTS initialization failed - will try lazy initialization");
        ttsAvailable = false;
    }
    
    // Set the tool callback
    visionAssistant.setToolCallback(toolHandler);
    
    // Button lives on the PCF8574 (P3) - no pinMode needed, it is read over I2C
    if (hapticsAvailable) {
        // Polled in its own task: the main loop can be busy for hundreds of ms (frame uploads)
        // and quick SOS taps were being missed. Core 1, above the loop task's priority.
        xTaskCreatePinnedToCore(buttonTask, "ButtonTask", 4096, NULL, 2, NULL, 1);
        Serial.println("Button ready on PCF8574 P3.");
    } else {
        Serial.println("Warning: PCF8574 not found - push button disabled (wake word still works).");
    }
    
    // Play a ding sound to indicate setup is complete
    Serial.println("✅ Setup complete! Playing notification sound...");
    AudioCommand setupCompleteDingCmd;
    setupCompleteDingCmd.type = AudioCommandType::PLAY_BUTTON_DING;
    if (xQueueSend(audioCommandQueue, &setupCompleteDingCmd, 0) != pdTRUE) {
        Serial.println("❌ Failed to queue setup complete ding command");
    }

    // Spoken confirmation: a blind user can't see a status LED, so say the device is ready
    AudioCommand readyCmd;
    readyCmd.type = AudioCommandType::SPEAK_TEXT;
    String readyText = settingsManager.getLanguage().startsWith("es") ? "Listo." : "Ready.";
    strncpy(readyCmd.text, readyText.c_str(), sizeof(readyCmd.text) - 1);
    readyCmd.text[sizeof(readyCmd.text) - 1] = '\0';
    if (xQueueSend(audioCommandQueue, &readyCmd, 0) != pdTRUE) {
        Serial.println("❌ Failed to queue ready announcement");
    }

    Serial.printf("Vision Assistant setup complete on core %d - starting main loop\n", xPortGetCoreID());
}

void process_audio() {
    // Check if audio buffers are allocated
    if (!wake_word_buffer || !command_buffer) {
        static unsigned long last_warning = 0;
        if (millis() - last_warning > 5000) {  // Warn every 5 seconds
            Serial.println("WARNING: Audio buffers not allocated, skipping audio processing");
            last_warning = millis();
        }
        return;
    }
    
    const int read_buffer_size = 512;
    int32_t raw_buffer[read_buffer_size];
    size_t bytes_read = 0;

    esp_err_t result = read_microphone_data(raw_buffer, sizeof(raw_buffer), &bytes_read);

    if (result == ESP_OK && bytes_read > 0) {
        int samples_read = bytes_read / sizeof(int32_t);
        
        // Debug: Log microphone reading stats occasionally
        static unsigned long last_read_debug = 0;
        static int total_reads = 0;
        static size_t total_bytes = 0;
        total_reads++;
        total_bytes += bytes_read;
        
        if (millis() - last_read_debug > 5000) {  // Every 5 seconds
            float avg_bytes_per_read = (float)total_bytes / total_reads;
            float effective_sample_rate = (total_bytes / sizeof(int32_t)) / ((millis() - last_read_debug) / 1000.0);
            Serial.printf("🎤 Mic stats: %d reads, avg %.1f bytes/read, ~%.0f samples/sec\n", 
                         total_reads, avg_bytes_per_read, effective_sample_rate);
            last_read_debug = millis();
            total_reads = 0;
            total_bytes = 0;
        }
        
        // Process each sample
        for (int i = 0; i < samples_read; i++) {
            // Convert 32-bit sample to 16-bit (same as working demo)
            int32_t sample = raw_buffer[i] >> 14;

            // Apply digital gain (reduced from 5 to 2 like working demo)
            sample = sample * 2;

            // Clip to 16-bit range to prevent overflow
            if (sample > 32767) {
                sample = 32767;
            } else if (sample < -32768) {
                sample = -32768;
            }
            
            int16_t sample16 = (int16_t)sample;
            uint8_t* sample_bytes = (uint8_t*)&sample16;
            
            // Store in wake word buffer (continuous circular buffer) - ensure we have space for 2 bytes
            if (wake_word_buffer_index + 2 <= WAKE_WORD_BUFFER_SIZE) {
                wake_word_buffer[wake_word_buffer_index] = sample_bytes[0];
                wake_word_buffer[wake_word_buffer_index + 1] = sample_bytes[1];
                wake_word_buffer_index += 2;
            } else {
                // Buffer is full, wrap to beginning (circular buffer)
                wake_word_buffer_has_wrapped = true;
                wake_word_buffer_index = 0;
                if (wake_word_buffer_index + 2 <= WAKE_WORD_BUFFER_SIZE) {
                    wake_word_buffer[wake_word_buffer_index] = sample_bytes[0];
                    wake_word_buffer[wake_word_buffer_index + 1] = sample_bytes[1];
                    wake_word_buffer_index += 2;
                }
            }
            
            // Store in command buffer if recording - ensure we have space for 2 bytes
            if (is_recording && command_buffer_index + 2 <= COMMAND_BUFFER_SIZE) {
                command_buffer[command_buffer_index] = sample_bytes[0];
                command_buffer[command_buffer_index + 1] = sample_bytes[1];
                command_buffer_index += 2;
            }
            
            // Increment sequence every 1000 samples to track buffer updates
            if ((wake_word_buffer_index % 2000) == 0) {
                buffer_sequence++;
            }
        }
    } else if (result != ESP_OK) {
        static unsigned long last_error = 0;
        if (millis() - last_error > 10000) {  // Log error every 10 seconds
            Serial.printf("Microphone read error: %s\n", esp_err_to_name(result));
            last_error = millis();
        }
    }
}

// Audio processing task running on Core 0
void wakeSearchTask(void *pvParameters) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Use Deepgram's search API for wake word detection instead of transcription
        Serial.println("🔍 Searching for wake words using Deepgram search API...");
        bool hit = deepgramClient.searchForWakeWords(stt_temp_buffer, WAKE_WORD_BUFFER_SIZE, WAKE_WORDS, WAKE_WORDS_COUNT, 0.60f);
        if (hit) {
            Serial.println("✅ Wake word detected via search API!");
        }
        // A hit that lands while a command is already being recorded is stale
        wakeSearchHit = hit && !is_recording;
        wakeSearchBusy = false;
    }
}

void audioTask(void *pvParameters) {
    Serial.println("Audio task started on Core 0");
    
    // Initialize the microphone on Core 0
    Serial.println("Initializing microphone on Core 0...");
    setup_microphone();
    Serial.println("Microphone initialized successfully on Core 0!");
    
    // Give microphone additional time to stabilize (like in working demo)
    delay(1000);
    
    unsigned long last_stt_time = 0;
    unsigned long recording_start_time = 0;
    
    while (true) {
        // Check for commands from the main core
        AudioCommand receivedCmd;
        // Speech and wake word search must not overlap: each opens a TLS connection (~40KB heap),
        // and running both at once exhausted the heap and broke WiFi/DNS. Wait for an in-flight
        // search before taking a command (same delay as when the search ran inline).
        if (wakeSearchBusy && uxQueueMessagesWaiting(audioCommandQueue) > 0) {
            while (wakeSearchBusy) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        if (xQueueReceive(audioCommandQueue, &receivedCmd, 0) == pdTRUE) {
            bool micWasActive = is_microphone_active();
            if (micWasActive) {
                stop_microphone();
            }

            if (receivedCmd.type == AudioCommandType::SPEAK_TEXT) {
                Serial.printf("🎤 Audio task received SPEAK_TEXT: \"%s\"\n", receivedCmd.text);
                visionAssistant.setFramesPaused(true);  // leave the WiFi link to the speech download
                tts.speakText(String(receivedCmd.text));
                visionAssistant.setFramesPaused(false);
                is_speaking = false; // Reset flag after speaking is done
            } else if (receivedCmd.type == AudioCommandType::PLAY_DING) {
                Serial.println("🎤 Audio task received PLAY_DING");
                playDingSound();
            } else if (receivedCmd.type == AudioCommandType::PLAY_BUTTON_DING) {
                Serial.println("🎤 Audio task received PLAY_BUTTON_DING");
                playButtonDingSound();
            } else if (receivedCmd.type == AudioCommandType::START_RECORDING) {
                Serial.println("🎤 Audio task received START_RECORDING");
                if (is_speaking) {
                    Serial.println("🚫 Button pressed during speech - cancelling TTS...");
                    tts.cancel();
                }
                
                is_recording = true;
                recording_start_time = millis();
                Serial.println("Recording command (button press)...");
                
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    command_buffer_index = 0;
                    baseline_calculated = false;
                    if (command_buffer) {
                        memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
                    }
                    xSemaphoreGive(audioMutex);
                }
                startVoiceStream();
            } else if (receivedCmd.type == AudioCommandType::STOP_RECORDING_AND_PROCESS) {
                Serial.println("🎤 Audio task received STOP_RECORDING_AND_PROCESS");
                if (is_recording) {
                    processRecordedCommand();
                }
            }

            if (micWasActive) {
                setup_microphone();
            }
        }

        // Process audio data
        if ((wake_word_buffer || command_buffer) && xSemaphoreTake(audioMutex, portMAX_DELAY)) {
            process_audio();
            xSemaphoreGive(audioMutex);
        }

        // Speech-to-text processing every 1 second for wake word detection (only when not recording)
        // Note: Wake word detection uses Deepgram's search API for acoustic pattern matching
        // Command transcription after wake word still uses regular transcription API
        if (!is_recording && millis() - last_stt_time > 1000) {
            last_stt_time = millis();

            // Check if wake word buffer is allocated and has sufficient data
            if (!wake_word_buffer || (!wake_word_buffer_has_wrapped && wake_word_buffer_index < 8000)) {
                // Add debug info about buffer state
                static unsigned long last_debug = 0;
                if (millis() - last_debug > 10000) {  // Debug every 10 seconds
                    Serial.printf("Wake word buffer state: allocated=%s, index=%d, required=8000, wrapped=%s\n", 
                                wake_word_buffer ? "yes" : "no", wake_word_buffer_index, 
                                wake_word_buffer_has_wrapped ? "yes" : "no");
                    
                    // Show some sample values to see if we're getting real audio data
                    if (wake_word_buffer && wake_word_buffer_index > 100) {
                        int16_t* samples = (int16_t*)wake_word_buffer;
                        Serial.printf("Sample audio values: %d, %d, %d, %d, %d\n", 
                                    samples[0], samples[1], samples[10], samples[50], samples[100]);
                    }
                    
                    // Check buffer bounds
                    if (wake_word_buffer_index > WAKE_WORD_BUFFER_SIZE) {
                        Serial.printf("❌ CRITICAL: Wake word buffer index out of bounds: %d > %d\n", 
                                    wake_word_buffer_index, WAKE_WORD_BUFFER_SIZE);
                        wake_word_buffer_index = 0; // Reset to prevent corruption
                        wake_word_buffer_has_wrapped = false;
                    }
                    
                    Serial.printf("Buffer sequence: %u\n", buffer_sequence);
                    last_debug = millis();
                }
                continue;
            }

            // Create a temporary buffer with the last 3 seconds of audio
            if (!stt_temp_buffer) {
                Serial.println("STT temp buffer not allocated");
                continue;
            }
            // The search task is still reading stt_temp_buffer; keep recording and check back later
            if (wakeSearchBusy) {
                continue;
            }
            uint8_t* temp_buffer = stt_temp_buffer;
            
            int current_index;
            bool has_wrapped;
            uint32_t current_sequence;
            static uint32_t last_transcribed_sequence = 0;
            
            if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                current_index = wake_word_buffer_index;
                has_wrapped = wake_word_buffer_has_wrapped;
                current_sequence = buffer_sequence;
                
                // Only transcribe if we have new audio data
                if (current_sequence == last_transcribed_sequence) {
                    xSemaphoreGive(audioMutex);
                    continue; // Skip transcription - no new audio
                }
                
                // Copy the most recent audio data in proper circular buffer order
                if (has_wrapped) {
                    // Buffer has wrapped - copy from current position to end, then from start to current
                    int bytes_from_current_to_end = WAKE_WORD_BUFFER_SIZE - current_index;
                    memcpy(stt_temp_buffer, wake_word_buffer + current_index, bytes_from_current_to_end);
                    memcpy(stt_temp_buffer + bytes_from_current_to_end, wake_word_buffer, current_index);
                } else {
                    // Buffer hasn't wrapped yet, copy what we have and pad with zeros
                    memcpy(stt_temp_buffer, wake_word_buffer, current_index);
                    memset(stt_temp_buffer + current_index, 0, WAKE_WORD_BUFFER_SIZE - current_index);
                }
                
                // Update the last transcribed sequence
                last_transcribed_sequence = current_sequence;
                
                // Debug: show buffer state
                static unsigned long lastBufferDebug = 0;
                if (millis() - lastBufferDebug > 5000) {
                    Serial.printf("Buffer debug: index=%d, wrapped=%s, seq=%u, samples=[%d,%d,%d]\n", 
                                current_index, has_wrapped ? "yes" : "no", current_sequence,
                                ((int16_t*)wake_word_buffer)[0], ((int16_t*)wake_word_buffer)[1], ((int16_t*)wake_word_buffer)[2]);
                    lastBufferDebug = millis();
                }
                
                xSemaphoreGive(audioMutex);
            }

            // Only transcribe if we have enough real audio data (not just zeros)
            bool hasRealAudio = false;
            int16_t* samples = (int16_t*)stt_temp_buffer;
            int sampleCount = WAKE_WORD_BUFFER_SIZE / 2;
            for (int i = 0; i < sampleCount; i++) {
                if (abs(samples[i]) > 50) { // Threshold for real audio
                    hasRealAudio = true;
                    break;
                }
            }

            // Result of the search that just finished (it ran on the previous snapshot)
            bool wakeWordDetected = wakeSearchHit;
            wakeSearchHit = false;

            if (!wakeWordDetected) {
                if (uxQueueMessagesWaiting(audioCommandQueue) > 0) {
                    // Something is about to be spoken; let it go first (see the wait above)
                } else if (sos_in_progress) {
                    // The SOS request needs the TLS memory a search would take
                } else if (visionAssistant.isAwaitingCommandReply()) {
                    // The user just asked something; a search now would only delay speaking the answer
                } else if (hasRealAudio) {
                    // Hand the fresh snapshot to the search task; the mic keeps recording meanwhile
                    wakeSearchBusy = true;
                    xTaskNotifyGive(WakeSearchTaskHandle);
                } else {
                    Serial.println("No real audio detected in buffer, skipping wake word search");
                }
            }

            if (wakeWordDetected) {
                Serial.println("🎙️ Wake word detected via Deepgram search API!");
                
                // If TTS is active, cancel it immediately
                if (is_speaking) {
                    Serial.println("🚫 Wake word detected during speech - cancelling TTS...");
                    tts.cancel();
                }
                
                // Queue a ding sound to be played by the audio task, which will handle I2S switching
                AudioCommand dingCmd;
                dingCmd.type = AudioCommandType::PLAY_DING;
                if (xQueueSend(audioCommandQueue, &dingCmd, 0) != pdTRUE) {
                    Serial.println("❌ Failed to queue PLAY_DING command");
                }
                
                is_recording = true;
                recording_start_time = millis();
                Serial.println("Recording command (max 8 seconds)...");
                
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    command_buffer_index = 0; // Clear command buffer to start recording new audio
                    baseline_calculated = false; // Reset baseline
                    if (command_buffer) {
                        memset(command_buffer, 0, COMMAND_BUFFER_SIZE); // Clear the command buffer
                    }
                    xSemaphoreGive(audioMutex);
                }
                startVoiceStream();
            }
        }

        // Handle recording
        if (is_recording) {
            if (recording_start_time == 0) {
                recording_start_time = millis();
            }

            // Calculate baseline after first 0.5 seconds of recording
            if (!baseline_calculated && millis() - recording_start_time > 500) {
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    calculateBaselineAudioLevel();
                    xSemaphoreGive(audioMutex);
                }
            }

            // End-of-command detection. Wait for the user to start talking, then stop after ~1 s of
            // quiet. The old check compared against a baseline taken right after the ding (often the
            // user's own voice) and stopped at 3 s regardless, so most commands came back empty.
            bool shouldStopForSilence = false;
            static unsigned long lastSilenceCheck = 0;
            if (baseline_calculated && millis() - lastSilenceCheck > 200) { // Check every 200ms
                lastSilenceCheck = millis();
                float level = 0.0f;
                if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
                    // Safety check: ensure command buffer index is within bounds
                    if (command_buffer_index > COMMAND_BUFFER_SIZE) {
                        Serial.printf("❌ CRITICAL: Command buffer overflow detected: %d > %d\n",
                                    command_buffer_index, COMMAND_BUFFER_SIZE);
                        command_buffer_index = COMMAND_BUFFER_SIZE; // Cap it to prevent corruption
                    }
                    level = recentCommandLevel();
                    xSemaphoreGive(audioMutex);
                }

                // Noise floor = quietest window seen so far in this recording
                if (level > 1.0f && level < noise_floor) {
                    noise_floor = level;
                }
                float floorLevel = max(noise_floor, 20.0f);
                if (level > floorLevel * 3.0f) {
                    if (!speech_detected) {
                        Serial.printf("🗣️ Speech started (level %.0f, floor %.0f)\n", level, floorLevel);
                    }
                    speech_detected = true;
                    silent_windows = 0;
                } else if (level < floorLevel * 2.0f) {
                    silent_windows++;
                } else {
                    silent_windows = 0;
                }

                unsigned long elapsed = millis() - recording_start_time;
                if (speech_detected && silent_windows >= 7) {          // 1.4 s of quiet after speech (1 s cut "Dime... qué ves" at the pause)
                    Serial.println("🔇 Sustained silence detected - stopping recording");
                    shouldStopForSilence = true;
                } else if (!speech_detected && elapsed > 6000) {      // nothing said at all
                    Serial.println("🔇 No speech within 6 s - stopping recording");
                    shouldStopForSilence = true;
                }
            }

            // Stop recording if max time reached or silence detected
            // 8 s max: background TV/music can keep "speech" going, and commands are short anyway
            if (millis() - recording_start_time > 8000 || shouldStopForSilence) {
                if (shouldStopForSilence) {
                    Serial.println("Recording finished due to silence. Processing command...");
                } else {
                    Serial.println("Recording finished (8s max). Processing command...");
                }
                processRecordedCommand();
            }
        }

        // Small delay to prevent watchdog issues
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void processRecordedCommand() {
    is_recording = false;

    // The wake word buffer kept filling while the command was recorded. Drop it, or the next
    // search runs over the command's own words and can fire again (we saw 'aldo' -> 'halo').
    if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
        wake_word_buffer_index = 0;
        wake_word_buffer_has_wrapped = false;
        if (wake_word_buffer) {
            memset(wake_word_buffer, 0, WAKE_WORD_BUFFER_SIZE);
        }
        xSemaphoreGive(audioMutex);
    }

    // Play a ding sound to indicate the command was transcribed
    AudioCommand buttonDingCmd;
    buttonDingCmd.type = AudioCommandType::PLAY_BUTTON_DING;
    if (xQueueSend(audioCommandQueue, &buttonDingCmd, 0) != pdTRUE) {
        Serial.println("❌ Failed to queue PLAY_BUTTON_DING command");
    }

    // The audio already went to Gemini while the user spoke (pumpVoiceToGemini() in the main loop);
    // telling it the command is complete is all that's left. This replaced the Deepgram
    // transcription round trip (~3-5 s): Gemini understands the audio directly and answers in ~1 s.
    Serial.printf("🎤 Command recorded: %d bytes\n", command_buffer_index);
    voice_stream_finished = true;
}

// Vibrates the motor for a direction. Main-loop side of the I2C mutex (the button task holds
// it while reading the button).
void hapticsDirection(HazardDirection d, uint16_t durationMs) {
    if (!hapticsAvailable) {
        return;
    }
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100))) {
        haptics.triggerDirection(d, durationMs);
        xSemaphoreGive(i2cMutex);
    }
}

// Button on PCF8574 P3 (LOW = pressed), polled every 20 ms:
//  - held longer than 350 ms: push-to-talk (records while held, sent on release)
//  - 5 short taps, under 1 s apart: SOS alert
// Recording only starts once a press is clearly a hold, so SOS taps no longer each start one.
void buttonTask(void *pvParameters) {
    const unsigned long HOLD_TO_TALK_MS = 350;
    const unsigned long TAP_GAP_MS = 1000;
    const int SOS_TAPS = 5;

    bool pressed = false;
    bool talking = false;
    int taps = 0;
    unsigned long pressStart = 0;
    unsigned long lastTap = 0;

    while (true) {
        bool down = false;
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
            down = !haptics.readPin(BUTTON_PCF_PIN);
            xSemaphoreGive(i2cMutex);
        }
        unsigned long now = millis();

        if (down && !pressed) {
            pressed = true;
            pressStart = now;
        } else if (down && pressed && !talking && now - pressStart > HOLD_TO_TALK_MS) {
            talking = true;
            taps = 0;
            Serial.println("🔘 Push-to-talk: recording while the button is held");
            AudioCommand cmd;
            cmd.type = AudioCommandType::START_RECORDING;
            xQueueSend(audioCommandQueue, &cmd, 0);
        } else if (!down && pressed) {
            pressed = false;
            if (talking) {
                talking = false;
                Serial.println("🔘 Push-to-talk released - sending the command");
                AudioCommand cmd;
                cmd.type = AudioCommandType::STOP_RECORDING_AND_PROCESS;
                xQueueSend(audioCommandQueue, &cmd, 0);
            } else if (now - pressStart > 30) {  // ignore contact bounce
                taps = (now - lastTap < TAP_GAP_MS) ? taps + 1 : 1;
                lastTap = now;
                Serial.printf("🔘 Tap %d/%d\n", taps, SOS_TAPS);
                if (taps >= SOS_TAPS) {
                    taps = 0;
                    sos_requested = true;  // the HTTPS request runs in the main loop
                }
            }
        }
        if (taps > 0 && now - lastTap > TAP_GAP_MS) {
            taps = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Main-loop side of the button: send an SOS requested by the button task
void handleButton() {
    if (!sos_requested) {
        return;
    }
    sos_requested = false;
    Serial.println("🆘 SOS: 5 taps - sending alert");
    // Immediate feedback that the SOS was registered: all three motors
    hapticsDirection(HazardDirection::LEFT, 800);
    hapticsDirection(HazardDirection::CENTER, 800);
    hapticsDirection(HazardDirection::RIGHT, 800);
    sendEmergencyAlert("panic_button", "SOS: el usuario presiono el boton de emergencia 5 veces.");
}

void checkAndAnnounceNearbyPlaces() {
    if (millis() - last_places_check_time < 30000) { // Check every 30 seconds
        return;
    }
    last_places_check_time = millis();

    GPSData current_gps_data = visionAssistant.getCurrentGPSData();
    if (!current_gps_data.isValid) {
        return;
    }

    // Check if user has moved more than 20 meters
    float distance = visionAssistant.calculateDistance(last_checked_gps_data.latitude, last_checked_gps_data.longitude, current_gps_data.latitude, current_gps_data.longitude);
    if (distance < 20.0 && last_checked_gps_data.isValid) {
        return;
    }

    last_checked_gps_data = current_gps_data;

    HTTPClient http;
    String url = "https://maps.googleapis.com/maps/api/place/nearbysearch/json?location=" +
                 String(current_gps_data.latitude, 6) + "," +
                 String(current_gps_data.longitude, 6) +
                 "&radius=50&key=" + String(GEMINI_API_KEY);

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        deserializeJson(doc, payload);

        if (doc["results"].size() > 0) {
            String place_name = doc["results"][0]["name"].as<String>();
            String message = "You are entering " + place_name;
            Serial.println(message);

            AudioCommand cmd;
            cmd.type = AudioCommandType::SPEAK_TEXT;
            strncpy(cmd.text, message.c_str(), sizeof(cmd.text) - 1);
            cmd.text[sizeof(cmd.text) - 1] = '\0';
            if (xQueueSend(audioCommandQueue, &cmd, 0) != pdTRUE) {
                Serial.println("❌ Failed to queue SPEAK_TEXT command for nearby place");
            }
        }
    }
    http.end();
}

// Queries the shared hazard network (api/hazards.js) for verified hazards near the
// user's current GPS position, and pulses the haptic motors if one is close.
//
// NOTE: this firmware has no compass/heading source (TinyGPS++'s course-over-ground
// requires sustained movement and isn't reliable at walking speed), so unlike the
// Gemini-camera alerts we can't reliably compute whether the hazard is to the
// user's left/right - it always pulses MOTOR_CENTER as a general "hazard nearby"
// warning. If a compass module is added later, replace the CENTER trigger below
// with a bearing calculation (see firmware/haptic/INTEGRACION.md, section 3).
void checkNearbyHazards() {
    const unsigned long HAZARD_CHECK_INTERVAL = 5000; // ms between network checks
    const float HAZARD_ALERT_RADIUS_M = 15.0;         // matches MERGE_RADIUS_M in api/hazards.js

    if (millis() - last_hazard_check_time < HAZARD_CHECK_INTERVAL) {
        return;
    }
    last_hazard_check_time = millis();

    if (!hapticsAvailable || WiFi.status() != WL_CONNECTED) {
        return;
    }

    GPSData current_gps_data = visionAssistant.getCurrentGPSData();
    if (!current_gps_data.isValid) {
        return;
    }
    last_hazard_check_gps = current_gps_data;

    HTTPClient http;
    String url = String(HAZARDS_API_URL) + "/api/hazards?lat=" +
                 String(current_gps_data.latitude, 6) + "&lon=" +
                 String(current_gps_data.longitude, 6) + "&radius=100";

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error && doc.is<JsonArray>() && doc.size() > 0) {
            float closestDistance = doc[0]["distance_m"].as<float>();
            String category = doc[0]["category"].as<String>();
            if (closestDistance <= HAZARD_ALERT_RADIUS_M) {
                Serial.printf("⚠️ Shared hazard nearby: %s (%.1fm away)\n", category.c_str(), closestDistance);
                hapticsDirection(HazardDirection::CENTER);
            }
        }
    } else if (httpResponseCode > 0) {
        Serial.printf("Hazard network check failed, HTTP %d\n", httpResponseCode);
    }
    http.end();
}

String stripHtmlTags(const String& html) {
    String text = "";
    bool inTag = false;
    for (char c : html) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            text += c;
        }
    }
    return text;
}

// Forwards the command being recorded to Gemini as 8 kHz audio, every 200 ms while the user talks,
// so the upload overlaps the speech instead of following it. Runs in the main loop because the
// WebSocket isn't thread-safe.
void pumpVoiceToGemini() {
    static bool started = false;
    static unsigned long lastPump = 0;

    // After a reconnect, an unanswered spoken command is re-streamed from the recording
    if (visionAssistant.takeVoiceResendRequest() && !voice_stream_active && !is_recording) {
        voice_stream_sent = VOICE_STREAM_SKIP_BYTES;
        voice_stream_finished = true;
        voice_stream_active = true;
    }

    if (!voice_stream_active) {
        return;
    }
    if (!started) {
        visionAssistant.beginVoiceCommand();
        started = true;
    }
    // Hold the audio (it keeps accumulating in command_buffer) until Gemini can take the voice turn
    if (!visionAssistant.voiceTurnReady()) {
        return;
    }
    if (!voice_stream_finished && millis() - lastPump < 200) {
        return;
    }
    lastPump = millis();

    // Send everything recorded since last time, a bounded chunk per call
    const int MAX_CHUNK_BYTES = 16000;  // 0.5 s at 16 kHz
    int recorded = 0;
    int16_t* ds = nullptr;
    size_t dsSamples = 0;
    if (xSemaphoreTake(audioMutex, portMAX_DELAY)) {
        recorded = command_buffer_index;
        int from = voice_stream_sent;
        int to = min(recorded, from + MAX_CHUNK_BYTES);
        to -= (to - from) % 4;  // whole pairs of 16-bit samples
        if (to > from) {
            dsSamples = (to - from) / 4;
            ds = (int16_t*)ps_malloc(dsSamples * sizeof(int16_t));
            if (ds) {
                const int16_t* in = (const int16_t*)(command_buffer + from);
                for (size_t i = 0; i < dsSamples; i++) {
                    ds[i] = (int16_t)(((int32_t)in[2 * i] + in[2 * i + 1]) / 2);  // 16 -> 8 kHz
                }
                voice_stream_sent = to;
            }
        }
        xSemaphoreGive(audioMutex);
    }
    if (ds) {
        visionAssistant.sendVoiceAudio(ds, dsSamples);
        free(ds);
    }

    if (voice_stream_finished && voice_stream_sent + 4 > recorded) {
        visionAssistant.endVoiceCommand();
        voice_stream_active = false;
        started = false;
    }
}

void loop() {
    // Ensure we don't start processing until setup is complete
    static bool setup_done = false;
    if (!setup_done) {
        static unsigned long setup_start = millis();
        if (millis() - setup_start < 2000) {  // Wait 2 seconds after setup
            delay(10);
            return;
        }
        setup_done = true;
        Serial.printf("Setup delay complete, starting main loop processing on core %d\n", xPortGetCoreID());
    }
    
    // Handle button presses for push-to-talk and SOS
    handleButton();

#ifdef TEST_HAPTICS
    // Test-only build flag: once setup is done, pulse left, center, right (twice) to check the motors
    // on the real board without waiting for Gemini to report a hazard
    static int hapticTestStep = 0;
    static unsigned long nextHapticTest = millis() + 3000;
    if (hapticsAvailable && hapticTestStep < 6 && millis() > nextHapticTest) {
        const HazardDirection dirs[] = {HazardDirection::LEFT, HazardDirection::CENTER, HazardDirection::RIGHT};
        const char* names[] = {"IZQUIERDA", "CENTRO", "DERECHA"};
        Serial.printf("🧪 Test motor %s\n", names[hapticTestStep % 3]);
        hapticsDirection(dirs[hapticTestStep % 3], 600);
        hapticTestStep++;
        nextHapticTest = millis() + 1500;
    }
#endif

#ifdef TEST_COMMAND_AFTER_MS
#ifndef TEST_COMMAND_TEXT
#define TEST_COMMAND_TEXT "Dime qué ves."
#endif
    // Test-only build flag: inject one voice command so the Gemini round trip can be timed without
    // speaking (serial input can't be used: UART0 RX, GPIO3, is taken by the GPS)
    // Injects 4 commands, 30 s apart, to also catch problems that only show up after a few rounds
    static int testCommandsSent = 0;
    if (testCommandsSent < 4 && millis() > TEST_COMMAND_AFTER_MS + testCommandsSent * 30000UL) {
        testCommandsSent++;
        Serial.printf("🧪 Injecting test command %d\n", testCommandsSent);
        visionAssistant.queueUserCommand(TEST_COMMAND_TEXT);
    }
#endif

    // Run the vision assistant (handles WebSocket communication, GPS updates, and frame processing)
    visionAssistant.run();

    // Stream a spoken command to Gemini while it's being recorded
    pumpVoiceToGemini();

    // Check for nearby places
    checkAndAnnounceNearbyPlaces();

    // Check the shared hazard network and turn off any haptic motors whose pulse ended
    checkNearbyHazards();
    if (hapticsAvailable) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50))) {
            haptics.update();
            xSemaphoreGive(i2cMutex);
        }
    }

    // Check for commands from the audio task
    CommandMessage cmdMsg;
    if (xQueueReceive(commandQueue, &cmdMsg, 0) == pdTRUE) {
        Serial.printf("Received command from audio core: %s\n", cmdMsg.command);
        
        // Play button ding sound to indicate command was transcribed and is being processed
        visionAssistant.sendTextMessage(String(cmdMsg.command)); // Convert to String only when needed locally
    }
    
    // Print GPS status every 30 seconds
    static unsigned long lastGPSStatus = 0;
    if (millis() - lastGPSStatus > 30000) {
        GPSData gpsData = visionAssistant.getCurrentGPSData();
        if (gpsData.isValid) {
            Serial.println("GPS Status: " + visionAssistant.getGPSString());
        } else {
            Serial.println("GPS Status: No fix obtained");
        }
        // TLS needs contiguous byte-addressable internal RAM; watch it here. (ESP.getMaxAllocHeap()
        // reported a 32KB IRAM block TLS can't use, so it always read 32756 even when TLS failed.)
        // RSSI too: WiFi drops looked like memory errors until we checked the link itself.
        Serial.printf("Heap: internal free %u, largest usable block %u, min ever %u | PSRAM free %u | WiFi %s, RSSI %d dBm\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      ESP.getMinFreeHeap(), ESP.getFreePsram(),
                      WiFi.status() == WL_CONNECTED ? "connected" : "DOWN", WiFi.RSSI());
        lastGPSStatus = millis();
    }
    
    // Refresh language settings every 5 minutes
    static unsigned long lastLanguageUpdate = 0;
    if (millis() - lastLanguageUpdate > 300000) { // 5 minutes
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("🔄 Refreshing language settings...");
            String currentLanguage = settingsManager.getLanguage();
            deepgramClient.setDefaultLanguage(currentLanguage);
            tts.setDefaultLanguage(currentLanguage);
        }
        lastLanguageUpdate = millis();
    }
    
    // Set a 2-second delay in the main loop to control the Gemini sending rate.
    // Small delay to prevent watchdog issues
    delay(10);
}
