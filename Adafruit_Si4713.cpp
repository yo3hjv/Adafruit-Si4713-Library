/*!
 *  @file Adafruit_Si4713.cpp
 *
 *  @mainpage Adafruit Si4713 breakout
 *
 *  @section intro_sec Introduction
 *
 * 	I2C Driver for Si4713 breakout
 *
 * 	This is a library for the Adafruit Si4713 breakout:
 * 	http://www.adafruit.com/products/1958
 *
 * 	Adafruit invests time and resources providing this open source code,
 *  please support Adafruit and open-source hardware by purchasing products from
 * 	Adafruit!
 *
 *  @section author Author
 *
 *  Limor Fried/Ladyada (Adafruit Industries).
 *
 * 	@section license License
 *
 * 	BSD (see license.txt)
 *
 *     v1.0 - First release
 */
 
/*
 * Fixed improper initialisation of the SI4713 circuit
 * By Adrian YO3HJV @April 2026
 *
 * Changes vs original Adafruit library:
 *
 * begin():
 *   Moved reset() call to BEFORE i2c_dev->begin() (I2C address scan).
 *   The original code scanned the I2C bus before resetting the chip; if the
 *   chip was in an undefined power-on state it would not ACK its address,
 *   causing begin() to return false and requiring multiple MCU resets.
 *
 * reset():
 *   Added delay(100) after NRST de-assertion (final digitalWrite HIGH).
 *   Without this delay, powerUp() was issued before the chip completed its
 *   internal boot sequence, causing intermittent I2C failures on fast MCUs
 *   such as ESP32. Required by Si4713 datasheet power-up timing (sec. 8).
 *
 * sendCommand():
 *   Added a 5-second timeout to the CTS polling loop. The original loop was
 *   infinite; if the chip failed to assert CTS the MCU would block
 *   permanently. On timeout a diagnostic message is printed to Serial.
 *
 * getRev():
 *   Chip revision details (part number, firmware, patch, component, chip rev)
 *   are now always printed to Serial unconditionally. The original code
 *   guarded all output behind #ifdef SI4713_CMD_DEBUG, making startup
 *   verification impossible in normal builds. A firmware field duplication
 *   and missing component version field present in the original debug block
 *   are also corrected.
 *   Per Si4713 datasheet, FWMAJOR, FWMINOR, CMPMAJOR and CMPMINOR are ASCII
 *   characters (like CHIPREV). Fixed display from Serial.print(..., HEX) to
 *   Serial.write() so firmware shows as e.g. "2.0" instead of "32.30".
 *
 * powerUp():
 *   Corrected the inline comment for setProperty(SI4713_PROP_TX_ACOMP_ENABLE,
 *   0x0): the original comment read "turn on limiter and AGC" which is the
 *   opposite of what value 0x0 does (disables the audio compander entirely).
 *   Documented correct values: 0x02 = limiter only, 0x03 = limiter + AGC.
 */

#include "Adafruit_Si4713.h"

/*!
 *    @brief  Instantiates a new Si4713 class
 *    @param  resetpin
 *            number of pin where reset is connected
 *
 */
Adafruit_Si4713::Adafruit_Si4713(int8_t resetpin) {
  _rst = resetpin;
}

/*!
 *    @brief  Setups the i2c and calls powerUp function.
 *    @param  addr
 *            i2c address
 *    @param  theWire
 *            wire object
 *    @return True if initialization was successful, otherwise false.
 *
 */
bool Adafruit_Si4713::begin(uint8_t addr, TwoWire* theWire) {
  if (i2c_dev)
    delete i2c_dev;
  i2c_dev = new Adafruit_I2CDevice(addr, theWire);

  reset(); // reset BEFORE I2C scan: chip must be booted to ACK its address

  if (!i2c_dev->begin())
    return false;

  powerUp();

  // check for Si4713
  if (getRev() != 13)
    return false;

  return true;
}

/*!
 *    @brief  Resets the registers to default settings and puts chip in
 * powerdown mode
 */
void Adafruit_Si4713::reset() {
  if (_rst > 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);
    delay(10);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(100); // wait for chip internal boot after NRST de-assertion (datasheet req.)
  }
}

/*!
 *    @brief  Set chip property over I2C
 *    @param  property
 *            prooperty that will be set
 *    @param  value
 *            value of property
 */
void Adafruit_Si4713::setProperty(uint16_t property, uint16_t value) {
  _i2ccommand[0] = SI4710_CMD_SET_PROPERTY;
  _i2ccommand[1] = 0;
  _i2ccommand[2] = property >> 8;
  _i2ccommand[3] = property & 0xFF;
  _i2ccommand[4] = value >> 8;
  _i2ccommand[5] = value & 0xFF;
  sendCommand(6);

#ifdef SI4713_CMD_DEBUG
  Serial.print("Set Prop ");
  Serial.print(property);
  Serial.print(" = ");
  Serial.println(value);
#endif
}

/*!
 *    @brief  Read a chip property value over I2C using GET_PROPERTY (0x13).
 *    @param  property  property address
 *    @return 16-bit property value read from chip, or 0 on timeout
 */
uint16_t Adafruit_Si4713::getProperty(uint16_t property) {
  _i2ccommand[0] = SI4710_CMD_GET_PROPERTY;
  _i2ccommand[1] = 0;
  _i2ccommand[2] = property >> 8;
  _i2ccommand[3] = property & 0xFF;
  sendCommand(4);

  uint8_t resp[4];
  i2c_dev->read(resp, 4);
  return ((uint16_t)resp[2] << 8) | resp[3];
}

/*!
 *    @brief  Send command stored in _i2ccommand to chip.
 *    @param  len
 *            length of command that will be send
 */
void Adafruit_Si4713::sendCommand(uint8_t len) {
  // Send command
  i2c_dev->write(_i2ccommand, len);
  // Wait for status CTS bit with 5-second timeout
  uint8_t status = 0;
  uint32_t t = millis();
  while (!(status & SI4710_STATUS_CTS)) {
    if (millis() - t > 5000) {
      Serial.println("SI4713: sendCommand timeout - CTS not received!");
      return;
    }
    i2c_dev->read(&status, 1);
  }
}

/*!
 *    @brief  Tunes to given transmit frequency.
 *    @param  freqKHz
 *            frequency in KHz
 */
void Adafruit_Si4713::tuneFM(uint16_t freqKHz) {
  _i2ccommand[0] = SI4710_CMD_TX_TUNE_FREQ;
  _i2ccommand[1] = 0;
  _i2ccommand[2] = freqKHz >> 8;
  _i2ccommand[3] = freqKHz;
  sendCommand(4);
  while ((getStatus() & 0x81) != 0x81) {
    delay(10);
  }
}

/*!
 *    @brief  Sets the output power level and tunes the antenna capacitor
 *    @param  pwr
 *            power value
 *    @param  antcap
 * 	          antenna capacitor (default to 0)
 */
void Adafruit_Si4713::setTXpower(uint8_t pwr, uint8_t antcap) {
  _i2ccommand[0] = SI4710_CMD_TX_TUNE_POWER;
  _i2ccommand[1] = 0;
  _i2ccommand[2] = 0;
  _i2ccommand[3] = pwr;
  _i2ccommand[4] = antcap;
  sendCommand(5);
}
/*!
 *    @brief  Queries the TX status and input audio signal metrics.
 */
void Adafruit_Si4713::readASQ() {
  _i2ccommand[0] = SI4710_CMD_TX_ASQ_STATUS;
  _i2ccommand[1] = 0x1;
  sendCommand(2);

  uint8_t resp[5];
  i2c_dev->read(resp, 5);
  currASQ = resp[1];
  currInLevel = resp[4];
}

/*!
 *    @brief  Queries the status of a previously sent TX Tune Freq, TX Tune
 * Power, or TX Tune Measure using SI4710_CMD_TX_TUNE_STATUS command.
 */
void Adafruit_Si4713::readTuneStatus() {
  _i2ccommand[0] = SI4710_CMD_TX_TUNE_STATUS;
  _i2ccommand[1] = 0x1; // INTACK
  sendCommand(2);

  uint8_t resp[8];
  i2c_dev->read(resp, 8);
  currFreq = (uint16_t(resp[2]) << 8) | resp[3];
  currdBuV = resp[5];
  currAntCap = resp[6];
  currNoiseLevel = resp[7];
}

/*!
 *    @brief  Measure the received noise level at the specified frequency using
 *            SI4710_CMD_TX_TUNE_MEASURE command.
 *    @param  freq
 *            frequency
 */
void Adafruit_Si4713::readTuneMeasure(uint16_t freq) {
  // check freq is multiple of 50khz
  if (freq % 5 != 0) {
    freq -= (freq % 5);
  }
  // Serial.print("Measuring "); Serial.println(freq);
  _i2ccommand[0] = SI4710_CMD_TX_TUNE_MEASURE;
  _i2ccommand[1] = 0;
  _i2ccommand[2] = freq >> 8;
  _i2ccommand[3] = freq;
  _i2ccommand[4] = 0;

  sendCommand(5);
  while (getStatus() != 0x81)
    delay(10);
}

/*!
 *    @brief  Begin RDS
 *            Sets properties as follows:
 *            SI4713_PROP_TX_AUDIO_DEVIATION: 66.25KHz,
 *            SI4713_PROP_TX_RDS_DEVIATION: 2KHz,
 *            SI4713_PROP_TX_RDS_INTERRUPT_SOURCE: 1,
 *            SI4713_PROP_TX_RDS_PS_MIX: 50% mix (default value),
 *            SI4713_PROP_TX_RDS_PS_MISC: 0x1008,
 *            SI4713_PROP_TX_RDS_PS_REPEAT_COUNT: 3,
 *            SI4713_PROP_TX_RDS_MESSAGE_COUNT: 1,
 *            SI4713_PROP_TX_RDS_PS_AF: 0xE0E0,
 *            SI4713_PROP_TX_RDS_FIFO_SIZE: 0,
 *            SI4713_PROP_TX_COMPONENT_ENABLE: 7
 *    @param  programID
 *            sets SI4713_PROP_TX_RDS_PI to parameter value
 */
void Adafruit_Si4713::beginRDS(uint16_t programID) {
  setProperty(SI4713_PROP_TX_AUDIO_DEVIATION,
              6625);                              // 66.25KHz (default is 68.25)
  setProperty(SI4713_PROP_TX_RDS_DEVIATION, 200); // 2KHz (default)
  setProperty(SI4713_PROP_TX_RDS_INTERRUPT_SOURCE, 0x0001); // RDS IRQ
  setProperty(SI4713_PROP_TX_RDS_PI, programID);      // program identifier
  setProperty(SI4713_PROP_TX_RDS_PS_MIX, 0x03);       // 50% mix (default)
  setProperty(SI4713_PROP_TX_RDS_PS_MISC, 0x1008);    // RDSD0 & RDSMS (default)
  setProperty(SI4713_PROP_TX_RDS_PS_REPEAT_COUNT, 3); // 3 repeats (default)
  setProperty(SI4713_PROP_TX_RDS_MESSAGE_COUNT, 1);   // 1 message (default)
  setProperty(SI4713_PROP_TX_RDS_PS_AF, 0xE0E0);      // no AF (default)
  setProperty(SI4713_PROP_TX_RDS_FIFO_SIZE, 0);       // no FIFO (default)
  setProperty(SI4713_PROP_TX_COMPONENT_ENABLE,
              0x0007); // enable RDS, stereo, tone
}

/*!
 *    @brief  Set up the RDS station string
 *    @param  *s
 *            string to load
 */
void Adafruit_Si4713::setRDSstation(const char* s) {
  uint8_t len = strlen(s);
  uint8_t slots = (len + 3) / 4;

  for (uint8_t i = 0; i < slots; i++) {
    memset(_i2ccommand, ' ', 6); // clear it with ' '
    memcpy(_i2ccommand + 2, s, min(4, (int)strlen(s)));
    s += 4;
    _i2ccommand[6] = 0;
#ifdef SI4713_CMD_DEBUG
    Serial.print("Set slot #");
    Serial.print(i);
    char* slot = (char*)(_i2ccommand + 2);
    Serial.print(" to '");
    Serial.print(slot);
    Serial.println("'");
#endif
    _i2ccommand[0] = SI4710_CMD_TX_RDS_PS;
    _i2ccommand[1] = i; // slot #
    sendCommand(6);
  }
}

/*!
 *    @brief  Queries the status of the RDS Group Buffer and loads new data into
 * buffer.
 *    @param  *s
 *            string to load
 */
void Adafruit_Si4713::setRDSbuffer(const char* s) {
  uint8_t len = strlen(s);
  uint8_t slots = (len + 3) / 4;

  for (uint8_t i = 0; i < slots; i++) {
    memset(_i2ccommand, ' ', 8); // clear it with ' '
    memcpy(_i2ccommand + 4, s, min(4, (int)strlen(s)));
    s += 4;
    _i2ccommand[8] = 0;
#ifdef SI4713_CMD_DEBUG
    Serial.print("Set buff #");
    Serial.print(i);
    char* slot = (char*)(_i2ccommand + 4);
    Serial.print(" to '");
    Serial.print(slot);
    Serial.println("'");
#endif
    _i2ccommand[0] = SI4710_CMD_TX_RDS_BUFF;
    if (i == 0)
      _i2ccommand[1] = 0x06;
    else
      _i2ccommand[1] = 0x04;

    _i2ccommand[2] = 0x20;
    _i2ccommand[3] = i;
    sendCommand(8);
  }
}

/*!
 *    @brief  Read interrupt status bits.
 *    @return status bits
 */
uint8_t Adafruit_Si4713::getStatus() {
  uint8_t resp[1] = {SI4710_CMD_GET_INT_STATUS};
  i2c_dev->write_then_read(resp, 1, resp, 1);
  return resp[0];
}

/*!
 *    @brief  Sends power up command to the breakout, than CTS and GPO2 output
 * is disabled and than enable xtal oscilator. Also It sets properties:
 *            SI4713_PROP_REFCLK_FREQ: 32.768
 *            SI4713_PROP_TX_PREEMPHASIS: 74uS pre-emph (USA standard)
 *            SI4713_PROP_TX_ACOMP_GAIN: max gain
 *            SI4713_PROP_TX_ACOMP_ENABLE: turned on limiter and AGC
 */
void Adafruit_Si4713::powerUp() {
  _i2ccommand[0] = SI4710_CMD_POWER_UP;
  _i2ccommand[1] = 0x12;
  // CTS interrupt disabled
  // GPO2 output disabled
  // Boot normally
  // xtal oscillator ENabled
  // FM transmit
  _i2ccommand[2] = 0x50; // analog input mode
  sendCommand(3);

  // configuration! see page 254
  setProperty(SI4713_PROP_REFCLK_FREQ, 32768); // crystal is 32.768
  setProperty(SI4713_PROP_TX_PREEMPHASIS, 0);  // 74uS pre-emph (USA std)
  setProperty(SI4713_PROP_TX_ACOMP_GAIN, 10);  // max gain?
  // setProperty(SI4713_PROP_TX_ACOMP_ENABLE, 0x02); // turn on limiter, but no
  // dynamic ranging
  setProperty(SI4713_PROP_TX_ACOMP_ENABLE, 0x0); // audio compander disabled (0x02=limiter only, 0x03=limiter+AGC)
}

/*!
 *    @brief  Get the hardware revision code from the device using
 * SI4710_CMD_GET_REV
 *    @return revision number
 */
uint8_t Adafruit_Si4713::getRev() {
  _i2ccommand[0] = SI4710_CMD_GET_REV;
  _i2ccommand[1] = 0;
  sendCommand(2);

  uint8_t resp[9];
  i2c_dev->read(resp, 9);
  uint8_t  pn      = resp[1];
  uint8_t  fwmaj   = resp[2]; // ASCII character per datasheet
  uint8_t  fwmin   = resp[3]; // ASCII character per datasheet
  uint16_t patch   = ((uint16_t)resp[4] << 8) | resp[5];
  uint8_t  cmpmaj  = resp[6]; // ASCII character per datasheet
  uint8_t  cmpmin  = resp[7]; // ASCII character per datasheet
  uint8_t  chiprev = resp[8]; // ASCII character per datasheet

  Serial.print("Part # Si47");
  Serial.println(pn);
  Serial.print("Firmware : ");
  Serial.write(fwmaj);
  Serial.print(".");
  Serial.write(fwmin);
  Serial.println();
  Serial.print("Patch    : 0x");
  Serial.println(patch, HEX);
  Serial.print("Component: ");
  Serial.write(cmpmaj);
  Serial.print(".");
  Serial.write(cmpmin);
  Serial.println();
  Serial.print("Chip Rev : ");
  Serial.write(chiprev);
  Serial.println();

  return pn;
}

/*!
 *    @brief  Configures GP1 / GP2 as output or Hi-Z.
 *    @param  x
 *            bit value
 */
void Adafruit_Si4713::setGPIOctrl(uint8_t x) {
#ifdef SI4713_CMD_DEBUG
  Serial.println("GPIO direction");
#endif
  _i2ccommand[0] = SI4710_CMD_GPO_CTL;
  _i2ccommand[1] = x;
  sendCommand(2);
}

/*!
 *    @brief  Sets GP1 / GP2 output level (low or high).
 *    @param  x
 *            bit value
 */
void Adafruit_Si4713::setGPIO(uint8_t x) {
#ifdef SI4713_CMD_DEBUG
  Serial.println("GPIO set");
#endif
  _i2ccommand[0] = SI4710_CMD_GPO_SET;
  _i2ccommand[1] = x;
  sendCommand(2);
}

/*!
 *    @brief  Powers down the Si4713 (POWER_DOWN command 0x11).
 *            Call begin() again to restart the chip.
 */
void Adafruit_Si4713::powerDown() {
  _i2ccommand[0] = SI4710_CMD_POWER_DOWN;
  _i2ccommand[1] = 0;
  sendCommand(2);
}

/*!
 *    @brief  Powers up the Si4713 in digital audio input mode (I2S).
 *            Equivalent to powerUp() but with ARG2 = 0xD0 instead of 0x50.
 *    @param  sampleRate  I2S sample rate in Hz (e.g. 44100). Default 44100.
 */
void Adafruit_Si4713::powerUpDigital(uint16_t sampleRate) {
  _i2ccommand[0] = SI4710_CMD_POWER_UP;
  _i2ccommand[1] = 0x12; // XOSCEN=1, FM TX
  _i2ccommand[2] = 0xD0; // digital audio input mode
  sendCommand(3);

  setProperty(SI4713_PROP_REFCLK_FREQ, 32768);
  setProperty(SI4713_PROP_DIGITAL_INPUT_FORMAT, 0x0000); // I2S, 16-bit, stereo
  setProperty(SI4713_PROP_DIGITAL_INPUT_SAMPLE_RATE, sampleRate);
  setProperty(SI4713_PROP_TX_PREEMPHASIS, 0);
  setProperty(SI4713_PROP_TX_ACOMP_GAIN, 10);
  setProperty(SI4713_PROP_TX_ACOMP_ENABLE, 0x0);
}

/*!
 *    @brief  Configures the audio dynamic range control (compander).
 *    @param  mode           0=disabled, 0x01=limiter only, 0x02=compander(ADRC) only, 0x03=both
 *    @param  threshold      Input level threshold in dBFS (-40..0). Default -40.
 *    @param  attack         Attack time index 0-4 (0.5/1/1.5/2/2.5 ms). Default 0.
 *    @param  release        Compander release time index 0-9 (100..8000 ms). Default 4.
 *    @param  gain           Compander gain 0-20 dB. Default 15.
 *    @param  limiterRelease Limiter-specific release time (TX_LIMITER_RELEASE_TIME).
 *                           Default 102 = 5.01 ms per datasheet.
 */
void Adafruit_Si4713::setAudioCompander(uint8_t mode, int8_t threshold,
                                        uint8_t attack, uint8_t release,
                                        uint8_t gain, uint8_t limiterRelease) {
  setProperty(SI4713_PROP_TX_ACOMP_THRESHOLD,    (uint16_t)(int16_t)threshold);
  setProperty(SI4713_PROP_TX_ATTACK_TIME,         attack);
  setProperty(SI4713_PROP_TX_RELEASE_TIME,        release);
  setProperty(SI4713_PROP_TX_ACOMP_GAIN,          gain);
  setProperty(SI4713_PROP_TX_LIMITER_RELEASE_TIME, limiterRelease);
  setProperty(SI4713_PROP_TX_ACOMP_ENABLE,        mode); // enable last
}

/*!
 *    @brief  Configures Audio Signal Quality (ASQ) interrupt thresholds.
 *            After calling this, readASQ() will report low/high audio events.
 *    @param  levelLow   Low level threshold in dBFS (-70..0). Silence detect.
 *    @param  durLow     Duration below low threshold to trigger event (ms).
 *    @param  levelHigh  High level threshold in dBFS (-70..0). Overdrive detect.
 *    @param  durHigh    Duration above high threshold to trigger event (ms).
 */
void Adafruit_Si4713::setASQThresholds(int8_t levelLow, uint16_t durLow,
                                       int8_t levelHigh, uint16_t durHigh) {
  setProperty(SI4713_PROP_TX_ASQ_INTERRUPT_SOURCE, 0x03); // enable both interrupts
  setProperty(SI4713_PROP_TX_ASQ_LEVEL_LOW,        (uint16_t)(int16_t)levelLow);
  setProperty(SI4713_PROP_TX_ASQ_DURATION_LOW,     durLow);
  setProperty(SI4713_PROP_TX_ASQ_LEVEL_HIGH,       (uint16_t)(int16_t)levelHigh);
  setProperty(SI4713_PROP_TX_ASQ_DURATION_HIGH,    durHigh);
}

/*!
 *    @brief  Scans a range of FM frequencies and returns the one with the
 *            lowest measured noise level (best candidate for transmission).
 *    @param  startFreq  Start of scan range in 10 kHz units (e.g. 8750).
 *    @param  endFreq    End of scan range in 10 kHz units (e.g. 10800).
 *    @param  step       Step size in 10 kHz units. Default 10 (= 100 kHz).
 *    @return Frequency with lowest noise, in 10 kHz units.
 */
uint16_t Adafruit_Si4713::scanNoise(uint16_t startFreq, uint16_t endFreq,
                                    uint8_t step) {
  uint16_t bestFreq  = startFreq;
  uint8_t  bestNoise = 0xFF;

  for (uint16_t f = startFreq; f <= endFreq; f += step) {
    readTuneMeasure(f);
    readTuneStatus();
    if (currNoiseLevel < bestNoise) {
      bestNoise = currNoiseLevel;
      bestFreq  = f;
    }
  }
  return bestFreq;
}

/*!
 *    @brief  Sets the RDS Programme Identifier (PI) code standalone.
 *    @param  pi  16-bit PI code (e.g. 0xADAF for Adafruit default).
 */
void Adafruit_Si4713::setRDSpi(uint16_t pi) {
  setProperty(SI4713_PROP_TX_RDS_PI, pi);
}
