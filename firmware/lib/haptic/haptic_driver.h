#ifndef HAPTIC_DRIVER_H
#define HAPTIC_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

// Direccion I2C por defecto del expansor PCF8574 (A0-A2 a GND)
#define PCF8574_ADDR 0x20

// Posicion de cada motor en el expansor (salidas activas en LOW)
enum HapticMotor {
    MOTOR_LEFT   = 0,
    MOTOR_CENTER = 1,
    MOTOR_RIGHT  = 2
};

// Direcciones que puede reportar Gemini o el calculo de rumbo GPS
enum class HazardDirection {
    LEFT,
    CENTER,
    RIGHT,
    UNKNOWN
};

/**
 * Controla los 3 motores de vibracion (izquierda/centro/derecha) a traves
 * de un expansor de E/S PCF8574 en el bus I2C. Cada salida del PCF8574
 * maneja un transistor NPN que a su vez enciende un motor de moneda (ERM).
 *
 * Fuentes de activacion (ver firmware/haptic/INTEGRACION.md):
 *  - El intent 'obstacle_alert' que Gemini genera al analizar la camara.
 *  - La red de peligros compartidos (api/hazards.js) via calculo de rumbo GPS.
 */
class HapticDriver {
public:
    // Inicializa el bus I2C y apaga los 3 motores. Devuelve false si no
    // encuentra el PCF8574 en el bus (revisar wiring / direccion I2C).
    bool initialize(uint8_t sda = 21, uint8_t scl = 22, uint8_t i2cAddr = PCF8574_ADDR);

    // Enciende un motor especifico durante durationMs milisegundos (no bloqueante).
    void pulse(HapticMotor motor, uint16_t durationMs = 300);

    // Traduce una direccion de texto ("left" | "center" | "right") a motor
    // y dispara el pulso. Pensado para conectarse directo al valor que
    // devuelve Gemini en el systemAction, o al resultado del bearing GPS.
    void triggerDirection(HazardDirection direction, uint16_t durationMs = 300);

    // Debe llamarse en cada loop() del firmware principal: apaga los
    // motores cuyo tiempo de pulso ya termino.
    void update();

    // Apaga los 3 motores de inmediato.
    void allOff();

private:
    uint8_t i2cAddr = PCF8574_ADDR;
    uint8_t outputState = 0xFF;              // activo en LOW, todo apagado = todos en 1
    unsigned long motorOffAt[3] = {0, 0, 0};  // timestamp en el que cada motor debe apagarse
    bool initialized = false;

    void writeState();
    void setMotor(HapticMotor motor, bool on);
};

#endif
