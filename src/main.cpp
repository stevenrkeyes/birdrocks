#include <Arduino.h>

#include "main_state_machine.h"
#include "pins.h"
#include "rgb_led.h"
#include "sound_player.h"

void setup() {
  delay(5000);
  Serial.begin(115200);
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
