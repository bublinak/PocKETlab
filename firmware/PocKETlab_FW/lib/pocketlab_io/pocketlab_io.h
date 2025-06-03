#ifndef POCKETLAB_IO_H
#define POCKETLAB_IO_H

#include <Arduino.h>
#include <SPI.h>
#include "MCP_ADC.h"
#include "MCP_DAC.h"
#include "../../include/pin_definitions.h"

// ADC configuration
#define ADC_REFERENCE_VOLTAGE 3.3f  // 3.3V reference
#define ADC_MAX_VALUE 4095          // 12-bit ADC (2^12 - 1)

// DAC configuration  
#define DAC_REFERENCE_VOLTAGE 2.048f // 2.048V with 1x gain (safer default)
#define DAC_MAX_VALUE 4095           // 12-bit DAC (2^12 - 1)

// Signal path amplifier configuration
#define SIGNAL_AMPLIFIER_GAIN 6.7f   // Op-amp gain on signal outputs
#define SIGNAL_FEEDBACK_GAIN 1.0f    // FB_A0/A1 are 1:1 (before amplifier)

// ADC input attenuator configuration (MCP3202 inputs)
#define ADC_DIVIDER_LOSS 1.6663
#define ADC_INPUT_LOSS 6.8 * ADC_DIVIDER_LOSS  // Input voltage divider gain

// Channel definitions for signal ADC/DAC
enum SignalChannel {
    SIGNAL_CHANNEL_A = 0,
    SIGNAL_CHANNEL_B = 1
};

// Power control ranges
#define POWER_VOLTAGE_MIN 0.0f
#define POWER_VOLTAGE_MAX 20.0f  // Assuming 20V max from power supply
#define POWER_CURRENT_MIN 0.0f
#define POWER_CURRENT_MAX 3.0f   // Assuming 3A max current

class PocKETlabIO {
public:
    PocKETlabIO();
    
    // Initialization
    bool begin();
    void end();
    
    // === Power Control Functions ===
    // Set power voltage (0-20V range)
    bool setPowerVoltage(float voltage);
    
    // Set power current limit (0-3A range)  
    bool setPowerCurrent(float current);
    
    // Read power feedback values
    float readPowerVoltage();    // From FB_VOUT (GPIO5)
    float readPowerCurrent();    // From FB_IOUT (GPIO4)
    float readGroundVoltage();   // From FB_GOUT (GPIO3)
      // === Signal Control Functions ===
    // Set signal DAC outputs - voltage is FINAL OUTPUT after 6.7x amplifier
    // Range: 0 to ~13.7V (limited by 2.048V DAC * 6.7x gain)
    bool setSignalVoltage(SignalChannel channel, float voltage);
      // Read signal ADC inputs (compensated for input attenuator)
    float readSignalVoltage(SignalChannel channel);
    
    // Read raw signal ADC voltage (before attenuator compensation)
    float readSignalVoltageRaw(SignalChannel channel);
      // Read signal DAC feedback (from FB_AO, FB_A1) - BEFORE amplifier (0-2.048V)
    float readSignalFeedback(SignalChannel channel);
    
    // Get expected amplified output voltage based on current DAC setting
    float getExpectedSignalOutput(SignalChannel channel);
    
    // === Advanced Control ===
    // Simultaneous DAC update (using LDAC)
    void updateAllDACs();
    
    // Raw ADC/DAC access
    uint16_t readRawADC(uint8_t channel);
    bool writeRawDAC(uint8_t dac, uint8_t channel, uint16_t value);
    
    // Temperature monitoring
    float readTemperature();  // From NTC probe on GPIO10
    
    // === Calibration and Configuration ===
    // Set custom reference voltages if needed
    void setADCReference(float voltage);
    void setDACReference(float voltage);    // Get current ranges and references
    float getADCReference() const { return _adcRefVoltage; }
    float getDACReference() const { return _dacRefVoltage; }
    float getPowerVoltageRange() const { return POWER_VOLTAGE_MAX; }
    float getPowerCurrentRange() const { return POWER_CURRENT_MAX; }
    float getSignalVoltageRange() const { return DAC_REFERENCE_VOLTAGE * SIGNAL_AMPLIFIER_GAIN; }
    float getSignalInputRange() const { return ADC_REFERENCE_VOLTAGE * ADC_INPUT_LOSS; }

    // Status and diagnostics
    bool isInitialized() const { return _initialized; }
    void printStatus();

private:
    // Hardware objects
    MCP3202 *_signalADC;      // U8 - Signal ADC (MCP3202)
    MCP4822 *_signalDAC;      // U5 - Signal DAC (MCP4822) 
    MCP4822 *_powerDAC;       // U6 - Power DAC (MCP4822)
    
    // Configuration
    float _adcRefVoltage;
    float _dacRefVoltage;
    bool _initialized;
    
    // SPI settings
    SPISettings _spiSettings;
    
    // Internal helper functions
    float _rawToVoltage(uint16_t raw, float refVoltage, uint16_t maxValue);
    uint16_t _voltageToRaw(float voltage, float refVoltage, uint16_t maxValue);
    bool _validateVoltageRange(float voltage, float minV, float maxV);
    
    // ADC reading helpers
    float _readAnalogPin(int pin);  // For feedback pins using built-in ADC
    
    // Temperature calculation helper
    float _calculateTemperature(uint16_t rawADC);
};

#endif // POCKETLAB_IO_H
