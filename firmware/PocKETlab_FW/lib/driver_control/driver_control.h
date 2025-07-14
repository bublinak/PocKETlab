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
    float start_voltage;
    float end_voltage;
    float step_voltage;
    float start_current;
    float end_current;
    float step_current;
    int total_steps;
    int current_step;
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
    
    // Current mode tracking
    String _current_mode;

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
};

#endif // DRIVER_CONTROL_H
