#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>

class StatusLed {
private:
    uint8_t ledPin;
    unsigned long ultimoParpadeo;
    bool estadoLed;
    bool conectado;

public:
    StatusLed(uint8_t pin);
    void begin();
    void setConnected(bool state);
    void update();
};

#endif
