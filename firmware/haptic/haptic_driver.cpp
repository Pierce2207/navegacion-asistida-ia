#include "haptic_driver.h"

bool HapticDriver::initialize(uint8_t sda, uint8_t scl, uint8_t i2cAddr_) {
    i2cAddr = i2cAddr_;
    Wire.begin(sda, scl);

    Wire.beginTransmission(i2cAddr);
    uint8_t error = Wire.endTransmission();
    if (error != 0) {
        Serial.printf("[Haptic] No se encontro el PCF8574 en 0x%02X (error %d)\n", i2cAddr, error);
        return false;
    }

    outputState = 0xFF; // todas las salidas en alto = motores apagados (logica activa en LOW)
    writeState();
    initialized = true;
    Serial.println("[Haptic] PCF8574 inicializado, 3 motores listos.");
    return true;
}

void HapticDriver::setMotor(HapticMotor motor, bool on) {
    if (!initialized) return;
    uint8_t bit = 1 << motor;
    if (on) {
        outputState &= ~bit; // LOW = motor encendido
    } else {
        outputState |= bit;  // HIGH = motor apagado
    }
    writeState();
}

void HapticDriver::writeState() {
    Wire.beginTransmission(i2cAddr);
    Wire.write(outputState);
    Wire.endTransmission();
}

void HapticDriver::pulse(HapticMotor motor, uint16_t durationMs) {
    if (!initialized) return;
    setMotor(motor, true);
    motorOffAt[motor] = millis() + durationMs;
}

void HapticDriver::triggerDirection(HazardDirection direction, uint16_t durationMs) {
    switch (direction) {
        case HazardDirection::LEFT:   pulse(MOTOR_LEFT, durationMs); break;
        case HazardDirection::CENTER: pulse(MOTOR_CENTER, durationMs); break;
        case HazardDirection::RIGHT:  pulse(MOTOR_RIGHT, durationMs); break;
        default: break; // UNKNOWN: no vibra nada, evita falsos avisos
    }
}

void HapticDriver::update() {
    if (!initialized) return;
    unsigned long now = millis();
    for (int m = 0; m < 3; m++) {
        if (motorOffAt[m] != 0 && now >= motorOffAt[m]) {
            setMotor((HapticMotor)m, false);
            motorOffAt[m] = 0;
        }
    }
}

void HapticDriver::allOff() {
    outputState = 0xFF;
    writeState();
    motorOffAt[0] = motorOffAt[1] = motorOffAt[2] = 0;
}
