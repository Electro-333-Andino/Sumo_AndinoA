#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>

class StatusLed {
  private:
    uint8_t pin;
    bool isConnected;
    unsigned long previousMillis;
    const long interval = 500; // Medio segundo de parpadeo
    bool ledState;

  public:
    StatusLed(uint8_t pin);
    void begin();
    void setConnected(bool connected);
    void update();
};

#endif
