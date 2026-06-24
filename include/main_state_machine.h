#pragma once

#include <stdint.h>

typedef enum {
  STATE_ASLEEP,
  STATE_AWAKE,

  NUM_STATES
} State;

typedef struct {
  void (*enter)(void);
  void (*tick)(void);
  void (*exit)(void);
} StateDef;

extern State currentState;
extern uint32_t stateEnteredAtMs;
extern const StateDef states[NUM_STATES];

void mainStateMachineInit();
void readInputs();
State determineSystemState();
