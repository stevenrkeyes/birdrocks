#include "sound_level_monitor.h"

#include <Arduino.h>
#include <I2S.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr int SAMPLE_RATE_HZ = 16000;
constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
constexpr uint32_t WINDOW_DURATION_MS = 20000;
constexpr size_t WINDOW_SAMPLE_COUNT = WINDOW_DURATION_MS / SAMPLE_INTERVAL_MS;

constexpr int READ_CHUNK_SAMPLES = 512;
constexpr float QUIET_RMS_THRESHOLD = 1250.0f;

float levelWindow[WINDOW_SAMPLE_COUNT] = {};
size_t windowWriteIndex = 0;
size_t windowSampleCount = 0;
uint32_t lastSampleMs = 0;
uint32_t lastPrintMs = 0;
float lastRms = 0.0f;
bool monitorActive = false;

bool initPdmInput() {
  I2S.end();
  I2S.setAllPins(-1, PDM_CLK_PIN, PDM_DATA_PIN, -1, -1);
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE_HZ, 16)) {
    Serial.println("PDM microphone init failed");
    return false;
  }
  return true;
}

float computeRms(const int16_t* samples, size_t count) {
  if (count == 0) {
    return 0.0f;
  }

  double sumSquares = 0.0;
  for (size_t i = 0; i < count; i++) {
    const float sample = static_cast<float>(samples[i]);
    sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
  }

  return sqrtf(static_cast<float>(sumSquares / static_cast<double>(count)));
}

void pushLevelSample(float rms) {
  lastRms = rms;
  levelWindow[windowWriteIndex] = rms;
  windowWriteIndex = (windowWriteIndex + 1) % WINDOW_SAMPLE_COUNT;
  if (windowSampleCount < WINDOW_SAMPLE_COUNT) {
    windowSampleCount++;
  }
}

float averageWindowLevel() {
  if (windowSampleCount == 0) {
    return 0.0f;
  }

  float sum = 0.0f;
  for (size_t i = 0; i < windowSampleCount; i++) {
    sum += levelWindow[i];
  }
  return sum / static_cast<float>(windowSampleCount);
}

}  // namespace

void initSoundLevelMonitor() {
  windowWriteIndex = 0;
  windowSampleCount = 0;
  lastSampleMs = 0;
  lastPrintMs = 0;
  lastRms = 0.0f;
  monitorActive = initPdmInput();
}

void updateSoundLevelMonitor() {
  if (!monitorActive) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastPrintMs >= 1000) {
    lastPrintMs = now;
    Serial.printf("Sound level: %.1f\n", lastRms);
  }

  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleMs = now;

  int16_t samples[READ_CHUNK_SAMPLES] = {};
  size_t bytesRead = 0;
  const esp_err_t readResult = esp_i2s::i2s_read(
      esp_i2s::I2S_NUM_0, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(50));

  if (readResult != ESP_OK || bytesRead == 0) {
    return;
  }

  const size_t sampleCount = bytesRead / sizeof(int16_t);
  pushLevelSample(computeRms(samples, sampleCount));
}

void suspendSoundLevelMonitor() {
  if (!monitorActive) {
    return;
  }

  I2S.end();
  monitorActive = false;
}

void resumeSoundLevelMonitor() {
  if (monitorActive) {
    return;
  }

  monitorActive = initPdmInput();
}

void resetSoundLevelWindow() {
  windowWriteIndex = 0;
  windowSampleCount = 0;
  lastSampleMs = 0;
  lastPrintMs = 0;
  lastRms = 0.0f;
}

bool soundLevelWindowReady() {
  return windowSampleCount >= WINDOW_SAMPLE_COUNT;
}

bool isEnvironmentQuiet() {
  if (!soundLevelWindowReady()) {
    return false;
  }

  return averageWindowLevel() < QUIET_RMS_THRESHOLD;
}
