#ifndef VISION_ASSISTANT_H
#define VISION_ASSISTANT_H

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "gps_module.h"
#include "TTS.h"

// Forward declarations
class VisionAssistant;

// Callback function types
typedef void (*ResponseCallback)(const String& response);
typedef void (*ToolCallback)(const String& toolName, const String& message);

class VisionAssistant {
private:
    static VisionAssistant* instance;
    
    WebSocketsClient ws;
    GPSModule gps;
    bool setupComplete;
    bool systemPromptSent;
    unsigned long lastFrameTime;
    unsigned long lastGPSUpdate;
    ResponseCallback responseCallback;
    ToolCallback toolCallback;
    
    // Queue system for user commands
    struct QueuedCommand {
        String message;
        unsigned long timestamp;
    };
    static const int MAX_QUEUED_COMMANDS = 5;
    QueuedCommand commandQueue[MAX_QUEUED_COMMANDS];
    int queueHead;
    int queueTail;
    int queueSize;

    // User's language from the app settings; Gemini writes spoken messages in it
    String responseLanguage;

    String currentAddress;
    bool hasLocationOverride = false;
    double overrideLat = 0;
    double overrideLon = 0;

    // Which network we're on (0 = WIFI_SSID, 1 = WIFI_SSID_2) and since when the link is down
    int wifiNetwork = 0;
    unsigned long wifiDownSince = 0;
    static const unsigned long WIFI_SWITCH_AFTER_MS = 20000;
    // ...or after this many drops within the window (link keeps coming back and dropping)
    static const int WIFI_DROPS_TO_SWITCH = 4;
    static const unsigned long WIFI_DROP_WINDOW_MS = 90000;
    bool wifiWasUp = false;
    int wifiDrops = 0;
    unsigned long wifiDropWindowStart = 0;

    // When a frame carried a user voice command, hold further frames until Gemini answers:
    // each frame is a complete turn, and a new turn interrupts the reply to the previous one
    volatile unsigned long awaitingCommandReplySince = 0;  // 0 = not waiting
    // Text commands (sent inside a frame turn): hold briefly, the next frame used to release a stuck
    // reply. Audio commands: hold until the reply, since a new frame turn interrupts the answer
    // being generated (seen as "interrupted": true and a 10 s delay); stalls are caught separately.
    static const unsigned long TEXT_COMMAND_REPLY_TIMEOUT = 3000;
    static const unsigned long VOICE_COMMAND_REPLY_TIMEOUT = 10000;
    unsigned long commandReplyTimeout = TEXT_COMMAND_REPLY_TIMEOUT;

    // Set by the audio task while it downloads/plays speech: frame uploads (~35KB every 2 s)
    // share the WiFi link and slowed the speech download to a crawl
    volatile bool framesPaused = false;

    // The user's last command, kept until Gemini answers it. Unlike awaitingCommandReplySince
    // (which only holds frames for a few seconds) this stays set until the reply or a timeout, so
    // wake word searches don't load the link meanwhile, and the command can be resent if the
    // connection is restarted.
    String pendingCommand;
    volatile unsigned long commandPendingSince = 0;  // 0 = none
    static const unsigned long COMMAND_PENDING_TIMEOUT = 20000;

    // Spoken commands go to Gemini as audio (no Deepgram transcription: ~3-5 s saved). While one
    // is being streamed, camera frames are held: each frame is a complete turn and would cut it.
    volatile bool voiceCommandActive = false;
    bool voiceCommandPending = false;     // audio command sent, no reply yet (resent after a reconnect)
    volatile bool voiceResendRequested = false;
    int voiceResends = 0;                 // a spoken command is resent at most once
    // What Gemini transcribed from the spoken command. Gemini Live sometimes hears an audio turn
    // (transcribes it) and then never answers it; resending the same words as a text command
    // inside a frame turn gets an answer, faster than reconnecting.
    String voiceHeardText;
    static const unsigned long VOICE_TEXT_FALLBACK_AFTER = 4000;
    // The voice turn is opened (activityStart) only once the last frame turn is answered: audio
    // sent while Gemini was still on a frame got folded into that turn and never answered.
    bool voiceTurnOpen = false;
    bool frameTurnInFlight = false;
    unsigned long frameTurnSentAt = 0;
    static const unsigned long FRAME_TURN_WAIT = 4000;

    // Last time anything arrived from Gemini. After a spoken command the TLS link sometimes stalls
    // for ~14 s (looks like TCP retransmission backoff) while frames keep going out unanswered;
    // a fresh connection is faster than waiting it out.
    unsigned long lastServerMessageAt = 0;
    static const unsigned long STALL_TIMEOUT = 6000;

    // Frame processing constants
    static const unsigned long FRAME_INTERVAL = 2000; // 2 seconds between frames (faster for user commands)
    static const unsigned long GPS_UPDATE_INTERVAL = 1000; // 1 second between GPS updates
    static const size_t MAX_FRAME_SIZE = 50000; // Maximum frame size in bytes
    
public:
    VisionAssistant();
    ~VisionAssistant();
    
    // Initialization and main loop
    bool initialize();
    void run();
    
    // Callback management
    void setResponseCallback(ResponseCallback callback);
    void setToolCallback(ToolCallback callback);

    // Must be set before the WebSocket connects (it goes into the setup message)
    void setResponseLanguage(const String& language);

    // Street address for the current GPS fix (main loop looks it up), sent with every frame so
    // Gemini can answer "where am I?"
    void setCurrentAddress(const String& address) { currentAddress = address; }

    // Demo / test builds with a simulated GPS: frames report this position instead of the
    // module's (indoors it has no fix, and Gemini kept saying it had no location)
    void setLocationOverride(double lat, double lon) {
        overrideLat = lat;
        overrideLon = lon;
        hasLocationOverride = true;
    }

    // A user command is queued, being spoken or sent, and Gemini hasn't answered it yet
    bool isAwaitingCommandReply() const { return commandPendingSince != 0 || queueSize > 0 || voiceCommandActive; }

    // Spoken command streamed as audio (call from the main loop only: the WebSocket isn't thread-safe).
    // begin: holds frames, sends the current view and activityStart. send: 8 kHz 16-bit mono PCM.
    // end: activityEnd; Gemini then answers with a systemAction like for a text command.
    void beginVoiceCommand();
    // Opens the voice turn as soon as Gemini is free; false while still waiting (keep the audio)
    bool voiceTurnReady();
    void sendVoiceAudio(const int16_t* pcm8k, size_t samples);
    void endVoiceCommand();
    // True once after a reconnect if an audio command was still unanswered (caller re-streams it)
    bool takeVoiceResendRequest() {
        bool r = voiceResendRequested;
        voiceResendRequested = false;
        return r;
    }

    // Drops the Gemini connection to free its TLS memory (~40KB) for something more urgent, like
    // an SOS request; ws.loop() reconnects on its own afterwards
    void dropConnectionForMemory() {
        Serial.println("Dropping the Gemini connection to free memory for an urgent request");
        setupComplete = false;
        ws.disconnect();
    }

    void setFramesPaused(bool paused) {
        framesPaused = paused;
        if (!paused) {
            // No frames went out while paused, so the silence from Gemini meanwhile isn't a stall
            lastServerMessageAt = millis();
        }
    }

    // Frame processing
    void processFrame();
    bool isSetupComplete() const;
    
    // GPS access
    GPSData getCurrentGPSData() const;
    String getGPSString() const;

    // Send a text message to Gemini (queues it to be sent with next frame)
    void sendTextMessage(const String& message);

    // GPS distance calculation
    float calculateDistance(float lat1, float lon1, float lat2, float lon2);
    
    // Queue management
    void queueUserCommand(const String& command);
    bool hasQueuedCommands() const;
    String getNextQueuedCommand();
    
    // Static callback for WebSocket events
    static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
    
    // Response handling
    static void onGeminiResponse(const String& response);
    
private:
    // Internal helper methods
    bool connectToWiFi();
    bool beginWiFiNetwork(int n);
    bool initializeCamera();
    bool initializeGPS();
    bool initializeWebSocket();
    void sendSetupMessage();
    bool sendFrameRealtime();  // current camera view as realtimeInput video (for spoken commands)
    void sendToolResponse(const char* functionId, const char* functionName, const char* result);
    void handleWebSocketMessage(const JsonDocument& doc);
};

#endif
