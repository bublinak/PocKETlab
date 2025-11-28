#include "pocketlab_io.h"

PocKETlabIO::PocKETlabIO() 
    : _signalADC(nullptr), _signalDAC(nullptr), _powerDAC(nullptr),
      _adcRefVoltage(ADC_REFERENCE_VOLTAGE), _dacRefVoltage(DAC_REFERENCE_VOLTAGE),
            _initialized(false), _spiSettings(1000000, MSBFIRST, SPI_MODE0) {
        _ledc_initialized = false;
        for (int i = 0; i < 16; ++i) _ledc_channel_attached[i] = false;
}

bool PocKETlabIO::begin() {
    if (_initialized) {
        return true;
    }
    
    // Initialize SPI
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    
    // Configure chip select pins
    pinMode(PIN_CS_ADC_SIGNAL, OUTPUT);
    pinMode(PIN_CS_DAC_SIGNAL, OUTPUT);
    pinMode(PIN_CS_DAC_POWER, OUTPUT);
    digitalWrite(PIN_CS_ADC_SIGNAL, HIGH);
    digitalWrite(PIN_CS_DAC_SIGNAL, HIGH);
    digitalWrite(PIN_CS_DAC_POWER, HIGH);
    
    // Configure LDAC pin for simultaneous DAC updates
    pinMode(PIN_DAC_LDAC, OUTPUT);
    digitalWrite(PIN_DAC_LDAC, HIGH); // LDAC is active low
    
    // Configure feedback pins as analog inputs
    pinMode(PIN_FB_AO, INPUT);      // Signal A feedback
    pinMode(PIN_FB_A1, INPUT);      // Signal B feedback  
    pinMode(PIN_FB_GOUT, INPUT);    // Ground voltage feedback
    pinMode(PIN_FB_IOUT, INPUT);    // Current feedback
    pinMode(PIN_FB_VOUT, INPUT);    // Voltage feedback
    pinMode(PIN_TEMP_PROBE, INPUT); // Temperature probe
      // Initialize ADC (MCP3202)
    _signalADC = new MCP3202(&SPI);
    _signalADC->begin(PIN_CS_ADC_SIGNAL);
    
    // Initialize Signal DAC (MCP4822)
    _signalDAC = new MCP4822(&SPI);
    _signalDAC->begin(PIN_CS_DAC_SIGNAL);
    
    // Initialize Power DAC (MCP4822)
    _powerDAC = new MCP4822(&SPI);
    _powerDAC->begin(PIN_CS_DAC_POWER);
      // Configure DACs for 1x gain (safer default, lower voltage range)
    _signalDAC->setGain(1);  // 1x gain for 2.048V reference or full range
    _powerDAC->setGain(1);   // 1x gain for 2.048V reference or full range
    
    // Set LDAC pin for both DACs if the library supports it
    _signalDAC->setLatchPin(PIN_DAC_LDAC);
    _powerDAC->setLatchPin(PIN_DAC_LDAC);
    
    // Set all outputs to zero initially
    setPowerVoltage(0.0);
    setPowerCurrent(0.0);
    setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
    setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
    updateAllDACs();
    
    _initialized = true;
    Serial.println("PocKETlab I/O initialized successfully");
    printStatus();
    
    return true;
}

void PocKETlabIO::end() {
    if (!_initialized) {
        return;
    }
    
    // Set all outputs to safe values
    setPowerVoltage(0.0);
    setPowerCurrent(0.0);
    setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
    setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
    updateAllDACs();
    
    // Clean up objects
    delete _signalADC;
    delete _signalDAC;
    delete _powerDAC;
    _signalADC = nullptr;
    _signalDAC = nullptr;
    _powerDAC = nullptr;
    
    _initialized = false;
    Serial.println("PocKETlab I/O shutdown");
}

// === Power Control Functions ===

bool PocKETlabIO::setPowerVoltage(float voltage) {
    if (!_initialized || !_powerDAC) {
        return false;
    }
    
    // Calculate maximum output voltage considering amplifier gain
    float maxOutputVoltage = _dacRefVoltage * POWER_AMPLIFIER_GAIN;
    
    if (!_validateVoltageRange(voltage, POWER_VOLTAGE_MIN, maxOutputVoltage)) {
        Serial.printf("ERROR: Power voltage %.3fV out of range (%.1f-%.3fV)\n", 
                     voltage, POWER_VOLTAGE_MIN, maxOutputVoltage);
        return false;
    }
    
    // Compensate for amplifier gain: DAC voltage = desired output / gain
    float dacVoltage = voltage / POWER_AMPLIFIER_GAIN;
    
    // Convert voltage to DAC value
    uint16_t dacValue = _voltageToRaw(dacVoltage, _dacRefVoltage, DAC_MAX_VALUE);
    // Use channel A of power DAC for voltage control
    return _powerDAC->write(dacValue, 0);
}

bool PocKETlabIO::setPowerCurrent(float current) {
    if (!_initialized || !_powerDAC) {
        return false;
    }
    
    if (!_validateVoltageRange(current, POWER_CURRENT_MIN, POWER_CURRENT_MAX)) {
        Serial.printf("ERROR: Power current %.3fA out of range (%.1f-%.1fA)\n", 
                     current, POWER_CURRENT_MIN, POWER_CURRENT_MAX);
        return false;
    }
    
    // Convert current to voltage (assuming current sense resistor or similar)
    // This may need calibration based on actual hardware implementation
    float voltage = (current / POWER_CURRENT_MAX) * _dacRefVoltage;
    uint16_t dacValue = _voltageToRaw(voltage, _dacRefVoltage, DAC_MAX_VALUE);
      // Use channel B of power DAC for current control
    return _powerDAC->write(dacValue, 1);
}

float PocKETlabIO::readPowerVoltage() {
    return _readAnalogPin(PIN_FB_VOUT) * POWER_AMPLIFIER_GAIN; // Compensate for amplifier gain
}

float PocKETlabIO::readPowerCurrent() {
    return _readAnalogPin(PIN_FB_IOUT) * POWER_AMPLIFIER_GAIN; // Compensate for amplifier gain
}

float PocKETlabIO::readGroundVoltage() {
    return _readAnalogPin(PIN_FB_GOUT) * POWER_AMPLIFIER_GAIN; // Compensate for amplifier gain
}

// === Signal Control Functions ===

bool PocKETlabIO::setSignalVoltage(SignalChannel channel, float voltage) {
    if (!_initialized || !_signalDAC) {
        return false;
    }
    
    // Calculate maximum output voltage considering amplifier gain
    float maxOutputVoltage = _dacRefVoltage * SIGNAL_AMPLIFIER_GAIN;
    
    if (!_validateVoltageRange(voltage, 0.0f, maxOutputVoltage)) {
        Serial.printf("ERROR: Signal voltage %.3fV out of range (0.0-%.3fV)\n", 
                     voltage, maxOutputVoltage);
        return false;
    }
    
    // Compensate for amplifier gain: DAC voltage = desired output / gain
    float dacVoltage = voltage / SIGNAL_AMPLIFIER_GAIN;
    
    uint16_t dacValue = _voltageToRaw(dacVoltage, _dacRefVoltage, DAC_MAX_VALUE);
    return _signalDAC->write(dacValue, channel);
}

float PocKETlabIO::readSignalVoltage(SignalChannel channel) {
    if (!_initialized || !_signalADC) {
        return 0.0;
    }
    
    uint16_t rawValue = _signalADC->read(channel);
    float adcVoltage = _rawToVoltage(rawValue, _adcRefVoltage, ADC_MAX_VALUE);
    
    // Compensate for input attenuator: actual input voltage = ADC reading / gain
    return adcVoltage * ADC_INPUT_LOSS;
}

float PocKETlabIO::readSignalVoltageRaw(SignalChannel channel) {
    if (!_initialized || !_signalADC) {
        return 0.0;
    }
    
    uint16_t rawValue = _signalADC->read(channel);
    return _rawToVoltage(rawValue, _adcRefVoltage, ADC_MAX_VALUE);
}

float PocKETlabIO::readSignalFeedback(SignalChannel channel) {
    int pin = (channel == SIGNAL_CHANNEL_A) ? PIN_FB_AO : PIN_FB_A1;
    return _readAnalogPin(pin);
}

float PocKETlabIO::getExpectedSignalOutput(SignalChannel channel) {
    // Read the DAC feedback voltage and multiply by amplifier gain
    float dacVoltage = readSignalFeedback(channel);
    return dacVoltage * SIGNAL_AMPLIFIER_GAIN;
}

float PocKETlabIO::getExpectedPowerOutput() {
    // Read the power DAC feedback voltage and multiply by amplifier gain
    float dacVoltage = readPowerVoltage();
    return dacVoltage * POWER_AMPLIFIER_GAIN;
}

// === Advanced Control ===

void PocKETlabIO::updateAllDACs() {
    if (!_initialized) {
        return;
    }
    
    // Trigger LDAC to update all DAC outputs simultaneously
    _signalDAC->triggerLatch();
    _powerDAC->triggerLatch();
}

uint16_t PocKETlabIO::readRawADC(uint8_t channel) {
    if (!_initialized || !_signalADC || channel > 1) {
        return 0;
    }
    
    return _signalADC->read(channel);
}

bool PocKETlabIO::writeRawDAC(uint8_t dac, uint8_t channel, uint16_t value) {
    if (!_initialized || channel > 1 || value > DAC_MAX_VALUE) {
        return false;
    }
    
    MCP4822* targetDAC = nullptr;
    if (dac == 0) {
        targetDAC = _signalDAC;
    } else if (dac == 1) {
        targetDAC = _powerDAC;
    } else {
        return false;
    }
    
    if (!targetDAC) {
        return false;
    }
    
    return targetDAC->write(value, channel);
}

float PocKETlabIO::readTemperature() {
    if (!_initialized) {
        return 0.0;
    }
    
    // Read temperature from NTC probe
    float voltage = _readAnalogPin(PIN_TEMP_PROBE);
    
    // Convert voltage to temperature (this needs proper NTC calibration)
    // For now, return a placeholder calculation
    return _calculateTemperature((uint16_t)(voltage * ADC_MAX_VALUE / _adcRefVoltage));
}

// === Calibration and Configuration ===

void PocKETlabIO::setADCReference(float voltage) {
    _adcRefVoltage = voltage;
}

void PocKETlabIO::setDACReference(float voltage) {
    _dacRefVoltage = voltage;
}

void PocKETlabIO::printStatus() {
    if (!_initialized) {
        Serial.println("PocKETlab I/O: Not initialized");
        return;
    }
    Serial.println("=== PocKETlab I/O Status ===");
    Serial.printf("ADC Reference: %.3fV\n", _adcRefVoltage);
    Serial.printf("DAC Reference: %.3fV\n", _dacRefVoltage);
    Serial.printf("Power Output Range: 0.0-%.1fV (after %.1fx amplifier)\n", 
                  getPowerVoltageRange(), POWER_AMPLIFIER_GAIN);
    Serial.printf("Power Current Range: %.1f-%.1fA\n", POWER_CURRENT_MIN, POWER_CURRENT_MAX);
    Serial.printf("Signal Output Range: 0.0-%.1fV (after %.1fx amplifier)\n",
                  getSignalVoltageRange(), SIGNAL_AMPLIFIER_GAIN);
    Serial.printf("Signal Input Range: 0.0-%.1fV (with %.3fx input loss)\n", 
                  getSignalInputRange(), ADC_INPUT_LOSS);
    Serial.printf("Signal DAC Range: 0.0-%.3fV (before amplifier)\n", _dacRefVoltage);
      Serial.println("\n--- Current Readings ---");
    
    // Show both power DAC feedback and expected amplified output
    float powerFeedback = readPowerVoltage();
    float expectedPowerOutput = getExpectedPowerOutput();
    Serial.printf("Power: DAC: %.3fV (before amp), Expected Output: %.2fV (after amp)\n", 
                  powerFeedback, expectedPowerOutput);
    
    Serial.printf("Power Current: %.3fA\n", readPowerCurrent());
    Serial.printf("Ground Voltage: %.3fV\n", readGroundVoltage());
    
    // Show both raw and compensated ADC readings
    float rawInputA = readSignalVoltageRaw(SIGNAL_CHANNEL_A);
    float compensatedInputA = readSignalVoltage(SIGNAL_CHANNEL_A);
    float rawInputB = readSignalVoltageRaw(SIGNAL_CHANNEL_B);
    float compensatedInputB = readSignalVoltage(SIGNAL_CHANNEL_B);
    
    Serial.printf("Signal A Input: ADC: %.3fV, Actual: %.2fV (compensated)\n", 
                  rawInputA, compensatedInputA);
    Serial.printf("Signal B Input: ADC: %.3fV, Actual: %.2fV (compensated)\n", 
                  rawInputB, compensatedInputB);
    
    // Show both DAC feedback and expected amplified output
    float feedbackA = readSignalFeedback(SIGNAL_CHANNEL_A);
    float feedbackB = readSignalFeedback(SIGNAL_CHANNEL_B);
    Serial.printf("Signal A Feedback: %.3fV (DAC), Expected Output: %.2fV\n", 
                  feedbackA, feedbackA * SIGNAL_AMPLIFIER_GAIN);
    Serial.printf("Signal B Feedback: %.3fV (DAC), Expected Output: %.2fV\n", 
                  feedbackB, feedbackB * SIGNAL_AMPLIFIER_GAIN);
    
    Serial.printf("Temperature: %.1f°C\n", readTemperature());
    Serial.println("============================");
}

// === Private Helper Functions ===

float PocKETlabIO::_rawToVoltage(uint16_t raw, float refVoltage, uint16_t maxValue) {
    return (float)raw * refVoltage / (float)maxValue;
}

uint16_t PocKETlabIO::_voltageToRaw(float voltage, float refVoltage, uint16_t maxValue) {
    if (voltage < 0.0) voltage = 0.0;
    if (voltage > refVoltage) voltage = refVoltage;
    
    return (uint16_t)((voltage / refVoltage) * (float)maxValue + 0.5); // Round to nearest
}

bool PocKETlabIO::_validateVoltageRange(float voltage, float minV, float maxV) {
    return (voltage >= minV && voltage <= maxV);
}

float PocKETlabIO::_readAnalogPin(int pin) {
    // Use ESP32's built-in ADC for feedback pins
    uint16_t rawValue = analogRead(pin);
    
    // ESP32 ADC is 12-bit (0-4095) with default 3.3V reference
    return (float)rawValue * 3.3f / 4095.0f;
}

float PocKETlabIO::_calculateTemperature(uint16_t rawADC) {
    // Placeholder temperature calculation for NTC thermistor
    // This needs proper calibration based on the actual NTC characteristics
    // For now, assume a simple linear relationship for demonstration
    
    float voltage = _rawToVoltage(rawADC, _adcRefVoltage, ADC_MAX_VALUE);
    
    // Example calculation - replace with actual NTC formula
    // Assuming 25°C at mid-voltage, 100°C range
    float temperature = 25.0 + (voltage - _adcRefVoltage/2.0) * 50.0;
    
    return temperature;
}

// === DA channels (MCU-direct I/O) ===

int PocKETlabIO::_mapDA(uint8_t channel) {
    switch (channel) {
        case 0: return PIN_DA0;
        case 1: return PIN_DA1;
        case 2: return PIN_DA2;
        case 3: return PIN_DA3;
        default: return -1;
    }
}

void PocKETlabIO::configureDA(uint8_t channel, uint8_t mode, bool pullup) {
    int pin = _mapDA(channel);
    if (pin < 0) return;
    if (mode == INPUT && pullup) {
        pinMode(pin, INPUT_PULLUP);
    } else {
        pinMode(pin, mode);
    }
}

void PocKETlabIO::digitalWriteDA(uint8_t channel, bool level) {
    int pin = _mapDA(channel);
    if (pin < 0) return;
    pinMode(pin, OUTPUT); // ensure output
    digitalWrite(pin, level ? HIGH : LOW);
}

int PocKETlabIO::digitalReadDA(uint8_t channel) {
    int pin = _mapDA(channel);
    if (pin < 0) return 0;
    pinMode(pin, INPUT);
    return digitalRead(pin) == HIGH ? 1 : 0;
}

float PocKETlabIO::analogReadDA(uint8_t channel) {
    int pin = _mapDA(channel);
    if (pin < 0) return 0.0f;
    pinMode(pin, INPUT);
    uint16_t raw = analogRead(pin);
    return (float)raw * 3.3f / 4095.0f; // Scale to voltage (assuming 3.3V ref)
}

void PocKETlabIO::analogWriteDAVoltage(uint8_t channel, float voltage_v) {
    int pin = _mapDA(channel);
    if (pin < 0) return;
    _analogWriteVoltageLEDC(/*ledc_channel*/ channel, pin, voltage_v);
}

// === DB channels (GPIO33..36) ===
int PocKETlabIO::_mapDB(uint8_t channel) {
    switch (channel) {
        case 0: return PIN_DB0;
        case 1: return PIN_DB1;
        case 2: return PIN_DB2;
        case 3: return PIN_DB3;
        default: return -1;
    }
}

void PocKETlabIO::configureDB(uint8_t channel, uint8_t mode, bool pullup) {
    int pin = _mapDB(channel);
    if (pin < 0) return;
    if (mode == INPUT && pullup) {
        pinMode(pin, INPUT_PULLUP);
    } else {
        pinMode(pin, mode);
    }
}

void PocKETlabIO::digitalWriteDB(uint8_t channel, bool level) {
    int pin = _mapDB(channel);
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level ? HIGH : LOW);
}

int PocKETlabIO::digitalReadDB(uint8_t channel) {
    int pin = _mapDB(channel);
    if (pin < 0) return 0;
    pinMode(pin, INPUT);
    return digitalRead(pin) == HIGH ? 1 : 0;
}

void PocKETlabIO::analogWriteDBVoltage(uint8_t channel, float voltage_v) {
    // Use LEDC channels 4..7 for DB0..DB3
    int ledc_ch = 4 + channel;
    int pin = _mapDB(channel);
    if (pin < 0) return;
    _analogWriteVoltageLEDC((uint8_t)ledc_ch, pin, voltage_v);
}

// === LEDC helpers ===
void PocKETlabIO::_ensureLEDCSetup(uint8_t channel, uint8_t timer, uint32_t freq_hz, uint8_t resolution_bits) {
    // Only need to call ledcSetup once per channel
    if (!_ledc_initialized) {
        _ledc_initialized = true; // marker; per-channel setup tracked separately
    }
    if (!_ledc_channel_attached[channel]) {
        ledcSetup(channel, freq_hz, resolution_bits);
    }
}

void PocKETlabIO::_attachLEDC(uint8_t channel, int pin) {
    if (!_ledc_channel_attached[channel]) {
        ledcAttachPin(pin, channel);
        _ledc_channel_attached[channel] = true;
    }
}

void PocKETlabIO::_analogWriteVoltageLEDC(uint8_t ledc_channel, int pin, float voltage_v) {
    // Clamp voltage 0..3.3V
    if (voltage_v < 0.0f) voltage_v = 0.0f;
    if (voltage_v > 3.3f) voltage_v = 3.3f;
    // Configure LEDC channel at 5kHz, 10-bit resolution
    const uint32_t freq = 5000;
    const uint8_t bits = 10;
    _ensureLEDCSetup(ledc_channel, /*timer*/ 0, freq, bits);
    _attachLEDC(ledc_channel, pin);
    uint32_t max_duty = (1u << bits) - 1u;
    uint32_t duty = (uint32_t)((voltage_v / 3.3f) * (float)max_duty + 0.5f);
    ledcWrite(ledc_channel, duty);
}
