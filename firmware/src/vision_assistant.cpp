#include "vision_assistant.h"
#include "TTS.h"

#include <WiFi.h>

#include "base64.h"
#include "camera_pins.h"
#include "camera_setup.h"
#include "gemini_config.h"
#include "secrets.h"

VisionAssistant *VisionAssistant::instance = nullptr;

VisionAssistant::VisionAssistant() : setupComplete(false), systemPromptSent(false), lastFrameTime(0), lastGPSUpdate(0), responseCallback(nullptr), toolCallback(nullptr), queueHead(0), queueTail(0), queueSize(0) {
    instance = this;  // Set static instance for callbacks
}

VisionAssistant::~VisionAssistant() {
    instance = nullptr;
}

bool VisionAssistant::initialize() {
    Serial.begin(115200);
    Serial.println("Initializing Vision Assistant...");

    // Camera and GPS only on the first attempt: setup() retries this whole function
    // until WiFi connects, and re-running esp_camera_init fails with ESP_ERR_INVALID_STATE.
    static bool cameraReady = false;
    if (!cameraReady) {
        if (!initializeCamera()) {
            Serial.println("Failed to initialize camera");
            return false;
        }
        cameraReady = true;
    }

    static bool gpsAttempted = false;
    if (!gpsAttempted) {
        gpsAttempted = true;
        if (!initializeGPS()) {
            Serial.println("Warning: Failed to initialize GPS - continuing without GPS");
            // Don't return false here, GPS is optional
        }
    }

    // Connect to WiFi
    if (!connectToWiFi()) {
        Serial.println("Failed to connect to WiFi");
        return false;
    }

    // Initialize WebSocket
    if (!initializeWebSocket()) {
        Serial.println("Failed to initialize WebSocket");
        return false;
    }

    // Set default response callback
    setResponseCallback(onGeminiResponse);


    Serial.println("Vision Assistant initialized successfully!");
    return true;
}

void VisionAssistant::run() {
    // Handle WebSocket communication
    ws.loop();

    // Update GPS data
    unsigned long currentTime = millis();
    if (currentTime - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
        gps.update();
        lastGPSUpdate = currentTime;
    }

    if (commandPendingSince != 0 && currentTime - commandPendingSince > COMMAND_PENDING_TIMEOUT) {
        Serial.println("Gave up waiting for a reply to the user command");
        commandPendingSince = 0;
        pendingCommand = "";
    }

    // Frames have kept going out for STALL_TIMEOUT with nothing coming back: restart the link
    if (setupComplete && lastServerMessageAt != 0 && lastFrameTime > lastServerMessageAt + STALL_TIMEOUT) {
        Serial.printf("⚠️ Gemini link stalled (nothing received for %lu ms while sending frames) - reconnecting\n",
                      currentTime - lastServerMessageAt);
        setupComplete = false;
        lastServerMessageAt = currentTime;  // don't re-trigger while the new connection comes up
        ws.disconnect();                    // ws.loop() reconnects; the pending command is resent on setupComplete
        return;
    }

    if (framesPaused) {
        return;  // speech is being downloaded/played
    }

    if (awaitingCommandReplySince != 0) {
        if (currentTime - awaitingCommandReplySince < COMMAND_REPLY_TIMEOUT) {
            return;  // still waiting for the answer to the user's question
        }
        Serial.println("No reply to user command within timeout, resuming frames");
        awaitingCommandReplySince = 0;
    }

    // Check if it's time to process a new frame
    if (currentTime - lastFrameTime >= FRAME_INTERVAL) {
        processFrame();
        lastFrameTime = currentTime;
    }
}

void VisionAssistant::setResponseCallback(ResponseCallback callback) {
    responseCallback = callback;
}

void VisionAssistant::setToolCallback(ToolCallback callback) {
    toolCallback = callback;
}

void VisionAssistant::setResponseLanguage(const String& language) {
    responseLanguage = language;
}

void VisionAssistant::processFrame() {
    // Wait until WebSocket is connected and setup is complete
    if (!setupComplete) {
        Serial.println(ws.isConnected() ? "WebSocket connected but setupComplete is false" : "WebSocket not connected and setupComplete is false");
        return;
    }

    // Capture frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Check frame size
    if (fb->len > MAX_FRAME_SIZE) {
        Serial.printf("Frame too large (%zu bytes), skipping\n", fb->len);
        esp_camera_fb_return(fb);
        return;
    }

    // Encode and send frame with GPS data
    String frameB64 = base64_encode(fb->buf, fb->len);
    Serial.printf("Frame captured: %zu bytes, Base64 length: %d\n", fb->len, frameB64.length());

    // Get GPS data
    GPSData gpsData = gps.getGPSData();
    String gpsText = "";
    
    if (gpsData.isValid && gps.hasValidFix()) {
        gpsText = "Current GPS location: Latitude " + String(gpsData.latitude, 6) + 
                 ", Longitude " + String(gpsData.longitude, 6) + 
                 ", Altitude " + String(gpsData.altitude, 1) + "m. ";
        Serial.printf("Including GPS data: %s\n", gpsText.c_str());
    } else {
        gpsText = "GPS location not available. ";
        Serial.println("GPS data not available or not recent");
    }

    // Create message with both image and GPS context
    String msg = "{\"client_content\":{\"turn_complete\":true,\"turns\":[{\"role\":\"user\",\"parts\":[";
    
    // Add GPS text if available
    if (gpsText.length() > 0) {
        msg += "{\"text\":\"" + gpsText + "\"},";
    }
    
    // Check for queued user commands and include them
    if (hasQueuedCommands()) {
        String userCommand = getNextQueuedCommand();
        Serial.printf("Including queued user command: %s\n", userCommand.c_str());
        awaitingCommandReplySince = millis();
        commandPendingSince = millis();
        pendingCommand = userCommand;
        // Restating the rule next to the command: the system prompt alone wasn't enough, and the
        // model kept answering direct questions with a silent 'log' about the camera view
        msg += "{\"text\":\"USER VOICE COMMAND (the user is asking you directly: answer out loud with "
               "intent=voice_query and shouldSpeak=true, even if the view is dark or blocked): " + userCommand + "\"},";
    }
    
    // Add image data
    msg += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"" + frameB64 + "\"}}";
    msg += "]}]}}";

    bool sent = ws.sendTXT(msg);
    if (!sent) {
        Serial.println("Failed to send frame to Gemini");
    }

    esp_camera_fb_return(fb);
}

bool VisionAssistant::isSetupComplete() const {
    return setupComplete;
}

bool VisionAssistant::connectToWiFi() {
    // Log the router's disconnect reason: "status 6" alone doesn't say whether it was the password
    static bool reasonLoggerAdded = false;
    if (!reasonLoggerAdded) {
        reasonLoggerAdded = true;
        WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            // 15/202/204 = handshake/auth failure (wrong password); 201 = AP not found;
            // 2/4 = auth/association expired (weak signal or the router dropped us, not the password)
            const char* hint = "";
            if (reason == 15 || reason == 202 || reason == 204) hint = " (authentication failed - WIFI_PASSWORD is probably wrong)";
            else if (reason == 201) hint = " (network not found - out of range or router off)";
            else if (reason == 2 || reason == 4) hint = " (connection expired - weak signal or router dropped us)";
            Serial.printf("\nWiFi disconnected, reason %u%s\n", reason, hint);
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        wl_status_t status = WiFi.status();
        Serial.printf("\nFailed to connect to WiFi (status %d", status);
        if (status == WL_NO_SSID_AVAIL) Serial.print(": network not found - check WIFI_SSID, and that it is 2.4 GHz");
        else if (status == WL_CONNECT_FAILED) Serial.print(": connection rejected - check WIFI_PASSWORD");
        Serial.println(")");

        // List visible networks so a wrong or 5 GHz SSID is obvious from the serial log
        WiFi.disconnect();
        int n = WiFi.scanNetworks();
        Serial.printf("Visible 2.4 GHz networks (%d):\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %s (%d dBm)%s\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                          WiFi.SSID(i) == WIFI_SSID ? "  <- configured" : "");
        }
        WiFi.scanDelete();
        return false;
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Signal: %d dBm (above -70 is fine; below -80 drops are likely)\n", WiFi.RSSI());
    return true;
}

bool VisionAssistant::initializeCamera() {
    setupCamera();
    return true;  // Assuming setupCamera() handles its own error checking
}

bool VisionAssistant::initializeWebSocket() {
    Serial.println("Initializing WebSocket...");
    ws.beginSSL(WS_HOST, WS_PORT, WS_PATH.c_str());
    ws.onEvent(webSocketEvent);
    ws.setReconnectInterval(1500);  // short: we also force reconnects when the link stalls
    return true;
}

bool VisionAssistant::initializeGPS() {
    Serial.println("Initializing GPS module...");
    
    if (!gps.initialize()) {
        Serial.println("GPS initialization failed");
        return false;
    }
    
    Serial.println("GPS module initialized successfully");
    return true;
}

void VisionAssistant::sendSetupMessage() {
    // NOTE: This should be set to low only when testing.
    // const char* mediaResolution = "MEDIA_RESOLUTION_HIGH";
    const char* mediaResolution = "MEDIA_RESOLUTION_LOW";
    // String setupMsg = "{\"setup\":{\"model\":\"models/gemini-2.0-flash-live-001\",\"generationConfig\":{\"responseModalities\":[\"TEXT\"], \"mediaResolution\":\"" + String(mediaResolution) + "\"},\"tools\":[" + String(TOOLS_JSON) + "],\"systemInstruction\":{\"parts\":[{\"text\":\"" + String(SYSTEM_PROMPT) + "\"}]}}}";
    // Current Live models reject TEXT ("response modalities (TEXT) is not supported"), so we ask
    // for AUDIO. The system prompt forces every reply to be a systemAction tool call, so in
    // practice no audio comes back and replies are still spoken through Deepgram TTS.
    // The prompt is a multi-line raw string, so it must be JSON-escaped before embedding.
    String prompt = String(SYSTEM_PROMPT);
    prompt.replace("\\", "\\\\");
    prompt.replace("\"", "\\\"");
    prompt.replace("\r", "");
    prompt.replace("\n", "\\n");
    prompt.replace("\t", "\\t");
    // The examples in the prompt are in English; without this, spoken messages stay in English
    if (responseLanguage.startsWith("es")) {
        prompt += "\\n\\nLANGUAGE: The user speaks Spanish. Write every 'message' value in Spanish "
                  "(keep intent names and other field values in English). User voice commands arrive in Spanish.";
    }
    String setupMsg = "{\"setup\":{\"model\":\"models/gemini-3.1-flash-live-preview\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"], \"mediaResolution\":\"" + String(mediaResolution) + "\"},\"tools\":[" + String(TOOLS_JSON) + "],\"systemInstruction\":{\"parts\":[{\"text\":\"" + prompt + "\"}]}}}";
    ws.sendTXT(setupMsg);
    Serial.println("Sent setup message");
}

void VisionAssistant::sendToolResponse(const char *functionId, const char *functionName, const char *result) {
    // Create tool response according to BidiGenerateContentToolResponse format
    String toolResponseMsg = "{\"toolResponse\":{\"functionResponses\":[{";

    if (functionId) {
        toolResponseMsg += "\"id\":\"" + String(functionId) + "\",";
    }

    toolResponseMsg += "\"name\":\"" + String(functionName) + "\",";
    toolResponseMsg += "\"response\":{\"output\":\"" + String(result) + "\"}";
    toolResponseMsg += "}]}}";

    bool sent = ws.sendTXT(toolResponseMsg);
    if (sent) {
        Serial.printf("Sent tool response for %s\n", functionName);
    } else {
        Serial.printf("Failed to send tool response for %s\n", functionName);
    }
}

void VisionAssistant::handleWebSocketMessage(const JsonDocument &doc) {
    // Serial.println("Received WebSocket message:");
    // serializeJsonPretty(doc, Serial);
    // Serial.println();
    // Check for setupComplete signal
    if (doc.containsKey("setupComplete")) {
        Serial.println("Setup complete - ready to send frames");
        setupComplete = true;
        systemPromptSent = true;
        // A new session doesn't know about a question asked on the previous (stalled) connection
        if (commandPendingSince != 0 && pendingCommand.length() > 0 && queueSize == 0) {
            Serial.printf("Resending pending user command: %s\n", pendingCommand.c_str());
            queueUserCommand(pendingCommand);
        }
        return;
    }

    // serializeJsonPretty(doc, Serial);
    // Serial.println();
    // Handle toolCall at top level
    if (doc.containsKey("toolCall")) {
        JsonObjectConst toolCall = doc["toolCall"];
        if (toolCall.containsKey("functionCalls")) {
            Serial.println("Function calls detected");
            JsonArrayConst functionCalls = toolCall["functionCalls"];
            for (JsonObjectConst funcCall : functionCalls) {
                const char *toolName = funcCall["name"];
                const char *toolId = funcCall["id"];
                Serial.printf("Tool call detected: %s (ID: %s)\n", toolName, toolId ? toolId : "N/A");
                if (toolName && strcmp(toolName, "systemAction") == 0) {
                    // For systemAction, we need to pass the entire args object as a JSON string
                    JsonObjectConst args = funcCall["args"];
                    String argsJson;
                    serializeJson(args, argsJson);
                    
                    if (toolCallback) {
                        Serial.printf("Tool call: %s(%s)\n", toolName, argsJson.c_str());
                        toolCallback(toolName, argsJson);

                        // Send tool response back to Gemini
                        sendToolResponse(toolId, toolName, "System action processed successfully");
                    }
                }
            }
        } else {
            Serial.println("No function calls found in toolCall");
        }
        // Gemini answered; frames can flow again. Cleared only after the callbacks so any reply to
        // speak is already queued (the audio task checks both before starting a wake word search).
        awaitingCommandReplySince = 0;
        commandPendingSince = 0;
        pendingCommand = "";
        return;
    }

    // Handle model text response
    if (doc.containsKey("serverContent") && doc["serverContent"].containsKey("modelTurn")) {
        JsonObjectConst modelTurn = doc["serverContent"]["modelTurn"];
        if (modelTurn["parts"][0].containsKey("text")) {
            const char *text = modelTurn["parts"][0]["text"];
            if (text && responseCallback) {
                String response = String(text);
                Serial.printf("Gemini: %s\n", text);
                responseCallback(response);
            }
        } else {
            Serial.println("No text in modelTurn response");
        }
    } else {
        // TODO: DETECT TURNS
        // Serial.println("No serverContent or modelTurn in response");
        // serializeJsonPretty(doc, Serial);
        // Serial.println();
    }
}

void VisionAssistant::webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
    if (!instance)
        return;

    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("[WSc] Disconnected!");
            instance->setupComplete = false;
            instance->systemPromptSent = false;
            break;

        case WStype_CONNECTED: {
            // The URL carries the API key as a query param, so don't log it
            Serial.println("[WSc] Connected to Gemini Live");
            instance->lastServerMessageAt = millis();
            instance->setupComplete = false;
            instance->systemPromptSent = false;
            instance->sendSetupMessage();
            break;
        }

        case WStype_TEXT: {
            instance->lastServerMessageAt = millis();
            Serial.printf("[WSc] Received text: %s\n", (char*)payload);
            JsonDocument doc;
            auto error = deserializeJson(doc, payload, length);
            if (error) {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                // Print the raw payload to see what we received
                Serial.print("Raw payload: ");
                Serial.write(payload, length);
                Serial.println();
                return;
            }

            instance->handleWebSocketMessage(doc);
            break;
        }

        case WStype_BIN: {
            instance->lastServerMessageAt = millis();
            Serial.printf("[WSc] Received binary data: %zu bytes\n", length);
            if (length < 200) {
                // Short control messages (turnComplete, generationComplete...) are useful when debugging timing
                Serial.print("[WSc]   ");
                Serial.write(payload, length);
                Serial.println();
            }
            JsonDocument doc;
            auto error = deserializeJson(doc, payload, length);
            if (error) {
                Serial.print("deserializeJson() failed: ");
                Serial.println(error.c_str());
                // Print the raw payload to see what we received
                Serial.print("Raw payload: ");
                Serial.write(payload, length);
                Serial.println();
                return;
            }

            instance->handleWebSocketMessage(doc);
            break;
        }

        case WStype_ERROR: {
            Serial.printf("[WSc] Error: %s\n", (char*)payload);
            break;
        }

        default:
            break;
    }
}

GPSData VisionAssistant::getCurrentGPSData() const {
    return gps.getGPSData();
}

String VisionAssistant::getGPSString() const {
    return gps.getLocationString();
}

void VisionAssistant::onGeminiResponse(const String &response) {
    // This is the default callback for Gemini text responses
    // Users can set their own callback using setResponseCallback()
    // For now, just log it (already logged in handleWebSocketMessage)
    // You could add additional processing here like:
    // - Parse object detection results
    // - Trigger actions based on detected objects
    // - Store responses for analysis
    // - Send responses to other systems
    Serial.println("Default response handler - response already processed");
}

void VisionAssistant::queueUserCommand(const String& command) {
    if (queueSize >= MAX_QUEUED_COMMANDS) {
        Serial.println("Command queue full - dropping oldest command");
        // Remove oldest command (FIFO)
        queueHead = (queueHead + 1) % MAX_QUEUED_COMMANDS;
        queueSize--;
    }
    
    // Add new command to tail
    commandQueue[queueTail].message = command;
    commandQueue[queueTail].timestamp = millis();
    queueTail = (queueTail + 1) % MAX_QUEUED_COMMANDS;
    queueSize++;
    
    Serial.printf("Queued command: %s (queue size: %d)\n", command.c_str(), queueSize);
}

bool VisionAssistant::hasQueuedCommands() const {
    return queueSize > 0;
}

String VisionAssistant::getNextQueuedCommand() {
    if (queueSize == 0) {
        return "";
    }
    
    String command = commandQueue[queueHead].message;
    queueHead = (queueHead + 1) % MAX_QUEUED_COMMANDS;
    queueSize--;
    
    Serial.printf("Dequeued command: %s (remaining: %d)\n", command.c_str(), queueSize);
    return command;
}

void VisionAssistant::sendTextMessage(const String& message) {
    if (!setupComplete) {
        Serial.println("WebSocket not ready - queueing user command for next frame.");
        queueUserCommand(message);
        return;
    }

    // Queue the user command to be sent with the next frame
    queueUserCommand(message);
    Serial.printf("Queued user command for next frame: %s\n", message.c_str());
}

float VisionAssistant::calculateDistance(float lat1, float lon1, float lat2, float lon2) {
    float R = 6371000; // Earth radius in meters
    float phi1 = lat1 * M_PI / 180;
    float phi2 = lat2 * M_PI / 180;
    float deltaPhi = (lat2 - lat1) * M_PI / 180;
    float deltaLambda = (lon2 - lon1) * M_PI / 180;

    float a = sin(deltaPhi / 2) * sin(deltaPhi / 2) +
              cos(phi1) * cos(phi2) *
              sin(deltaLambda / 2) * sin(deltaLambda / 2);
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return R * c;
}
