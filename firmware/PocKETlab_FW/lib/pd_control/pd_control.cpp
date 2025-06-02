#include "pd_control.h"

// Constructor for CH224K (CFG1, CFG2, CFG3 pins)
PDControl::PDControl(PDChipType type, int cfg1Pin, int cfg2Pin, int cfg3Pin) 
    : chipType(type), _cfg1Pin(cfg1Pin), _cfg2Pin(cfg2Pin), _cfg3Pin(cfg3Pin), 
      _currentNominalVoltage(5.0), _maxTestedSourceVoltage(0.0) {
    if (type != CH224K) {
        // Invalid constructor for this chip type
    }
}

// Constructor for CH224Q (SDA, SCL pins for I2C)
PDControl::PDControl(PDChipType type, int sdaPin, int sclPin) 
    : chipType(type), _sdaPin(sdaPin), _sclPin(sclPin), 
      _currentNominalVoltage(5.0), _maxTestedSourceVoltage(0.0) {
    if (type != CH224Q) {
        // Invalid constructor for this chip type
    }
}

// Constructor for 5V only mode (no pins needed)
PDControl::PDControl(PDChipType type) 
    : chipType(type), _currentNominalVoltage(5.0), _maxTestedSourceVoltage(5.0) {
    if (type != FIVE_V_ONLY) {
        // Invalid constructor for this chip type
    }
}

void PDControl::begin() {
    if (chipType == CH224K) {
        // Initialize CH224K pins as outputs
        pinMode(_cfg1Pin, OUTPUT);
        pinMode(_cfg2Pin, OUTPUT);
        pinMode(_cfg3Pin, OUTPUT);
        
        // Set initial voltage to 5V
        _setCH224KVoltagePins(5.0);
    } else if (chipType == CH224Q) {
        // Initialize I2C pins (placeholder - actual I2C init would go here)
        pinMode(_sdaPin, OUTPUT);
        pinMode(_sclPin, OUTPUT);
    } else if (chipType == FIVE_V_ONLY) {
        // No pins to initialize in 5V only mode
        Serial.println("PD Control: 5V only mode - no PD chip control");
        _maxTestedSourceVoltage = 5.0;
        return; // Skip voltage testing
    }
    
    // Test all PD modes to find max source voltage
    float testVoltages[] = {5.0, 9.0, 12.0, 15.0, 20.0};
    int numVoltages = sizeof(testVoltages) / sizeof(testVoltages[0]);
    
    for (int i = 0; i < numVoltages; i++) {
        setPDVoltage(testVoltages[i]);
        delay(100); // Wait for voltage to stabilize
        
        // Read actual voltage (placeholder - would need actual voltage measurement)
        float actualVoltage = readPDVoltage();
        
        // Check if this voltage is supported by the source
        if (actualVoltage >= testVoltages[i] * 0.9) { // Allow 10% tolerance
            _maxTestedSourceVoltage = testVoltages[i];
        }
    }
    
    // Set back to 5V after testing
    setPDVoltage(5.0);
}

int PDControl::readPDStatus() {
    // Placeholder - would implement actual status reading
    // For CH224K: might read from status pins if available
    // For CH224Q: would read via I2C
    return 1; // Assume PD is active
}

float PDControl::readPDVoltage() {
    // Placeholder - would implement actual voltage reading
    // This could be from ADC measurement or register reading
    return _currentNominalVoltage;
}

bool PDControl::setPDVoltage(float voltage) {
    if (chipType == FIVE_V_ONLY) {
        if (voltage != 5.0) {
            Serial.print("PD Control: 5V only mode - cannot set voltage to ");
            Serial.print(voltage);
            Serial.println("V, staying at 5V");
            return false;
        }
        return true; // 5V is always "set" in 5V only mode
    }
    
    if (chipType == CH224K) {
        _setCH224KVoltagePins(voltage);
        _currentNominalVoltage = voltage;
        return true;
    } else if (chipType == CH224Q) {
        // Placeholder for I2C communication
        // Would send I2C command to set voltage
        _currentNominalVoltage = voltage;
        return true;
    }
    return false;
}

bool PDControl::setPPSVoltage(float voltage) {
    if (chipType == FIVE_V_ONLY) {
        Serial.println("PD Control: 5V only mode - PPS not supported");
        return false;
    }
    
    if (chipType != CH224Q) {
        return false; // PPS only supported on CH224Q
    }
    
    // Placeholder for I2C communication
    // Would send I2C command to set PPS voltage
    return true;
}

bool PDControl::setPPSCurrentLimit(float current) {
    if (chipType == FIVE_V_ONLY) {
        Serial.println("PD Control: 5V only mode - PPS not supported");
        return false;
    }
    
    if (chipType != CH224Q) {
        return false; // PPS only supported on CH224Q
    }
    
    // Placeholder for I2C communication
    // Would send I2C command to set PPS current limit
    return true;
}

float PDControl::getMaxTestedSourceVoltage() {
    return _maxTestedSourceVoltage;
}

bool PDControl::isFiveVOnlyMode() {
    return chipType == FIVE_V_ONLY;
}

void PDControl::_setCH224KVoltagePins(float voltage) {
    // CH224K voltage configuration via CFG pins
    // Based on typical CH224K configuration:
    // CFG1 CFG2 CFG3 -> Voltage
    //  0    0    0   ->  5V
    //  0    0    1   ->  9V
    //  0    1    0   -> 12V
    //  0    1    1   -> 15V
    //  1    0    0   -> 20V
    
    bool cfg1 = false, cfg2 = false, cfg3 = false;
    
    if (voltage >= 20.0) {
        cfg1 = true; cfg2 = false; cfg3 = false; // 20V
    } else if (voltage >= 15.0) {
        cfg1 = false; cfg2 = true; cfg3 = true; // 15V
    } else if (voltage >= 12.0) {
        cfg1 = false; cfg2 = true; cfg3 = false; // 12V
    } else if (voltage >= 9.0) {
        cfg1 = false; cfg2 = false; cfg3 = true; // 9V
    } else {
        cfg1 = false; cfg2 = false; cfg3 = false; // 5V (default)
    }
    
    digitalWrite(_cfg1Pin, cfg1);
    digitalWrite(_cfg2Pin, cfg2);
    digitalWrite(_cfg3Pin, cfg3);
}
