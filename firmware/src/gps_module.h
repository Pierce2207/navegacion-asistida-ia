#ifndef GPS_MODULE_H
#define GPS_MODULE_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

struct GPSData {
    bool isValid;
    float latitude;
    float longitude;
    float altitude;
    float speed;
    int satellites;
    float hdop;
    unsigned long timestamp;
    String dateTime;
};

class GPSModule {
private:
    // GPS pins - AI-Thinker ESP32-CAM: solo quedan libres GPIO 1/3 (UART0).
    // GPIO3 (U0R) recibe el NMEA del GPS; GPIO1 (U0T) sigue libre para el monitor serie.
    // No hace falta enviar nada al GPS, asi que TX queda sin usar (-1).
    // IMPORTANTE: desconectar el TX del GPS mientras se flashea la placa.
    static const int GPS_RX_PIN = 3;   // Connect to GPS TX
    static const int GPS_TX_PIN = -1;  // GPS RX sin conectar
    static const int GPS_BAUD_RATE = 9600;
    
    TinyGPSPlus gps;
    HardwareSerial gpsSerial;
    GPSData currentData;
    unsigned long lastValidFix;
    static const unsigned long GPS_TIMEOUT = 30000; // 30 seconds
    
public:
    GPSModule();
    ~GPSModule();
    
    // Initialization
    bool initialize();
    
    // Main update function, called regularly in loop
    void update();
    
    // Data access
    GPSData getGPSData() const;
    bool hasValidFix() const;
    bool isRecentFix(unsigned long maxAge = 10000) const; // Default 10 seconds
    
    // Formatted strings for easy display/transmission
    String getLocationString() const;
    String getLocationJSON() const;
    
    // Status information
    void printStatus() const;
    void printRawData(int seconds = 10);
    bool testBaudRates();
    bool isConnected() const;
};

#endif
