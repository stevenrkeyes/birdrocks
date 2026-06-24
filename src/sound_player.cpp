#include "sound_player.h"

#include <Arduino.h>
#include <I2S.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr int SAMPLE_RATE_HZ = 16000;
constexpr int TONE_AMPLITUDE = 8000;

constexpr float MIN_NOTE_HZ = 440.0f;
constexpr float MAX_NOTE_HZ = 1760.0f;
constexpr float ROOT_NOTE_HZ = 440.0f;

constexpr uint32_t HOLD_SAMPLES = SAMPLE_RATE_HZ * 2;
constexpr uint32_t GLIDE_SAMPLES = SAMPLE_RATE_HZ / 2;
constexpr uint32_t FADE_SAMPLES = SAMPLE_RATE_HZ;

constexpr int kPentatonicSemitones[] = {0, 2, 4, 7, 9};

constexpr UBaseType_t AUDIO_TASK_PRIORITY = 1;
constexpr uint32_t AUDIO_TASK_STACK = 4096;

TaskHandle_t audioTaskHandle = nullptr;
volatile bool playbackActive = false;
volatile uint32_t playbackDurationSamples = 0;

float pentatonicNotes[16] = {};
size_t pentatonicNoteCount = 0;

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

void buildPentatonicNotes() {
  pentatonicNoteCount = 0;

  for (int octave = 0; octave < 4; octave++) {
    for (int semitone : kPentatonicSemitones) {
      const float freq = ROOT_NOTE_HZ * powf(2.0f, (octave * 12 + semitone) / 12.0f);
      if (freq >= MIN_NOTE_HZ && freq <= MAX_NOTE_HZ &&
          pentatonicNoteCount < (sizeof(pentatonicNotes) / sizeof(pentatonicNotes[0]))) {
        pentatonicNotes[pentatonicNoteCount++] = freq;
      }
    }
  }
}

float pickRandomNote(float avoidHz) {
  if (pentatonicNoteCount == 0) {
    return ROOT_NOTE_HZ;
  }
  if (pentatonicNoteCount == 1) {
    return pentatonicNotes[0];
  }

  float note = pentatonicNotes[0];
  do {
    note = pentatonicNotes[random(0, pentatonicNoteCount)];
  } while (fabsf(note - avoidHz) < 0.1f);

  return note;
}

float computeEnvelope(uint32_t sampleIndex, uint32_t durationSamples) {
  if (durationSamples == 0) {
    return 0.0f;
  }

  if (sampleIndex < FADE_SAMPLES) {
    return static_cast<float>(sampleIndex) / static_cast<float>(FADE_SAMPLES);
  }

  if (durationSamples <= FADE_SAMPLES) {
    return 0.0f;
  }

  const uint32_t fadeOutStart = durationSamples - FADE_SAMPLES;
  if (sampleIndex >= fadeOutStart) {
    if (sampleIndex >= durationSamples) {
      return 0.0f;
    }
    return static_cast<float>(durationSamples - sampleIndex) / static_cast<float>(FADE_SAMPLES);
  }

  return 1.0f;
}

void audioTask(void* /*parameter*/) {
  float tonePhase = 0.0f;
  float currentFreq = ROOT_NOTE_HZ;
  float glideStartFreq = ROOT_NOTE_HZ;
  float glideTargetFreq = ROOT_NOTE_HZ;
  uint32_t holdSamplesRemaining = 0;
  uint32_t glideSamplesRemaining = 0;
  bool gliding = false;
  uint32_t playbackSampleIndex = 0;

  while (true) {
    if (!playbackActive) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      currentFreq = pickRandomNote(-1.0f);
      glideStartFreq = currentFreq;
      glideTargetFreq = currentFreq;
      gliding = false;
      holdSamplesRemaining = HOLD_SAMPLES;
      glideSamplesRemaining = 0;
      playbackSampleIndex = 0;
      tonePhase = 0.0f;
      continue;
    }

    const uint32_t durationSamples = playbackDurationSamples;
    const float envelope =
        computeEnvelope(playbackSampleIndex, durationSamples);
    const float phaseStep = (2.0f * PI * currentFreq) / SAMPLE_RATE_HZ;
    const int16_t sample = static_cast<int16_t>(TONE_AMPLITUDE * envelope * sinf(tonePhase));

    I2S.write(sample);
    I2S.write(sample);

    tonePhase += phaseStep;
    if (tonePhase >= 2.0f * PI) {
      tonePhase -= 2.0f * PI;
    }

    playbackSampleIndex++;

    if (gliding) {
      const float progress =
          1.0f - (static_cast<float>(glideSamplesRemaining) / static_cast<float>(GLIDE_SAMPLES));
      currentFreq = glideStartFreq * powf(glideTargetFreq / glideStartFreq, progress);

      if (glideSamplesRemaining > 0) {
        glideSamplesRemaining--;
      }

      if (glideSamplesRemaining == 0) {
        currentFreq = glideTargetFreq;
        gliding = false;
        holdSamplesRemaining = HOLD_SAMPLES;
      }
    } else if (holdSamplesRemaining > 0) {
      holdSamplesRemaining--;

      if (holdSamplesRemaining == 0) {
        glideTargetFreq = pickRandomNote(currentFreq);
        glideStartFreq = currentFreq;
        gliding = true;
        glideSamplesRemaining = GLIDE_SAMPLES;
      }
    }
  }
}

}  // namespace

void initSoundPlayer() {
  buildPentatonicNotes();
  xTaskCreate(audioTask, "audio", AUDIO_TASK_STACK, nullptr, AUDIO_TASK_PRIORITY,
              &audioTaskHandle);
}

void startGentleTonePlayback(uint32_t durationMs) {
  if (audioTaskHandle == nullptr) {
    return;
  }

  playbackDurationSamples = (static_cast<uint64_t>(durationMs) * SAMPLE_RATE_HZ) / 1000;
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
