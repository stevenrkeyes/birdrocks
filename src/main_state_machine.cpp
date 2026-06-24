#include "main_state_machine.h"

#include <Arduino.h>

#include "rgb_led.h"
#include "sound_level_monitor.h"
#include "sound_player.h"

constexpr uint32_t kAwakeDurationMs = 2 * 60 * 1000;

namespace {

static void asleepEnter() {
  resumeSoundLevelMonitor();
  resetSoundLevelWindow();
  resetDimRedPulse();
}

static void asleepTick() {
  runDimRedPulse();
}

static void asleepExit() {
  suspendSoundLevelMonitor();
}

static void awakeEnter() {
  resetAwakeColorPulse();
  startGentleTonePlayback(kAwakeDurationMs);
}

static void awakeTick() {
  runAwakeColorPulse();
}

static void awakeExit() {
  stopPlayback();
  setRgbLedOff();
}

}  // namespace

State currentState = STATE_ASLEEP;
uint32_t stateEnteredAtMs = 0;

const StateDef states[NUM_STATES] = {
    {asleepEnter, asleepTick, asleepExit},
    {awakeEnter, awakeTick, awakeExit},
};

void mainStateMachineInit() {
  currentState = STATE_ASLEEP;
  stateEnteredAtMs = millis();
  states[currentState].enter();
}

void readInputs() {
  if (currentState == STATE_ASLEEP) {
    updateSoundLevelMonitor();
  }
}

State determineSystemState() {
  if (currentState == STATE_ASLEEP && isEnvironmentQuiet()) {
    return STATE_AWAKE;
  }

  if (currentState == STATE_AWAKE) {
    const uint32_t elapsedMs = millis() - stateEnteredAtMs;
    if (elapsedMs >= kAwakeDurationMs) {
      return STATE_ASLEEP;
    }
  }

  return currentState;
}
