#ifndef DRIVER_CONTROL_H
#define DRIVER_CONTROL_H

#include <ArduinoJson.h>
#include "postman_mqtt.h"
#include "pocketlab_io.h"
#include <BasicLinearAlgebra.h>
#include <StateSpaceControl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

using namespace BLA;

// Data buffer configuration
#define CONTROL_SYSTEM_BUFFER_SIZE 20  // Store 20 samples (for ~1 second at 100Hz)
#define CONTROL_SYSTEM_FREQUENCY_HZ 100  // 100Hz = 10ms period
#define VA_BUFFER_SIZE 50  // Store up to 50 VA measurement points before sending
#define BODE_BUFFER_SIZE 20  // Store up to 20 Bode measurement points before sending
#define STEP_DATA_POINTS 200  // Fixed 200 data points for step response
#define IMPULSE_DATA_POINTS 200  // Fixed 200 data points for impulse response

struct ControlSystemData {
    unsigned long timestamp;
    float input_value;
    float state_x1, state_x2;
    float output_y1, output_y2;
};

// VA characteristics measurement data
struct VAMeasurementData {
    float voltage;
    float current;
    unsigned long timestamp;
};

// VA measurement configuration
struct VAMeasurementConfig {
    String channel;           // CH0, CH1, CH2
    String mode_type;        // CV (Constant Voltage) or CC (Constant Current)
    float start_voltage;      // Target start device voltage (V_A - V_B)
    float end_voltage;        // Target end device voltage (V_A - V_B)
    float step_voltage;       // Device voltage step size
    float start_current;      // Target start current for CC mode
    float end_current;        // Target end current for CC mode
    float step_current;       // Current step size for CC mode
    float shunt_resistance;   // Shunt resistor value in Ohms for current calculation
    int total_steps;          // Maximum number of steps
    int current_step;         // Current step index
    float cc_output_voltage;  // Current output voltage for CC mode closed-loop control
    float output_voltage;     // Current output voltage being applied
    float max_output_voltage; // Maximum output voltage for the channel
    float target_device_voltage; // Current target device voltage
    bool capped;              // True if measurement ended due to output voltage limit
};

// Bode measurement data point
struct BodeMeasurementData {
    float frequency;
    float gain;       // in dB
    float phase;      // in degrees
};

// Bode measurement configuration
struct BodeMeasurementConfig {
    String channel;           // CH0, CH1, CH2
    float freq_from;          // Start frequency in Hz
    float freq_to;            // End frequency in Hz
    int points_per_decade;    // Number of measurement points per decade
    float output_voltage;     // Output signal amplitude
    int total_points;         // Total number of frequency points
    int current_point;        // Current measurement index
};

// Step response measurement data point
struct StepMeasurementData {
    float time;
    float response;
};

// Step response configuration
struct StepMeasurementConfig {
    String channel;           // CH0, CH1, CH2
    float voltage;            // Step voltage
    float measurement_time;   // Total measurement time in seconds
    int total_points;         // Fixed at 200
    int current_point;        // Current measurement index
    unsigned long start_time; // Measurement start timestamp
    float time_step;          // Time between measurements
};

// Impulse response measurement data point
struct ImpulseMeasurementData {
    float time;
    float response;
};

// Impulse response configuration
struct ImpulseMeasurementConfig {
    float voltage;            // Impulse voltage
    int duration_us;          // Impulse duration in microseconds
    float measurement_time;   // Total measurement time in seconds
    int total_points;         // Fixed at 200
    int current_point;        // Current measurement index
    unsigned long start_time; // Measurement start timestamp
    float time_step;          // Time between measurements
    bool impulse_applied;     // Whether impulse has been applied
};

// Basic structure for Driver Control library
class DriverControl {
public:
    DriverControl(PostmanMQTT& postman, PocKETlabIO& io);
    ~DriverControl();

    void handleCommand(const JsonDocument& doc);
    void loop();
    void sendBufferedData();  // Send accumulated data via MQTT
    
    // Status reporting
    const char* getCurrentMode() const;

private:
    PostmanMQTT& _postman;
    PocKETlabIO& _io;
    bool _testbed_running;
    unsigned long _testbed_last_update;
    int _testbed_update_interval;
    
    // Control system variables
    bool _control_system_running;
    unsigned long _control_system_last_update;
    float _control_system_dt;
    
    // FreeRTOS task management
    TaskHandle_t _control_task_handle;
    SemaphoreHandle_t _data_mutex;
    
    // Data buffering for high-frequency sampling
    ControlSystemData _data_buffer[CONTROL_SYSTEM_BUFFER_SIZE];
    volatile int _buffer_write_index;
    volatile int _buffer_count;
    unsigned long _last_data_send;
    
    // State-space simulation using StateSpaceControl library
    Model<2, 1, 2>* _system_model;  // 2 states, 1 input, 2 outputs
    Simulation<2, 1, 2>* _simulation;
    Matrix<1> _control_input;  // 1x1 input matrix
    Matrix<2> _system_output;  // 2x1 output matrix
    
    // Voltage mapping
    float _input_min_volts, _input_max_volts, _input_zero_offset;
    float _output_min_volts, _output_max_volts, _output_zero_offset;
    
    // VA characteristics measurement
    bool _va_running;
    VAMeasurementConfig _va_config;
    unsigned long _va_last_measurement;
    int _va_measurement_delay_ms;  // Delay between measurements
    
    // VA data buffering
    VAMeasurementData _va_data_buffer[VA_BUFFER_SIZE];
    int _va_buffer_count;
    unsigned long _va_last_data_send;
    
    // Bode characteristics measurement
    bool _bode_running;
    BodeMeasurementConfig _bode_config;
    BodeMeasurementData _bode_data_buffer[BODE_BUFFER_SIZE];
    int _bode_buffer_count;
    unsigned long _bode_last_measurement;
    int _bode_measurement_delay_ms;
    
    // Step response measurement
    bool _step_running;
    StepMeasurementConfig _step_config;
    StepMeasurementData _step_data_buffer[STEP_DATA_POINTS];
    int _step_buffer_count;
    unsigned long _step_last_measurement;
    
    // Impulse response measurement
    bool _impulse_running;
    ImpulseMeasurementConfig _impulse_config;
    ImpulseMeasurementData _impulse_data_buffer[IMPULSE_DATA_POINTS];
    int _impulse_buffer_count;
    unsigned long _impulse_last_measurement;
    
    // Current mode tracking
    String _current_mode;

    // Testbed per-pin last-set values (volts). NAN indicates 'not set' (use measured or default).
    float _testbed_da_value_v[4];
    float _testbed_db_value_v[4];

    void handleVA(JsonObjectConst settings);
    void handleBode(JsonObjectConst settings);
    void handleStep(JsonObjectConst settings);
    void handleImpulse(JsonObjectConst settings);
    void handleTestbed(JsonObjectConst settings);
    void handleControlSystem(JsonObjectConst settings);
    void handleStopCommand(const char* mode);
    
    // Control system helpers
    void handleControllerMode(JsonObjectConst settings);
    void handleSystemMode(JsonObjectConst settings);
    void updateControlSystem();
    float voltageToSystemValue(float voltage);
    float systemValueToVoltage(float value);
    float roundTo3Decimals(float value);  // Helper to round to 3 decimal places
    float roundTo6Decimals(float value);  // Helper to round to 6 decimal places for small currents
    
    // FreeRTOS task functions
    static void controlSystemTaskWrapper(void* parameter);
    void controlSystemTask();
    void startControlSystemTask();
    void stopControlSystemTask();
    
    // VA characteristics helpers
    void performVAMeasurement();
    void sendVADataPoint(float voltage, float current, float progress, bool completed);
    void sendBufferedVAData(bool completed);
    void stopVAMeasurement();
    bool isValidVAChannel(const String& channel, const String& mode_type);
    
    // Bode characteristics helpers
    void performBodeMeasurement();
    void sendBufferedBodeData(bool completed);
    void stopBodeMeasurement();
    float calculateBodeFrequency(int point_index);
    int calculateTotalBodePoints();
    
    // Step response helpers
    void performStepMeasurement();
    void sendBufferedStepData(bool completed);
    void stopStepMeasurement();
    
    // Impulse response helpers
    void performImpulseMeasurement();
    void sendBufferedImpulseData(bool completed);
    void stopImpulseMeasurement();
};

#endif // DRIVER_CONTROL_H
