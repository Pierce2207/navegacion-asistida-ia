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

    // When a frame carried a user voice command, hold further frames until Gemini answers:
    // each frame is a complete turn, and a new turn interrupts the reply to the previous one
    volatile unsigned long awaitingCommandReplySince = 0;  // 0 = not waiting
    // Normally the reply takes ~1.5 s. After a *spoken* command (transcription + ding + mic restart
    // all happening at once) the reply sometimes doesn't come until the next frame is sent, so
    // don't hold frames for long: sending the next one releases it.
    static const unsigned long COMMAND_REPLY_TIMEOUT = 3000;

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

    // A user command is queued or sent and Gemini hasn't answered it yet
    bool isAwaitingCommandReply() const { return commandPendingSince != 0 || queueSize > 0; }

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
    bool initializeCamera();
    bool initializeGPS();
    bool initializeWebSocket();
    void sendSetupMessage();
    void sendToolResponse(const char* functionId, const char* functionName, const char* result);
    void handleWebSocketMessage(const JsonDocument& doc);
};

#endif
