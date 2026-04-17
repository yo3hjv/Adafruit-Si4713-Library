/*
 * si4713_test.ino — Si4713 FM Transmitter sketch for ESP32
 *
 * Author  : Adrian YO3HJV
 * Date    : April 2026
 *
 * BSD 2-Clause License
 *
 * Copyright (c) 2026, Adrian YO3HJV
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <Wire.h>
#include "Adafruit_Si4713.h"   // local library (same folder as sketch)

#define SI4713_RST 13   // hardware reset pin

Adafruit_Si4713 radio(SI4713_RST);

/* =========================
   CONFIGURABLE PARAMETERS
   ========================= */

// TX frequency (units: 10 kHz, range 7600–10800)
uint16_t TX_FREQUENCY = 10780;     // 107.80 MHz

// RF output power (0–115 dBµV)
uint8_t  TX_POWER     = 110;

// MPX component enable flags
// bit0: L+R, bit1: L-R (stereo), bit2: pilot, bit3: RDS
uint16_t TX_COMPONENT_ENABLE = 0x000F;

// Audio deviation (units: 10 Hz)
uint16_t TX_AUDIO_DEVIATION = 7200;  // ~72 kHz

// Pilot tone deviation (units: 10 Hz)
uint16_t TX_PILOT_DEVIATION = 830;   // ~8.3 kHz

// RDS deviation (units: 10 Hz)
uint16_t TX_RDS_DEVIATION   = 200;   // 2 kHz

// Line input level (0–16383)
uint16_t TX_LINE_INPUT_LEVEL = 6000;

// Line input mute (0x0000 = not muted)
uint16_t TX_LINE_INPUT_MUTE  = 0x0000;

// Pre-emphasis time constant (0 = 75 µs / USA, 1 = 50 µs / Europe)
uint16_t TX_PREEMPHASIS = 1;

// Stereo pilot frequency (Hz)
uint16_t TX_PILOT_FREQUENCY = 19000;

// RDS Programme Service name (max 8 characters)
const char RDS_PS[] = "SI4713";

// RDS Programme Identifier (PI) code
uint16_t RDS_PI = 0xADAF;

/* --- Audio Compander (setAudioCompander) ---
 * mode      : 0=disabled, 0x01=limiter only, 0x02=compander(ADRC) only, 0x03=both
 * threshold : input level threshold in dBFS (-40..0)
 * attack    : attack time index 0-4 (0.5 / 1 / 1.5 / 2 / 2.5 ms)
 * release   : release time index 0-9 (100..8000 ms)
 * gain      : compander gain 0-20 dB
 */
uint8_t  ACOMP_MODE            = 0x03; // 0x00 none active/ 0x01 compander active/ 0x03 limiter + compander both active
int8_t   ACOMP_THRESHOLD       = -20;  // dBFS  (aggressive: compresses more signal)
uint8_t  ACOMP_ATTACK          = 0;    // 0.5 ms (fastest attack)
uint8_t  ACOMP_RELEASE         = 10;    // 200 ms (faster recovery = more aggressive)
uint8_t  ACOMP_GAIN            = 20;   // dB     (maximum compander gain)
uint8_t  ACOMP_LIMITER_RELEASE = 102;   // ~1.2 ms  (fast limiter release, datasheet default=102=5.01ms)

/* --- Audio Signal Quality thresholds (setASQThresholds) ---
 * levelLow  : silence detect threshold, dBFS (-70..0)
 * durLow    : time below low threshold to trigger event (ms)
 * levelHigh : overdrive detect threshold, dBFS (-70..0)
 * durHigh   : time above high threshold to trigger event (ms)
 */
int8_t   ASQ_LEVEL_LOW   = -50;  // dBFS
uint16_t ASQ_DUR_LOW     = 3000; // ms  (silence after 3 s)
int8_t   ASQ_LEVEL_HIGH  = -5;   // dBFS
uint16_t ASQ_DUR_HIGH    = 500;  // ms  (overdrive after 500 ms)

/* --- Noise scan range (scanNoise) ---
 * Frequencies in 10 kHz units: 8750 = 87.50 MHz, 10800 = 108.00 MHz
 * step: channel spacing in 10 kHz units (10 = 100 kHz European spacing)
 */
uint16_t SCAN_START = 8750;
uint16_t SCAN_END   = 10800;
uint8_t  SCAN_STEP  = 10;

/* ========================= */

void setup() {
  Serial.begin(115200);
  // 3-second Serial timeout (needed on ESP32 without native USB-CDC)
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < 3000));

  Serial.println("\n--- SI4713 FM Transmitter ---");

  Wire.begin();

  // begin() performs hardware reset, power-up and prints chip revision via getRev()
  if (!radio.begin()) {
    Serial.println("ERROR: SI4713 init failed! Check I2C and RST wiring.");
    while (1) { delay(1000); }
  }

  Serial.println("SI4713 initialised OK.");

  // TX / audio property setup
  radio.setProperty(0x2100, TX_COMPONENT_ENABLE);
  radio.setProperty(0x2101, TX_AUDIO_DEVIATION);
  radio.setProperty(0x2102, TX_PILOT_DEVIATION);
  radio.setProperty(0x2103, TX_RDS_DEVIATION);
  radio.setProperty(0x2104, TX_LINE_INPUT_LEVEL);
  radio.setProperty(0x2105, TX_LINE_INPUT_MUTE);
  radio.setProperty(0x2106, TX_PREEMPHASIS);
  radio.setProperty(0x2107, TX_PILOT_FREQUENCY);

  // RDS Programme Service + Programme Identifier
  radio.setRDSstation((char*)RDS_PS);
  radio.setRDSpi(RDS_PI);

  // Audio compander: limiter + ADRC with aggressive broadcast settings
  radio.setAudioCompander(ACOMP_MODE, ACOMP_THRESHOLD, ACOMP_ATTACK,
                          ACOMP_RELEASE, ACOMP_GAIN, ACOMP_LIMITER_RELEASE);

  // Audio Signal Quality thresholds (silence / overdrive detection)
  radio.setASQThresholds(ASQ_LEVEL_LOW, ASQ_DUR_LOW, ASQ_LEVEL_HIGH, ASQ_DUR_HIGH);

  // Set power and tune to frequency
  radio.setTXpower(TX_POWER, 0);
  radio.tuneFM(TX_FREQUENCY);

  // Read back and display all TX parameters from chip registers
  radio.readTuneStatus();
  Serial.println("\n--- TX Parameters (read from Si4713) ---");
  Serial.print("Frequency        : ");
  Serial.print(radio.currFreq / 100.0, 2);
  Serial.println(" MHz");
  Serial.print("RF Power         : ");
  Serial.print(radio.currdBuV);
  Serial.println(" dBuV");
  Serial.print("Antenna cap      : ");
  Serial.print(radio.currAntCap);
  Serial.println(" (0=auto)");
  Serial.print("MPX components   : 0x");
  Serial.println(radio.getProperty(0x2100), HEX);
  Serial.print("Audio deviation  : ");
  Serial.print(radio.getProperty(0x2101) / 10.0, 1);
  Serial.println(" kHz");
  Serial.print("Pilot deviation  : ");
  Serial.print(radio.getProperty(0x2102) / 10.0, 1);
  Serial.println(" kHz");
  Serial.print("RDS deviation    : ");
  Serial.print(radio.getProperty(0x2103) / 10.0, 1);
  Serial.println(" kHz");
  Serial.print("Line input level : ");
  Serial.println(radio.getProperty(0x2104));
  Serial.print("Line input mute  : 0x");
  Serial.println(radio.getProperty(0x2105), HEX);
  Serial.print("Pre-emphasis     : ");
  Serial.println(radio.getProperty(0x2106) == 0 ? "75 us (USA)" : "50 us (Europe)");
  Serial.print("Pilot frequency  : ");
  Serial.print(radio.getProperty(0x2107));
  Serial.println(" Hz");
  Serial.print("RDS PI code      : 0x");
  Serial.println(radio.getProperty(0x2C01), HEX);
  uint16_t acomp = radio.getProperty(0x2200);
  Serial.print("ACOMP enable     : 0x");
  Serial.print(acomp, HEX);
  Serial.println(acomp == 0 ? " (off)" : acomp == 1 ? " (limiter)" : acomp == 2 ? " (compander)" : " (limiter+compander)");
  Serial.print("ACOMP threshold  : ");
  Serial.print((int8_t)radio.getProperty(0x2201));
  Serial.println(" dBFS");
  Serial.print("ACOMP gain       : ");
  Serial.print(radio.getProperty(0x2204));
  Serial.println(" dB");
  Serial.print("Limiter release  : ");
  Serial.println(radio.getProperty(0x2205));
  Serial.println("----------------------------------------");
}

void loop() {
  //Serial.println("tick");  // heartbeat for debugging
  //delay(500);
}