#include "sound_player.h"

#include <Arduino.h>
#include <I2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr int SAMPLE_RATE_HZ = 16000;
constexpr int TONE_AMPLITUDE = 5000;

constexpr float BASE_FREQUENCY_HZ = 330.0f;
constexpr float FREQUENCY_SWING_HZ = 90.0f;
constexpr float FREQUENCY_LFO_RATE = 0.35f;

constexpr UBaseType_t AUDIO_TASK_PRIORITY = 1;
constexpr uint32_t AUDIO_TASK_STACK = 4096;

TaskHandle_t audioTaskHandle = nullptr;
volatile bool playbackActive = false;

void flushI2sSilence() {
  I2S.flush();
  esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
}

void initI2sOutput() {
  I2S.end();
  I2S.setAllPins(I2S_BCLK, I2S_LRC, I2S_DIN, I2S_DIN, -1);

  if (!I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE_HZ, 16)) {
    Serial.println("I2S init failed");
  }
}

void audioTask(void* /*parameter*/) {
  float tonePhase = 0.0f;
  float lfoPhase = 0.0f;

  while (true) {
    if (!playbackActive) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      tonePhase = 0.0f;
      lfoPhase = 0.0f;
      continue;
    }

    const float frequencyHz =
        BASE_FREQUENCY_HZ + FREQUENCY_SWING_HZ * sinf(lfoPhase);
    const float phaseStep = (2.0f * PI * frequencyHz) / SAMPLE_RATE_HZ;
    const int16_t sample =
        static_cast<int16_t>(TONE_AMPLITUDE * sinf(tonePhase));

    I2S.write(sample);
    I2S.write(sample);

    tonePhase += phaseStep;
    if (tonePhase >= 2.0f * PI) {
      tonePhase -= 2.0f * PI;
    }

    lfoPhase += (2.0f * PI * FREQUENCY_LFO_RATE) / SAMPLE_RATE_HZ;
    if (lfoPhase >= 2.0f * PI) {
      lfoPhase -= 2.0f * PI;
    }
  }
}

}  // namespace

void initSoundPlayer() {
  xTaskCreate(audioTask, "audio", AUDIO_TASK_STACK, nullptr, AUDIO_TASK_PRIORITY,
              &audioTaskHandle);
}

void startGentleTonePlayback() {
  if (audioTaskHandle == nullptr) {
    return;
  }

  initI2sOutput();
  flushI2sSilence();
  playbackActive = true;
  xTaskNotifyGive(audioTaskHandle);
}

void stopPlayback() {
  playbackActive = false;
  flushI2sSilence();
  I2S.end();
}
