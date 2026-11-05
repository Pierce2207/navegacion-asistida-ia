#include "navigation.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// Guidance thresholds. Phone-grade GPS is good to ~5-10 m, so these leave some margin.
static const double ANNOUNCE_M = 25.0;    // speak the maneuver when this close to it
static const double REACHED_M = 12.0;     // then move on to the next one
static const double OFF_ROUTE_M = 40.0;   // farther than this past the closest point -> reroute
static const unsigned long REROUTE_COOLDOWN_MS = 30000;
static const unsigned long REMINDER_MS = 60000;  // say something if nothing was said for a minute
static const unsigned long UPDATE_EVERY_MS = 1000;

void Navigator::begin(const char* base, SpeakFn speakFn, VibrateFn vibrateFn, NetworkFn networkFn) {
    apiBase = base;
    speak = speakFn;
    vibrate = vibrateFn;
    network = networkFn;
}

void Navigator::requestRoute(const String& destination) {
    pendingDestination = destination;
}

void Navigator::stop(bool announce) {
    bool wasActive = active;
    active = false;
    steps.clear();
    pendingDestination = "";
    if (announce && wasActive) {
        speak("Navegación cancelada.");
    }
}

bool Navigator::nextStepLocation(double& lat, double& lon) const {
    if (!active || next >= steps.size()) {
        return false;
    }
    lat = steps[next].lat;
    lon = steps[next].lon;
    return true;
}

double Navigator::distanceMeters(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLon / 2) * sin(dLon / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

String Navigator::httpGet(const String& url) {
    if (network) network(true);
    String body;
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    int code = http.GET();
    if (code == 200) {
        body = http.getString();
    } else {
        Serial.printf("🧭 Navigation request failed: %d (%s)\n", code, http.errorToString(code).c_str());
    }
    http.end();
    if (network) network(false);
    return body;
}

static String urlEncode(const String& s) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

String Navigator::lookupAddress(double lat, double lon) {
    String body = httpGet(apiBase + "/api/navigation?action=where&lat=" + String(lat, 6) + "&lon=" + String(lon, 6));
    if (body.isEmpty()) {
        return "";
    }
    JsonDocument doc;
    if (deserializeJson(doc, body) || !doc["ok"].as<bool>()) {
        return "";
    }
    return doc["text"].as<String>();
}

bool Navigator::fetchRoute(double lat, double lon, bool reroute) {
    String url = apiBase + "/api/navigation?action=route&lat=" + String(lat, 6) + "&lon=" + String(lon, 6) +
                 "&dest=" + urlEncode(reroute ? destinationName : pendingDestination);
    if (reroute) {
        url += "&dlat=" + String(destLat, 6) + "&dlon=" + String(destLon, 6);
    }
    String body = httpGet(url);
    if (body.isEmpty()) {
        speak("No pude calcular la ruta. Revisa la conexión e inténtalo de nuevo.");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        speak("No pude calcular la ruta.");
        return false;
    }
    if (!doc["ok"].as<bool>()) {
        speak(doc["summary"] | "No encontré ese lugar.");
        return false;
    }

    steps.clear();
    for (JsonObject s : doc["steps"].as<JsonArray>()) {
        NavStep step;
        step.lat = s["lat"].as<double>();
        step.lon = s["lon"].as<double>();
        step.text = s["text"].as<String>();
        String side = s["side"].as<String>();
        step.side = side == "left" ? 'L' : side == "right" ? 'R' : 'C';
        steps.push_back(step);
    }
    destinationName = doc["destination"]["name"].as<String>();
    destLat = doc["destination"]["lat"].as<double>();
    destLon = doc["destination"]["lon"].as<double>();

    // steps[0] is "start walking": its text is already part of the summary
    next = steps.size() > 1 ? 1 : 0;
    announced = false;
    minDistToNext = 1e9;
    active = !steps.empty();
    lastSpokeAt = millis();

    Serial.printf("🧭 Route to %s: %u steps\n", destinationName.c_str(), (unsigned)steps.size());
    speak(reroute ? "Te desviaste. Nueva ruta: " + steps[0].text : doc["summary"].as<String>());
    return active;
}

void Navigator::update(bool fixValid, double lat, double lon) {
    if (millis() - lastUpdateAt < UPDATE_EVERY_MS) {
        return;
    }
    lastUpdateAt = millis();

    // A new destination was requested
    if (pendingDestination.length() > 0) {
        if (!fixValid) {
            speak("Todavía no tengo señal de GPS. Sal a un lugar abierto y vuelve a pedirme la ruta.");
            pendingDestination = "";
            return;
        }
        Serial.printf("🧭 Requesting route to: %s\n", pendingDestination.c_str());
        fetchRoute(lat, lon, false);
        pendingDestination = "";
        return;
    }

    if (!active || !fixValid || next >= steps.size()) {
        return;
    }

    // Several maneuvers already within reach (bunched corners, or the user walked past one):
    // announce only the farthest, otherwise the speech queue falls behind the user
    while (!announced && next + 1 < steps.size() &&
           distanceMeters(lat, lon, steps[next + 1].lat, steps[next + 1].lon) <= ANNOUNCE_M) {
        Serial.printf("🧭 Skipping step %u (the next one is already close)\n", (unsigned)next);
        next++;
    }

    const NavStep& step = steps[next];
    double d = distanceMeters(lat, lon, step.lat, step.lon);
    bool isArrival = (next == steps.size() - 1);

    if (!announced && d <= ANNOUNCE_M) {
        Serial.printf("🧭 Step %u/%u (%.0f m): %s\n", (unsigned)next, (unsigned)steps.size() - 1, d, step.text.c_str());
        vibrate(step.side);
        speak(step.text);
        announced = true;
        lastSpokeAt = millis();
    }

    if (d <= REACHED_M) {
        if (isArrival) {
            Serial.println("🧭 Arrived");
            active = false;
            steps.clear();
            return;
        }
        next++;
        announced = false;
        minDistToNext = 1e9;
        return;
    }

    // Off route: clearly farther from the next maneuver than we've already been
    if (d < minDistToNext) {
        minDistToNext = d;
    } else if (d > minDistToNext + OFF_ROUTE_M && millis() - lastRerouteAt > REROUTE_COOLDOWN_MS) {
        Serial.printf("🧭 Off route (%.0f m from the next maneuver, was %.0f m) - rerouting\n", d, minDistToNext);
        lastRerouteAt = millis();
        fetchRoute(lat, lon, true);
        return;
    }

    // Long straight stretches: let the user know guidance is still running
    if (millis() - lastSpokeAt > REMINDER_MS) {
        int meters = max(10, (int)(round(d / 10.0) * 10));
        speak("Sigue " + String(meters) + " metros hasta la siguiente indicación.");
        lastSpokeAt = millis();
    }
}
