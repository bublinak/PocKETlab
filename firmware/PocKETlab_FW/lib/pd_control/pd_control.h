#ifndef PD_CONTROL_H
#define PD_CONTROL_H

#include <Arduino.h>

// Forward declaration for Wire library if used for CH224Q
// #include <Wire.h> // Uncomment if you implement I2C

enum PDChipType {
    CH224K,
    CH224Q,
    FIVE_V_ONLY  // Fallback mode for broken PD chips
};

class PDControl {
public:
    // Constructor for CH224K (CFG1, CFG2, CFG3 pins)
    PDControl(PDChipType type, int cfg1Pin, int cfg2Pin, int cfg3Pin);    // Constructor for CH224Q (SDA, SCL pins for I2C)
    PDControl(PDChipType type, int sdaPin, int sclPin);

    // Constructor for 5V only mode (no pins needed)
    PDControl(PDChipType type);

    void begin(); // Initializes pins and tests max source voltage

    int readPDStatus(); // Reads the PD status (placeholder)
    float readPDVoltage(); // Reads the current PD voltage (returns nominal set voltage)
    bool setPDVoltage(float voltage); // Sets the PD voltage

    // CH224Q specific functions (PPS - Programmable Power Supply)
    bool setPPSVoltage(float voltage); // Sets PPS voltage (placeholder for I2C)
    bool setPPSCurrentLimit(float current); // Sets PPS current limit (placeholder for I2C)

    float getMaxTestedSourceVoltage(); // Gets the max voltage found during the initial test
    
    bool isFiveVOnlyMode(); // Returns true if in 5V only mode

private:
    PDChipType chipType;
    int _cfg1Pin, _cfg2Pin, _cfg3Pin; // For CH224K
    int _sdaPin, _sclPin;           // For CH224Q

    float _currentNominalVoltage;
    float _maxTestedSourceVoltage;

    void _setCH224KVoltagePins(float voltage); // Helper for CH224K pin manipulation
};

#endif // PD_CONTROL_H
