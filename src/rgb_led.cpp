#include "rgb_led.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace {

constexpr uint8_t PWM_RES_BITS = 8;
constexpr uint32_t PWM_MAX_DUTY = (1u << PWM_RES_BITS) - 1;
constexpr uint32_t DUTY_CAP = (PWM_MAX_DUTY * 50) / 100;  // 50% max brightness
constexpr uint32_t AWAKE_DUTY_CAP = DUTY_CAP;

constexpr uint8_t RGB_RED_CHANNEL = 0;
constexpr uint8_t RGB_GREEN_CHANNEL = 1;
constexpr uint8_t RGB_BLUE_CHANNEL = 2;

constexpr uint32_t PWM_FREQ_HZ = 100000;

float dimRedPhase = 0.0f;
float awakeColorHue1 = 0.0f;
float awakeColorHue2 = 120.0f;
float awakeColorPhase = 0.0f;

void hsvToRgb(float hue, float saturation, float value, uint8_t &red, uint8_t &green,
              uint8_t &blue) {
  const float sector = hue / 60.0f;
  const int sectorIndex = static_cast<int>(sector) % 6;
  const float fraction = sector - static_cast<int>(sector);

  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));

  switch (sectorIndex) {
    case 0:
      red = static_cast<uint8_t>(value * 255.0f);
      green = static_cast<uint8_t>(t * 255.0f);
      blue = static_cast<uint8_t>(p * 255.0f);
      break;
    case 1:
      red = static_cast<uint8_t>(q * 255.0f);
      green = static_cast<uint8_t>(value * 255.0f);
      blue = static_cast<uint8_t>(p * 255.0f);
      break;
    case 2:
      red = static_cast<uint8_t>(p * 255.0f);
      green = static_cast<uint8_t>(value * 255.0f);
      blue = static_cast<uint8_t>(t * 255.0f);
      break;
    case 3:
      red = static_cast<uint8_t>(p * 255.0f);
      green = static_cast<uint8_t>(q * 255.0f);
      blue = static_cast<uint8_t>(value * 255.0f);
      break;
    case 4:
      red = static_cast<uint8_t>(t * 255.0f);
      green = static_cast<uint8_t>(p * 255.0f);
      blue = static_cast<uint8_t>(value * 255.0f);
      break;
    default:
      red = static_cast<uint8_t>(value * 255.0f);
      green = static_cast<uint8_t>(p * 255.0f);
      blue = static_cast<uint8_t>(q * 255.0f);
      break;
  }
}

void writeChannel(uint8_t channel, uint8_t colorValue, uint32_t dutyCap) {
  const uint32_t duty = (static_cast<uint32_t>(colorValue) * dutyCap) / 255;
  ledcWrite(channel, duty);
}

void writeRgb(uint8_t red, uint8_t green, uint8_t blue, uint32_t dutyCap = DUTY_CAP) {
  writeChannel(RGB_RED_CHANNEL, red, dutyCap);
  writeChannel(RGB_GREEN_CHANNEL, green, dutyCap);
  writeChannel(RGB_BLUE_CHANNEL, blue, dutyCap);
}

uint32_t setupPwmChannel(uint8_t channel, uint8_t pin) {
  const uint32_t actualFreq = ledcSetup(channel, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(pin, channel);
  ledcWrite(channel, 0);
  return actualFreq;
}

float randomHue() {
  return static_cast<float>(random(0, 3600)) / 10.0f;
}

}  // namespace

void initRgbLed() {
  const uint32_t redFreq = setupPwmChannel(RGB_RED_CHANNEL, RGB_RED_PIN);
  setupPwmChannel(RGB_GREEN_CHANNEL, RGB_GREEN_PIN);
  setupPwmChannel(RGB_BLUE_CHANNEL, RGB_BLUE_PIN);

  Serial.printf("RGB PWM: requested %lu Hz, actual %lu Hz, 8-bit, 50%% duty cap\n",
                static_cast<unsigned long>(PWM_FREQ_HZ),
                static_cast<unsigned long>(redFreq));
}

void setRgbLedOff() {
  writeRgb(0, 0, 0);
}

void resetDimRedPulse() {
  dimRedPhase = 0.0f;
}

void runDimRedPulse() {
  static uint32_t lastUpdateMs = 0;

  const uint32_t now = millis();
  if (lastUpdateMs == 0) {
    lastUpdateMs = now;
  }
  const uint32_t elapsedMs = now - lastUpdateMs;
  lastUpdateMs = now;

  dimRedPhase += elapsedMs * 0.003f;
  const float brightness = ((sinf(dimRedPhase) + 1.0f) * 0.5f) * 0.22f;
  const uint8_t red = static_cast<uint8_t>(brightness * 255.0f);
  writeRgb(red, 0, 0);
}

void resetAwakeColorPulse() {
  awakeColorHue1 = randomHue();
  do {
    awakeColorHue2 = randomHue();
  } while (fabsf(awakeColorHue2 - awakeColorHue1) < 30.0f);
  awakeColorPhase = 0.0f;
}

void runAwakeColorPulse() {
  static uint32_t lastUpdateMs = 0;

  const uint32_t now = millis();
  if (lastUpdateMs == 0) {
    lastUpdateMs = now;
  }
  const uint32_t elapsedMs = now - lastUpdateMs;
  lastUpdateMs = now;

  awakeColorPhase += elapsedMs * 0.004f;
  const float blend = (sinf(awakeColorPhase) + 1.0f) * 0.5f;
  const float hue = awakeColorHue1 + (awakeColorHue2 - awakeColorHue1) * blend;

  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  hsvToRgb(hue, 1.0f, 1.0f, red, green, blue);
  writeRgb(red, green, blue, AWAKE_DUTY_CAP);
}
