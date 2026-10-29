#pragma once

// SensESP's status LED, drawn on the screen instead of driving a GPIO.
//
// Without this, SensESP picks a status LED from the Arduino variant: the
// ESP32-S3 variants define PIN_RGB_LED 48, and GPIO48 is an RGB data line of
// the 2.1" panel. Passing this object to set_system_status_led() keeps
// SensESP off the pins and shows the same blink patterns (WiFi / Signal K
// connecting / connected) as a dot on the bezel.

#include "gauge_ui.h"
#include "sensesp/system/system_status_led.h"

namespace skgauges {

class ScreenStatusLed : public sensesp::SystemStatusLed {
 public:
  // SystemStatusLed's constructor attaches LEDC to the pin it is given; an
  // out-of-range pin makes that attach fail harmlessly (one log line), and
  // show() below never touches it.
  ScreenStatusLed() : sensesp::SystemStatusLed(kNoPin) {}

 protected:
  void show() override {
    const auto& led = leds_[0];
    ui_set_status_color(led.r, led.g, led.b);
  }

 private:
  static constexpr uint8_t kNoPin = 0xFF;
};

}  // namespace skgauges
