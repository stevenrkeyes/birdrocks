#pragma once

#include <Arduino.h>

// RGB LED (common cathode: higher duty = brighter).
constexpr uint8_t RGB_GREEN_PIN = D3;  // GPIO4
constexpr uint8_t RGB_RED_PIN = D4;    // GPIO5
constexpr uint8_t RGB_BLUE_PIN = D5;   // GPIO6

// MAX98357A I2S amplifier (XIAO ESP32-S3 header pins)
constexpr int I2S_BCLK = 7;  // D8
constexpr int I2S_LRC = 8;   // D9 (WS)
constexpr int I2S_DIN = 9;   // D10

// Sense expansion board PDM microphone
constexpr int PDM_CLK_PIN = 42;
constexpr int PDM_DATA_PIN = 41;
