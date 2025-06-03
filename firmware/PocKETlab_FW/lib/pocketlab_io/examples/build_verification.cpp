/**
 * Build Verification Script for PocKETlab Amplifier Compensation
 * 
 * This file can be temporarily renamed to main.cpp to test compilation
 * of the amplifier compensation features without running full tests.
 */

#include <Arduino.h>
#include "pocketlab_io.h"

// Minimal test to verify compilation
PocKETlabIO testInstance;

void setup() {
    Serial.begin(115200);
    
    // Test that all amplifier compensation functions compile
    testInstance.begin();
      // Test constant access
    float gain = SIGNAL_AMPLIFIER_GAIN;
    float feedback_gain = SIGNAL_FEEDBACK_GAIN;
    float adc_gain = ADC_INPUT_GAIN;
    
    // Test new functions
    float outputRange = testInstance.getSignalVoltageRange();
    float inputRange = testInstance.getSignalInputRange();
    
    // Test amplifier-compensated voltage setting
    testInstance.setSignalVoltage(SIGNAL_CHANNEL_A, 5.0f);
    
    // Test expected output calculation
    float expected = testInstance.getExpectedSignalOutput(SIGNAL_CHANNEL_A);
    
    // Test ADC input compensation
    float rawInput = testInstance.readSignalVoltageRaw(SIGNAL_CHANNEL_A);
    float compensatedInput = testInstance.readSignalVoltage(SIGNAL_CHANNEL_A);
    
    Serial.println("✓ Amplifier and ADC compensation features compiled successfully");
    Serial.print("Output amplifier gain: "); Serial.println(gain);
    Serial.print("ADC input gain: "); Serial.println(adc_gain);
    Serial.print("Max output range: "); Serial.println(outputRange);
    Serial.print("Max input range: "); Serial.println(inputRange);
    Serial.print("Expected output: "); Serial.println(expected);
    Serial.print("Raw input: "); Serial.println(rawInput);
    Serial.print("Compensated input: "); Serial.println(compensatedInput);
}

void loop() {
    delay(1000);
}
