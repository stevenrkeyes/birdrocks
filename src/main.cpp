#include <Arduino.h>
#include <esp_rom_sys.h>

#include "main_state_machine.h"
#include "pins.h"
#include "rgb_led.h"
#include "sound_player.h"

namespace {

void earlyBootMarker() __attribute__((constructor(101)));

void earlyBootMarker() {
  esp_rom_printf("\n[rocks_mic] starting...\n");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStartMs = millis();
  while (!Serial && (millis() - serialWaitStartMs) < 3000) {
    delay(10);
  }

  Serial.println("rocks_mic booting...");
  randomSeed(micros());

  initRgbLed();
  initSoundPlayer();

  Serial.println("rocks_mic ready");

  // Blink the yellow onboard LED to show the board is working, then turn it off.
  // Note inverted logic.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);

  mainStateMachineInit();
}

void loop() {
  while (true) {
    readInputs();
    const State nextState = determineSystemState();

    if (nextState != currentState) {
      states[currentState].exit();
      currentState = nextState;
      stateEnteredAtMs = millis();
      states[currentState].enter();
      Serial.printf("State -> %d\n", static_cast<int>(currentState));
    }

    states[currentState].tick();
    delay(1);
  }
}
