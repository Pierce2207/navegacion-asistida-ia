#ifndef NAVIGATION_H
#define NAVIGATION_H

#include <Arduino.h>
#include <vector>

// One maneuver of a walking route, as returned by the backend (api/navigation.js)
struct NavStep {
    double lat;
    double lon;
    String text;  // Spanish instruction, ready to speak
    char side;    // 'L', 'R' or 'C': which motor vibrates
};

// Walking navigation with turn-by-turn guidance. The route comes from our backend (OpenStreetMap),
// already in Spanish; this class follows the GPS along it and announces each maneuver by voice
// and with the motor on that side. All of it runs from the main loop (update()).
class Navigator {
public:
    typedef void (*SpeakFn)(const String& text);
    typedef void (*VibrateFn)(char side);
    typedef void (*NetworkFn)(bool start);  // brackets each HTTPS request (pauses other TLS users)

    void begin(const char* apiBase, SpeakFn speak, VibrateFn vibrate, NetworkFn network);

    // Gemini asked for a route. Only recorded here: the request runs from update(), so the tool
    // response to Gemini isn't held up by the HTTPS call.
    void requestRoute(const String& destination);
    void stop(bool announce);
    bool isActive() const { return active; }

    // Every main loop pass, with the current GPS fix
    void update(bool fixValid, double lat, double lon);

    // "Estás en Av. ..." for the current position, or "" if it couldn't be looked up
    String lookupAddress(double lat, double lon);

    // For the fake-GPS desk test: where the next maneuver is
    bool nextStepLocation(double& lat, double& lon) const;

    static double distanceMeters(double lat1, double lon1, double lat2, double lon2);

private:
    bool fetchRoute(double lat, double lon, bool reroute);
    String httpGet(const String& url);

    String apiBase;
    SpeakFn speak = nullptr;
    VibrateFn vibrate = nullptr;
    NetworkFn network = nullptr;

    String pendingDestination;     // set by requestRoute(), consumed by update()
    String destinationName;
    double destLat = 0, destLon = 0;

    std::vector<NavStep> steps;
    size_t next = 0;               // index of the next maneuver to reach
    bool announced = false;        // steps[next] already spoken
    bool active = false;
    double minDistToNext = 1e9;    // closest we've been to steps[next] (detects going off-route)
    unsigned long lastSpokeAt = 0;
    unsigned long lastRerouteAt = 0;
    unsigned long lastUpdateAt = 0;
};

#endif
