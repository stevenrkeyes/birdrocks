#include "sound_player.h"

#include <Arduino.h>
#include <FS.h>
#include <I2S.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pins.h"

namespace {

constexpr int SAMPLE_RATE_HZ = 16000;
constexpr uint32_t FADE_SAMPLES = SAMPLE_RATE_HZ;
constexpr size_t MAX_WAV_FILES = 16;
constexpr size_t MAX_FILENAME_LEN = 64;
constexpr size_t READ_BUFFER_SAMPLES = 256;

constexpr UBaseType_t AUDIO_TASK_PRIORITY = 1;
constexpr uint32_t AUDIO_TASK_STACK = 8192;
constexpr uint32_t kMaxPreFileDelayMs = 15000;

TaskHandle_t audioTaskHandle = nullptr;
volatile bool playbackActive = false;
volatile uint32_t playbackDurationSamples = 0;
bool i2sOutputActive = false;

char wavFilenames[MAX_WAV_FILES][MAX_FILENAME_LEN] = {};
size_t wavFileCount = 0;
size_t playbackOrder[MAX_WAV_FILES] = {};

struct WavMeta {
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t sampleCount = 0;
  uint32_t sampleIndex = 0;
  bool open = false;
};

WavMeta currentWav;
File* currentWavFile = nullptr;

void flushI2sSilence() {
  if (!i2sOutputActive) {
    return;
  }

  I2S.flush();
  esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
}

void initI2sOutput() {
  I2S.end();
  I2S.setAllPins(I2S_BCLK, I2S_LRC, I2S_DIN, I2S_DIN, -1);

  i2sOutputActive = I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE_HZ, 16);
  if (!i2sOutputActive) {
    Serial.println("I2S init failed");
  }
}

void closeCurrentWav() {
  if (currentWavFile != nullptr) {
    currentWavFile->close();
    delete currentWavFile;
    currentWavFile = nullptr;
  }
  currentWav.open = false;
}

bool parseWavHeader(File& file, uint32_t& dataOffset, uint32_t& dataSize, uint32_t& sampleRate,
                    uint16_t& channels, uint16_t& bitsPerSample) {
  char riffHeader[4] = {};
  if (file.read(reinterpret_cast<uint8_t*>(riffHeader), 4) != 4 ||
      memcmp(riffHeader, "RIFF", 4) != 0) {
    return false;
  }

  file.seek(8);
  char waveHeader[4] = {};
  if (file.read(reinterpret_cast<uint8_t*>(waveHeader), 4) != 4 ||
      memcmp(waveHeader, "WAVE", 4) != 0) {
    return false;
  }

  bool gotFmt = false;
  bool gotData = false;
  dataOffset = 0;
  dataSize = 0;
  sampleRate = 0;
  channels = 0;
  bitsPerSample = 0;

  while (file.available()) {
    char chunkId[4] = {};
    if (file.read(reinterpret_cast<uint8_t*>(chunkId), 4) != 4) {
      break;
    }

    uint32_t chunkSize = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&chunkSize), 4) != 4) {
      break;
    }

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      uint16_t audioFormat = 0;
      uint32_t byteRate = 0;
      uint16_t blockAlign = 0;
      file.read(reinterpret_cast<uint8_t*>(&audioFormat), 2);
      file.read(reinterpret_cast<uint8_t*>(&channels), 2);
      file.read(reinterpret_cast<uint8_t*>(&sampleRate), 4);
      file.read(reinterpret_cast<uint8_t*>(&byteRate), 4);
      file.read(reinterpret_cast<uint8_t*>(&blockAlign), 2);
      file.read(reinterpret_cast<uint8_t*>(&bitsPerSample), 2);
      if (chunkSize > 16) {
        file.seek(file.position() + chunkSize - 16);
      }
      gotFmt = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      dataOffset = file.position();
      dataSize = chunkSize;
      gotData = true;
      break;
    } else {
      file.seek(file.position() + chunkSize);
    }
  }

  return gotFmt && gotData;
}

bool openWavByCatalogIndex(size_t catalogIndex) {
  closeCurrentWav();

  if (catalogIndex >= wavFileCount) {
    return false;
  }

  char path[MAX_FILENAME_LEN + 2] = {};
  snprintf(path, sizeof(path), "/%s", wavFilenames[catalogIndex]);

  File* file = new File(LittleFS.open(path, "r"));
  if (file == nullptr || !*file) {
    delete file;
    return false;
  }

  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  if (!parseWavHeader(*file, dataOffset, dataSize, sampleRate, channels, bitsPerSample)) {
    delete file;
    return false;
  }

  if (sampleRate != static_cast<uint32_t>(SAMPLE_RATE_HZ) || channels != 1 ||
      bitsPerSample != 16) {
    delete file;
    return false;
  }

  file->seek(dataOffset);
  currentWavFile = file;
  currentWav.dataOffset = dataOffset;
  currentWav.dataSize = dataSize;
  currentWav.sampleCount = dataSize / sizeof(int16_t);
  currentWav.sampleIndex = 0;
  currentWav.open = true;
  return true;
}

void shufflePlaybackOrder() {
  for (size_t i = 0; i < wavFileCount; i++) {
    playbackOrder[i] = i;
  }

  for (size_t i = wavFileCount; i > 1; i--) {
    const size_t j = random(0, i);
    const size_t tmp = playbackOrder[i - 1];
    playbackOrder[i - 1] = playbackOrder[j];
    playbackOrder[j] = tmp;
  }
}

float computeFadeEnvelope(uint32_t sampleIndex, uint32_t durationSamples) {
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

bool openNextWavInPlaylist(size_t& playlistIndex) {
  if (wavFileCount == 0) {
    return false;
  }

  if (playlistIndex >= wavFileCount) {
    shufflePlaybackOrder();
    playlistIndex = 0;
  }

  const size_t catalogIndex = playbackOrder[playlistIndex];
  playlistIndex++;
  return openWavByCatalogIndex(catalogIndex);
}

void waitRandomPreFileDelay() {
  const uint32_t delayMs = random(0, kMaxPreFileDelayMs + 1);
  const uint32_t delayStartMs = millis();
  while (playbackActive && (millis() - delayStartMs) < delayMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool beginNextWavWithDelay(size_t& playlistIndex) {
  if (!openNextWavInPlaylist(playlistIndex)) {
    return false;
  }

  waitRandomPreFileDelay();
  return playbackActive;
}

void scanWavFiles() {
  wavFileCount = 0;

  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("LittleFS root open failed");
    return;
  }

  File entry = root.openNextFile();
  while (entry && wavFileCount < MAX_WAV_FILES) {
    const char* name = entry.name();
    if (name == nullptr) {
      entry.close();
      entry = root.openNextFile();
      continue;
    }

    const size_t nameLen = strlen(name);
    if (nameLen >= 4 && strcasecmp(name + nameLen - 4, ".wav") == 0) {
      strncpy(wavFilenames[wavFileCount], name, MAX_FILENAME_LEN - 1);
      wavFilenames[wavFileCount][MAX_FILENAME_LEN - 1] = '\0';
      wavFileCount++;
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  Serial.printf("Found %u WAV file(s)\n", static_cast<unsigned>(wavFileCount));
}

void audioTask(void* /*parameter*/) {
  size_t playlistIndex = 0;
  uint32_t sessionSampleIndex = 0;
  int16_t readBuffer[READ_BUFFER_SAMPLES] = {};
  size_t bufferSampleIndex = 0;
  size_t bufferSamplesAvailable = 0;

  while (true) {
    if (!playbackActive) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      closeCurrentWav();
      playlistIndex = 0;
      sessionSampleIndex = 0;
      bufferSampleIndex = 0;
      bufferSamplesAvailable = 0;
      shufflePlaybackOrder();

      if (!beginNextWavWithDelay(playlistIndex)) {
        playbackActive = false;
        continue;
      }
    }

    const uint32_t durationSamples = playbackDurationSamples;
    if (sessionSampleIndex >= durationSamples) {
      playbackActive = false;
      closeCurrentWav();
      continue;
    }

    if (bufferSampleIndex >= bufferSamplesAvailable) {
      if (!currentWav.open || currentWavFile == nullptr) {
        if (!beginNextWavWithDelay(playlistIndex)) {
          playbackActive = false;
          continue;
        }
        bufferSampleIndex = 0;
        bufferSamplesAvailable = 0;
      }

      const uint32_t bytesConsumed = currentWavFile->position() - currentWav.dataOffset;
      const uint32_t bytesRemaining = currentWav.dataSize - bytesConsumed;
      const size_t bytesToRead =
          min(static_cast<size_t>(bytesRemaining), READ_BUFFER_SAMPLES * sizeof(int16_t));
      if (bytesToRead == 0) {
        closeCurrentWav();
        if (!beginNextWavWithDelay(playlistIndex)) {
          playbackActive = false;
          continue;
        }
        bufferSampleIndex = 0;
        bufferSamplesAvailable = 0;
        continue;
      }

      const size_t bytesRead =
          currentWavFile->read(reinterpret_cast<uint8_t*>(readBuffer), bytesToRead);
      if (bytesRead == 0) {
        closeCurrentWav();
        if (!beginNextWavWithDelay(playlistIndex)) {
          playbackActive = false;
          continue;
        }
        bufferSampleIndex = 0;
        bufferSamplesAvailable = 0;
        continue;
      }

      bufferSampleIndex = 0;
      bufferSamplesAvailable = bytesRead / sizeof(int16_t);
    }

    const int16_t rawSample = readBuffer[bufferSampleIndex++];
    const float sessionEnvelope =
        computeFadeEnvelope(sessionSampleIndex, durationSamples);
    const float fileEnvelope =
        computeFadeEnvelope(currentWav.sampleIndex, currentWav.sampleCount);
    const float envelope = sessionEnvelope * fileEnvelope;
    const int16_t sample = static_cast<int16_t>(rawSample * envelope);

    I2S.write(sample);
    I2S.write(sample);
    sessionSampleIndex++;
    currentWav.sampleIndex++;

    if ((sessionSampleIndex & 0x3FF) == 0) {
      taskYIELD();
    }
  }
}

}  // namespace

void initSoundPlayer() {
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed - run: pio run -t uploadfs");
    return;
  }

  scanWavFiles();
  xTaskCreatePinnedToCore(audioTask, "audio", AUDIO_TASK_STACK, nullptr, AUDIO_TASK_PRIORITY,
                          &audioTaskHandle, 1);
}

void startGentleTonePlayback(uint32_t durationMs) {
  if (audioTaskHandle == nullptr || wavFileCount == 0) {
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

  if (i2sOutputActive) {
    flushI2sSilence();
    I2S.end();
    i2sOutputActive = false;
  }
}
