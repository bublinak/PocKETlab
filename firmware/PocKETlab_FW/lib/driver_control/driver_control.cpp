#include "driver_control.h"
#include <Arduino.h>

// Basic constructor
DriverControl::DriverControl(PostmanMQTT& postman, PocKETlabIO& io) : _postman(postman), _io(io), _testbed_running(false), _control_system_running(false), _va_running(false) {
    // Initialize hardware or other setup
    Serial.println("DriverControl initialized.");
    
    // Initialize state vector to zero
    _control_system_dt = 1.0 / CONTROL_SYSTEM_FREQUENCY_HZ; // 10ms for 100Hz
    
    // Initialize StateSpaceControl objects (will be properly configured when system model is loaded)
    _system_model = nullptr;
    _simulation = nullptr;
    _control_input.Fill(0);
    _system_output.Fill(0);
    
    // Initialize FreeRTOS components
    _control_task_handle = NULL;
    _data_mutex = xSemaphoreCreateMutex();
    
    // Initialize data buffer
    _buffer_write_index = 0;
    _buffer_count = 0;
    _last_data_send = 0;
    
    // Initialize VA measurement
    _va_last_measurement = 0;
    _va_measurement_delay_ms = 100;  // 100ms between measurements
    
    // Initialize VA data buffering
    _va_buffer_count = 0;
    _va_last_data_send = 0;
    
    // Initialize current mode tracking
    _current_mode = "none";
}

// Basic destructor
DriverControl::~DriverControl() {
    // Stop control system task if running
    stopControlSystemTask();
    
    // Stop VA measurement if running
    stopVAMeasurement();
    
    // Clean up StateSpaceControl objects
    if (_simulation != nullptr) {
        delete _simulation;
        _simulation = nullptr;
    }
    if (_system_model != nullptr) {
        delete _system_model;
        _system_model = nullptr;
    }
    
    // Clean up FreeRTOS components
    if (_data_mutex != NULL) {
        vSemaphoreDelete(_data_mutex);
    }
    
    // Cleanup resources
    Serial.println("DriverControl destroyed.");
}

void DriverControl::handleCommand(const JsonDocument& doc) {
    // Extract mode from payload
    const char* mode = doc["payload"]["mode"].as<const char*>();
    
    if (mode == nullptr) {
        Serial.println("ERROR: Command missing mode parameter");
        return;
    }
    
    // Check if this is a stop command (action: "stop")
    if (doc["payload"]["action"].is<const char*>() && 
        strcmp(doc["payload"]["action"].as<const char*>(), "stop") == 0) {
        handleStopCommand(mode);
        return;
    }
    
    // Handle regular payload-based commands with settings
    if (strcmp(mode, "va") == 0) {
        handleVA(doc["payload"]["settings"].as<JsonObjectConst>());
    } else if (strcmp(mode, "bode") == 0) {
        handleBode(doc["payload"]["settings"].as<JsonObjectConst>());
    } else if (strcmp(mode, "step") == 0) {
        handleStep(doc["payload"]["settings"].as<JsonObjectConst>());
    } else if (strcmp(mode, "impulse") == 0) {
        handleImpulse(doc["payload"]["settings"].as<JsonObjectConst>());
    } else if (strcmp(mode, "testbed") == 0) {
        handleTestbed(doc["payload"]["settings"].as<JsonObjectConst>());
    } else if (strcmp(mode, "control_system") == 0) {
        handleControlSystem(doc["payload"]["settings"].as<JsonObjectConst>());
    } else {
        Serial.printf("ERROR: Unknown mode: %s\n", mode);
    }
}

void DriverControl::loop() {
    if (_testbed_running) {
        if (millis() - _testbed_last_update > _testbed_update_interval) {
            _testbed_last_update = millis();

            JsonDocument doc;  // ArduinoJson 7 handles memory automatically
            doc["type"] = "data";
            doc["mode"] = "testbed";

            JsonObject payload = doc["payload"].to<JsonObject>();
            JsonObject readings = payload["readings"].to<JsonObject>();
            readings["output_voltage"] = roundTo3Decimals(_io.readPowerVoltage());
            readings["output_current"] = roundTo3Decimals(_io.readPowerCurrent());
            readings["input_ch0"] = roundTo3Decimals(_io.readSignalVoltage(SIGNAL_CHANNEL_A));
            readings["input_ch1"] = roundTo3Decimals(_io.readSignalVoltage(SIGNAL_CHANNEL_B));

            payload["status"] = "regulating"; // Placeholder
            payload["continuous"] = true;

            _postman.publish("data", doc);
        }
    }
    
    // Handle buffered data sending for control system (every 100ms to match buffer fill rate)
    if (_control_system_running) {
        // Buffer fills every 100ms at 100Hz with 20 samples (20 / 100Hz = 0.2s)
        if (millis() - _last_data_send >= 100) {
            sendBufferedData();
            _last_data_send = millis();
        }
    }
    
    // Handle VA characteristics measurement
    if (_va_running) {
        if (millis() - _va_last_measurement >= _va_measurement_delay_ms) {
            performVAMeasurement();
            _va_last_measurement = millis();
        }
    }
}

void DriverControl::handleVA(JsonObjectConst settings) {
    Serial.println("Handling VA Characteristics command");
    
    // Stop any existing VA measurement
    stopVAMeasurement();
    
    // Parse settings
    String channel = settings["va_channel"].as<String>();
    String mode_type = settings["va_mode_type"].as<String>();
    
    // Validate channel and mode combination
    if (!isValidVAChannel(channel, mode_type)) {
        _postman.sendError("E007", "Channel conflict or invalid mode", "va", "va_channel/va_mode_type", 
                          (channel + "/" + mode_type).c_str(), "CH0,CH1 support CV only; CH2 supports CV and CC");
        return;
    }
    
    // Configure measurement
    _va_config.channel = channel;
    _va_config.mode_type = mode_type;
    _va_config.start_voltage = settings["va_start_voltage"].as<float>();
    _va_config.end_voltage = settings["va_end_voltage"].as<float>();
    _va_config.step_voltage = settings["va_step_voltage"].as<float>();
    _va_config.start_current = settings["va_start_current"].as<float>();
    _va_config.end_current = settings["va_end_current"].as<float>();
    _va_config.step_current = settings["va_step_current"].as<float>();
    
    // Validate parameters
    if (mode_type == "CV") {
        if (_va_config.start_voltage < 0 || _va_config.end_voltage > 20 || 
            _va_config.start_voltage >= _va_config.end_voltage || _va_config.step_voltage <= 0) {
            _postman.sendError("E001", "Voltage parameter out of range", "va", "voltage", "", 
                              "Voltage must be 0-20V, start < end, step > 0");
            return;
        }
        // Calculate total steps for CV mode
        _va_config.total_steps = (int)(((_va_config.end_voltage - _va_config.start_voltage) / _va_config.step_voltage) + 1);
    } else if (mode_type == "CC") {
        if (_va_config.start_current < 0 || _va_config.end_current > 3 || 
            _va_config.start_current >= _va_config.end_current || _va_config.step_current <= 0) {
            _postman.sendError("E001", "Current parameter out of range", "va", "current", "", 
                              "Current must be 0-3A, start < end, step > 0");
            return;
        }
        // Calculate total steps for CC mode
        _va_config.total_steps = (int)(((_va_config.end_current - _va_config.start_current) / _va_config.step_current) + 1);
    }
    
    // Initialize measurement state
    _va_config.current_step = 0;
    _va_buffer_count = 0;  // Reset buffer for new measurement
    _va_running = true;
    _va_last_measurement = millis();
    _current_mode = "va";  // Set current mode
    
    // Calculate estimated duration (100ms per measurement + some overhead)
    int estimated_duration = (_va_config.total_steps * _va_measurement_delay_ms) / 1000 + 5;
    
    // Send success response
    _postman.sendResponse("va", "success", "VA measurement started", estimated_duration);
    
    Serial.printf("VA measurement started: %s mode on %s, %d steps, ~%ds\n", 
                  mode_type.c_str(), channel.c_str(), _va_config.total_steps, estimated_duration);
}

void DriverControl::handleBode(JsonObjectConst settings) {
    Serial.println("Handling Bode Plot command");
    _current_mode = "bode";  // Set current mode
    _postman.sendResponse("bode", "success", "Bode measurement started");
}

void DriverControl::handleStep(JsonObjectConst settings) {
    Serial.println("Handling Step Response command");
    _current_mode = "step";  // Set current mode

    const char* channel = settings["channel"].as<const char*>();
    float voltage = settings["voltage"].as<float>();
    float measurement_time = settings["measurement_time"].as<float>();

    Serial.printf("Channel: %s, Voltage: %.2fV, Time: %.3fs\n", channel, voltage, measurement_time);

    // Validate parameters
    if (voltage < 0 || voltage > 20 || measurement_time < 0.001 || measurement_time > 10) {
        _postman.sendError("E001", "Parameter out of range", "step", "voltage/measurement_time", "", "Check constraints");
        return;
    }

    _postman.sendResponse("step", "success", "Step measurement started");

    // TODO: Implement actual hardware control for step response
    // TODO: Implement data streaming for step response
}

void DriverControl::handleImpulse(JsonObjectConst settings) {
    Serial.println("Handling Impulse Response command");
    _current_mode = "impulse";  // Set current mode

    float voltage = settings["voltage"].as<float>();
    int duration_us = settings["duration_us"].as<int>();
    float measurement_time = settings["measurement_time"].as<float>();

    Serial.printf("Voltage: %.2fV, Duration: %dus, Time: %.3fs\n", voltage, duration_us, measurement_time);

    // Validate parameters
    if (voltage < 0 || voltage > 20 || duration_us < 1 || duration_us > 1000 || measurement_time < 0.001 || measurement_time > 2) {
        _postman.sendError("E001", "Parameter out of range", "impulse", "voltage/duration/time", "", "Check constraints");
        return;
    }

    _postman.sendResponse("impulse", "success", "Impulse measurement started");

    // TODO: Implement actual hardware control for impulse response
    // TODO: Implement data streaming for impulse response
}

void DriverControl::handleTestbed(JsonObjectConst settings) {
    Serial.println("Handling Testbed command");

    if (settings["action"].is<const char*>() && strcmp(settings["action"].as<const char*>(), "stop") == 0) {
        Serial.println("Stopping Testbed mode");
        _current_mode = "none";  // Reset mode when stopping
        // TODO: Add logic to stop continuous monitoring
        _postman.sendResponse("testbed", "success", "Testbed mode stopped");
        return;
    }

    _current_mode = "testbed";  // Set current mode
    float target_voltage = settings["target_voltage"].as<float>();
    float current_limit = settings["current_limit"].as<float>();
    bool continuous_monitoring = settings["continuous_monitoring"].as<bool>();
    int update_interval_ms = settings["update_interval_ms"].as<int>();

    Serial.printf("Target Voltage: %.2fV, Current Limit: %.2fA, Continuous: %d, Interval: %dms\n",
                  target_voltage, current_limit, continuous_monitoring, update_interval_ms);

    // Validate parameters
    if (target_voltage < 0 || target_voltage > 20 || current_limit < 0 || current_limit > 3 || update_interval_ms < 50 || update_interval_ms > 1000) {
        _postman.sendError("E001", "Parameter out of range", "testbed", "voltage/current/interval", "", "Check constraints");
        return;
    }

    _postman.sendResponse("testbed", "success", "Testbed mode activated");

    // TODO: Implement logic for continuous monitoring and data streaming
}

void DriverControl::handleControlSystem(JsonObjectConst settings) {
    Serial.println("Handling Control System command");
    _current_mode = "control_system";  // Set current mode
    
    const char* cs_mode = settings["cs_mode"].as<const char*>();
    
    if (strcmp(cs_mode, "controller") == 0) {
        handleControllerMode(settings);
    } else if (strcmp(cs_mode, "system") == 0) {
        handleSystemMode(settings);
    } else {
        _postman.sendError("E004", "Invalid control system mode", "control_system", "cs_mode", cs_mode, "Use 'controller' or 'system'");
        return;
    }
}

void DriverControl::handleControllerMode(JsonObjectConst settings) {
    Serial.println("Control System - Controller Mode");
    
    const char* controller_type = settings["cs_controller_type"].as<const char*>();
    
    if (strcmp(controller_type, "pid") == 0) {
        float kp = settings["cs_pid_kp"].as<float>();
        float ki = settings["cs_pid_ki"].as<float>();
        float kd = settings["cs_pid_kd"].as<float>();
        
        Serial.printf("PID Controller - Kp: %.3f, Ki: %.3f, Kd: %.3f\n", kp, ki, kd);
        
        // TODO: Implement PID controller logic
        _postman.sendResponse("control_system", "success", "PID Controller configured (placeholder)");
    } else {
        Serial.println("Controller type not implemented yet");
        _postman.sendResponse("control_system", "success", "Controller mode activated (placeholder)");
    }
}

void DriverControl::handleSystemMode(JsonObjectConst settings) {
    Serial.println("Control System - System Mode");
    
    // Stop any running control system
    stopControlSystemTask();
    
    // Parse system model
    JsonObjectConst system_model = settings["system_model"];
    if (system_model.isNull()) {
        _postman.sendError("E004", "Missing system_model", "control_system", "system_model", "", "Provide system model matrices");
        return;
    }
    
    // Parse A matrix (2x2)
    JsonArrayConst A_array = system_model["A"];
    if (A_array.size() != 2) {
        _postman.sendError("E004", "Invalid A matrix size", "control_system", "A", "", "A matrix must be 2x2");
        return;
    }
    
    Matrix<2,2> A_matrix;
    for (int i = 0; i < 2; i++) {
        JsonArrayConst row = A_array[i];
        if (row.size() != 2) {
            _postman.sendError("E004", "Invalid A matrix row size", "control_system", "A", "", "A matrix must be 2x2");
            return;
        }
        for (int j = 0; j < 2; j++) {
            A_matrix(i, j) = row[j].as<float>();
        }
    }
    
    // Parse B matrix (2x1)
    JsonArrayConst B_array = system_model["B"];
    if (B_array.size() != 2) {
        _postman.sendError("E004", "Invalid B matrix size", "control_system", "B", "", "B matrix must be 2x1");
        return;
    }
    
    Matrix<2,1> B_matrix;
    for (int i = 0; i < 2; i++) {
        JsonArrayConst row = B_array[i];
        if (row.size() != 1) {
            _postman.sendError("E004", "Invalid B matrix row size", "control_system", "B", "", "B matrix must be 2x1");
            return;
        }
        B_matrix(i, 0) = row[0].as<float>();
    }
    
    // Parse C matrix (2x2)
    JsonArrayConst C_array = system_model["C"];
    if (C_array.size() != 2) {
        _postman.sendError("E004", "Invalid C matrix size", "control_system", "C", "", "C matrix must be 2x2");
        return;
    }
    
    Matrix<2,2> C_matrix;
    for (int i = 0; i < 2; i++) {
        JsonArrayConst row = C_array[i];
        if (row.size() != 2) {
            _postman.sendError("E004", "Invalid C matrix row size", "control_system", "C", "", "C matrix must be 2x2");
            return;
        }
        for (int j = 0; j < 2; j++) {
            C_matrix(i, j) = row[j].as<float>();
        }
    }
    
    // Parse D matrix (2x1)
    JsonArrayConst D_array = system_model["D"];
    if (D_array.size() != 2) {
        _postman.sendError("E004", "Invalid D matrix size", "control_system", "D", "", "D matrix must be 2x1");
        return;
    }
    
    Matrix<2,1> D_matrix;
    for (int i = 0; i < 2; i++) {
        JsonArrayConst row = D_array[i];
        if (row.size() != 1) {
            _postman.sendError("E004", "Invalid D matrix row size", "control_system", "D", "", "D matrix must be 2x1");
            return;
        }
        D_matrix(i, 0) = row[0].as<float>();
    }
    
    // Parse voltage ranges
    JsonObjectConst input_range = system_model["input_voltage_range"];
    JsonObjectConst output_range = system_model["output_voltage_range"];
    
    _input_min_volts = input_range["min_volts"].as<float>();
    _input_max_volts = input_range["max_volts"].as<float>();
    _input_zero_offset = input_range["zero_offset"].as<float>();
    
    _output_min_volts = output_range["min_volts"].as<float>();
    _output_max_volts = output_range["max_volts"].as<float>();
    _output_zero_offset = output_range["zero_offset"].as<float>();
    
    // Copy matrices to member variables and create StateSpaceControl objects
    
    // Clean up existing objects
    if (_simulation != nullptr) {
        delete _simulation;
        _simulation = nullptr;
    }
    if (_system_model != nullptr) {
        delete _system_model;
        _system_model = nullptr;
    }
    
    // Create new Model object
    _system_model = new Model<2, 1, 2>();
    _system_model->A = A_matrix;
    _system_model->B = B_matrix;
    _system_model->C = C_matrix;
    _system_model->D = D_matrix;
    
    // Create simulation object
    _simulation = new Simulation<2, 1, 2>(*_system_model);
    
    // Initialize simulation state to zero
    _simulation->x.Fill(0);
    
    // Initialize data buffer
    _buffer_write_index = 0;
    _buffer_count = 0;
    _last_data_send = millis();
    
    // Print parsed model
    Serial.println("System Model Loaded:");
    Serial.printf("A = [%.2f %.2f; %.2f %.2f]\n", _system_model->A(0,0), _system_model->A(0,1), _system_model->A(1,0), _system_model->A(1,1));
    Serial.printf("B = [%.2f; %.2f]\n", _system_model->B(0,0), _system_model->B(1,0));
    Serial.printf("C = [%.2f %.2f; %.2f %.2f]\n", _system_model->C(0,0), _system_model->C(0,1), _system_model->C(1,0), _system_model->C(1,1));
    Serial.printf("D = [%.2f; %.2f]\n", _system_model->D(0,0), _system_model->D(1,0));
    Serial.printf("Input range: %.2f-%.2fV (zero: %.2fV)\n", _input_min_volts, _input_max_volts, _input_zero_offset);
    Serial.printf("Output range: %.2f-%.2fV (zero: %.2fV)\n", _output_min_volts, _output_max_volts, _output_zero_offset);
    Serial.printf("Control frequency: %dHz (%.1fms period)\n", CONTROL_SYSTEM_FREQUENCY_HZ, _control_system_dt * 1000);
    
    // Start the high-frequency control system task
    startControlSystemTask();
    
    _postman.sendResponse("control_system", "success", "System model loaded and high-frequency simulation started");
}

void DriverControl::updateControlSystem() {
    // Check if simulation is initialized
    if (_simulation == nullptr) {
        Serial.println("WARNING: Simulation not initialized");
        return;
    }
    
    // Read input from ADC (Channel A) and convert to system value
    float input_voltage = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
    float input_value = voltageToSystemValue(input_voltage);
    
    // Set control input for the simulation
    _control_input(0) = input_value;
    
    // Use StateSpaceControl library's simulation step function
    _system_output = _simulation->step(_control_input, _control_system_dt);
    
    // Convert outputs to voltages and set DACs
    float output1_voltage = systemValueToVoltage(_system_output(0));
    float output2_voltage = systemValueToVoltage(_system_output(1));
    
    // Set signal outputs
    _io.setSignalVoltage(SIGNAL_CHANNEL_A, output1_voltage);
    _io.setSignalVoltage(SIGNAL_CHANNEL_B, output2_voltage);
    _io.updateAllDACs();
    
    // Store data in buffer (thread-safe)
    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        _data_buffer[_buffer_write_index].timestamp = millis();
        _data_buffer[_buffer_write_index].input_value = input_value;
        _data_buffer[_buffer_write_index].state_x1 = _simulation->x(0);  // Access simulation state
        _data_buffer[_buffer_write_index].state_x2 = _simulation->x(1);  // Access simulation state
        _data_buffer[_buffer_write_index].output_y1 = _system_output(0);
        _data_buffer[_buffer_write_index].output_y2 = _system_output(1);
        
        _buffer_write_index = (_buffer_write_index + 1) % CONTROL_SYSTEM_BUFFER_SIZE;
        if (_buffer_count < CONTROL_SYSTEM_BUFFER_SIZE) {
            _buffer_count++;
        }
        
        // Debug: Print every 100th sample to avoid flooding (reduced from 10th)
        if (_buffer_write_index % 100 == 0) {
            Serial.printf("Control system data stored: input=%.3f, buffer_count=%d\n",
                         input_value, _buffer_count);
        }
        
        xSemaphoreGive(_data_mutex);
    } else {
        Serial.println("WARNING: Failed to take mutex for data storage");
    }
}

float DriverControl::voltageToSystemValue(float voltage) {
    // Map voltage to system value using the configured range
    // voltage range [min_volts, max_volts] maps to system range [-scale, +scale]
    // where zero_offset corresponds to 0 in system coordinates
    
    float voltage_range = _input_max_volts - _input_min_volts;
    float voltage_normalized = (voltage - _input_zero_offset) / (voltage_range / 2.0);
    
    return voltage_normalized;
}

float DriverControl::systemValueToVoltage(float value) {
    // Map system value to voltage using the configured range
    float voltage_range = _output_max_volts - _output_min_volts;
    float voltage = _output_zero_offset + value * (voltage_range / 2.0);
    
    // Clamp to valid range
    if (voltage < _output_min_volts) voltage = _output_min_volts;
    if (voltage > _output_max_volts) voltage = _output_max_volts;
    
    return voltage;
}

float DriverControl::roundTo3Decimals(float value) {
    // Round to 3 decimal places to reduce MQTT message size
    return round(value * 100.0) / 100.0;
}

// VA characteristics helper functions

bool DriverControl::isValidVAChannel(const String& channel, const String& mode_type) {
    // CH0 and CH1: CV mode only
    if ((channel == "CH0" || channel == "CH1") && mode_type == "CV") {
        return true;
    }
    // CH2: Both CV and CC modes supported
    if (channel == "CH2" && (mode_type == "CV" || mode_type == "CC")) {
        return true;
    }
    return false;
}

void DriverControl::performVAMeasurement() {
    if (!_va_running || _va_config.current_step >= _va_config.total_steps) {
        stopVAMeasurement();
        return;
    }
    
    float voltage, current;
    
    if (_va_config.mode_type == "CV") {
        // Constant Voltage mode: set voltage, measure current
        voltage = _va_config.start_voltage + (_va_config.current_step * _va_config.step_voltage);
        
        // Set the voltage on the appropriate channel
        if (_va_config.channel == "CH0") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_A, voltage);
        } else if (_va_config.channel == "CH1") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_B, voltage);
        } else if (_va_config.channel == "CH2") {
            _io.setPowerVoltage(voltage);
        }
        _io.updateAllDACs();
        
        // Wait a bit for voltage to settle
        delay(10);
        
        // Measure current
        if (_va_config.channel == "CH2") {
            current = _io.readPowerCurrent();
        } else {
            // For signal channels, we'd need to measure current differentl
            // This is a placeholder - actual implementation depends on hardware
            //current = 0.001; // Placeholder current reading
            if(_va_config.channel == "CH0") {
                current = _io.readSignalVoltage(SIGNAL_CHANNEL_A);  //Expects 1ohm load for current measurement
            } else if (_va_config.channel == "CH1") {
                current = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
            } else {
                current = 0.0; // Should not happen
            }
        }
        
    } else if (_va_config.mode_type == "CC") {
        // Constant Current mode: set current, measure voltage
        current = _va_config.start_current + (_va_config.current_step * _va_config.step_current);
        
        // Set the current on CH2 (only channel that supports CC mode)
        if (_va_config.channel == "CH2") {
            _io.setPowerCurrent(current);
            delay(10); // Wait for current to settle
            voltage = _io.readPowerVoltage();
        } else {
            // This shouldn't happen due to validation, but just in case
            voltage = 0.0;
        }
    }
    
    // Calculate progress
    float progress = (float)(_va_config.current_step + 1) / _va_config.total_steps * 100.0f;
    bool completed = (_va_config.current_step + 1) >= _va_config.total_steps;
    
    // Store data point in buffer
    if (_va_buffer_count < VA_BUFFER_SIZE) {
        _va_data_buffer[_va_buffer_count].voltage = voltage;
        _va_data_buffer[_va_buffer_count].current = current;
        _va_data_buffer[_va_buffer_count].timestamp = millis();
        _va_buffer_count++;
    }
    
    // Send buffered data if buffer is full or measurement is completed, or every 10 measurements
    if (_va_buffer_count >= VA_BUFFER_SIZE || completed || (_va_config.current_step % 10 == 9)) {
        sendBufferedVAData(completed);
    }
    
    // Move to next step
    _va_config.current_step++;
    
    if (completed) {
        Serial.println("VA measurement completed");
        _va_running = false;
        _current_mode = "none";  // Reset mode when measurement completes
    }
}

void DriverControl::sendVADataPoint(float voltage, float current, float progress, bool completed) {
    JsonDocument doc;
    
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());
    
    doc["timestamp"] = timestamp;
    doc["message_id"] = "va-data-" + String(millis());
    doc["type"] = "data";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = "va";
    
    // Create data array with single measurement point
    JsonArray data_array = payload["data"].to<JsonArray>();
    JsonObject data_point = data_array.add<JsonObject>();  // Use add<JsonObject>() to add object to array
    data_point["voltage"] = roundTo3Decimals(voltage);
    data_point["current"] = roundTo3Decimals(current);
    
    payload["progress"] = roundTo3Decimals(progress);
    payload["completed"] = completed;
    
    _postman.publish("data", doc);
    
    Serial.printf("VA data: V=%.3f, I=%.3f, Progress=%.1f%%, Completed=%s\n", 
                  voltage, current, progress, completed ? "true" : "false");
}

void DriverControl::sendBufferedVAData(bool completed) {
    if (_va_buffer_count == 0) return;  // No data to send
    
    JsonDocument doc;
    
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());
    
    doc["timestamp"] = timestamp;
    doc["message_id"] = "va-data-" + String(millis());
    doc["type"] = "data";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = "va";
    
    // Create data array with all buffered measurement points
    JsonArray data_array = payload["data"].to<JsonArray>();
    for (int i = 0; i < _va_buffer_count; i++) {
        JsonObject data_point = data_array.add<JsonObject>();
        data_point["voltage"] = roundTo3Decimals(_va_data_buffer[i].voltage);
        data_point["current"] = roundTo3Decimals(_va_data_buffer[i].current);
    }
    
    // Calculate progress based on current step
    float progress = (float)(_va_config.current_step + 1) / _va_config.total_steps * 100.0f;
    payload["progress"] = roundTo3Decimals(progress);
    payload["completed"] = completed;
    
    _postman.publish("data", doc);
    
    Serial.printf("VA buffered data sent: %d points, Progress=%.1f%%, Completed=%s\n", 
                  _va_buffer_count, progress, completed ? "true" : "false");
    
    // Reset buffer
    _va_buffer_count = 0;
    _va_last_data_send = millis();
}

void DriverControl::stopVAMeasurement() {
    if (_va_running) {
        // Send any remaining buffered data before stopping
        if (_va_buffer_count > 0) {
            sendBufferedVAData(true);  // Mark as completed since we're stopping
        }
        
        _va_running = false;
        _current_mode = "none";  // Reset mode when measurement stops
        
        // Reset outputs to safe values
        if (_va_config.channel == "CH0") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
        } else if (_va_config.channel == "CH1") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
        } else if (_va_config.channel == "CH2") {
            _io.setPowerVoltage(0.0);
        }
        _io.updateAllDACs();
        
        Serial.println("VA measurement stopped and outputs reset");
    }
}

void DriverControl::handleStopCommand(const char* mode) {
    Serial.printf("Handling Stop command for mode: %s\n", mode);
    
    if (strcmp(mode, "control_system") == 0) {
        // Stop the control system task
        stopControlSystemTask();
        _postman.sendResponse("control_system", "success", "Control system stopped");
        Serial.println("Control system stopped via MQTT command");
    } else if (strcmp(mode, "va") == 0) {
        // Stop VA measurement
        stopVAMeasurement();
        _postman.sendResponse("va", "success", "VA measurement stopped");
        Serial.println("VA measurement stopped via MQTT command");
    } else if (strcmp(mode, "testbed") == 0) {
        // Stop testbed mode
        _testbed_running = false;
        _current_mode = "none";
        _postman.sendResponse("testbed", "success", "Testbed mode stopped");
        Serial.println("Testbed mode stopped via MQTT command");
    } else {
        // Unknown mode
        _postman.sendError("E005", "Invalid stop mode", "stop", "mode", mode, "Use 'control_system', 'va', or 'testbed'");
        Serial.printf("Unknown stop mode: %s\n", mode);
    }
}

// FreeRTOS task wrapper (static function)
void DriverControl::controlSystemTaskWrapper(void* parameter) {
    DriverControl* instance = static_cast<DriverControl*>(parameter);
    instance->controlSystemTask();
}

// High-frequency control system task
void DriverControl::controlSystemTask() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000 / CONTROL_SYSTEM_FREQUENCY_HZ); // 20ms for 50Hz
    
    Serial.printf("Control system task started at %dHz (stack size: %d bytes)\n", 
                  CONTROL_SYSTEM_FREQUENCY_HZ, uxTaskGetStackHighWaterMark(NULL));
    
    int iteration = 0;
    while (_control_system_running) {
        // Monitor stack usage every 1000 iterations (every ~20 seconds at 50Hz)
        if (iteration % 1000 == 0) {
            UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("Control task iteration %d, stack remaining: %d bytes\n", iteration, stackRemaining);
            
            if (stackRemaining < 256) {
                Serial.println("WARNING: Control system task stack running low!");
            }
        }
        
        // Update control system
        updateControlSystem();
        
        iteration++;
        
        // Wait for next period (precise timing)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    
    Serial.println("Control system task stopped");
    vTaskDelete(NULL); // Delete this task
}

// Start the high-frequency control system task
void DriverControl::startControlSystemTask() {
    if (_control_task_handle == NULL) {
        _control_system_running = true;
        
        BaseType_t result = xTaskCreatePinnedToCore(
            controlSystemTaskWrapper,    // Task function
            "ControlSystemTask",         // Task name
            4096,                       // Stack size (4KB - reduced from 8KB)
            this,                       // Parameter passed to task
            1,                          // Priority (reduced from 2 to 1)
            &_control_task_handle,      // Task handle
            1                           // Core 1 (separate from WiFi/main loop on core 0)
        );
        
        if (result != pdPASS) {
            Serial.println("ERROR: Failed to create control system task!");
            _control_system_running = false;
            _current_mode = "none";  // Reset mode on failure
        } else {
            Serial.println("Control system task created successfully");
        }
    }
}

// Stop the control system task
void DriverControl::stopControlSystemTask() {
    if (_control_task_handle != NULL) {
        Serial.println("Stopping control system task...");
        _control_system_running = false;
        _current_mode = "none";  // Reset mode when stopping
        
        // Wait for the task to actually terminate (with timeout)
        int timeout_ms = 500;
        int check_interval_ms = 10;
        int elapsed_ms = 0;
        
        while (eTaskGetState(_control_task_handle) != eDeleted && elapsed_ms < timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
            elapsed_ms += check_interval_ms;
        }
        
        if (eTaskGetState(_control_task_handle) == eDeleted) {
            Serial.println("Control system task terminated successfully");
        } else {
            Serial.println("WARNING: Control system task did not terminate within timeout, forcing deletion");
            vTaskDelete(_control_task_handle);
        }
        
        _control_task_handle = NULL;
        Serial.println("Control system task stopped");
    } else {
        Serial.println("Control system task was not running");
    }
}

// Send buffered data via MQTT (called from main loop every 500ms)
void DriverControl::sendBufferedData() {
    Serial.printf("sendBufferedData called: buffer_count=%d, control_running=%d\n", _buffer_count, _control_system_running);
    
    // Send data if we have at least 5 samples (to avoid sending tiny batches)
    if (_buffer_count < 5) return;
    
    // Take mutex to safely read buffer
    if (xSemaphoreTake(_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // ArduinoJson 7 automatically manages memory allocation
        JsonDocument doc;
        doc["type"] = "data";
        doc["mode"] = "control_system";
        
        JsonObject payload = doc["payload"].to<JsonObject>();
        payload["sample_count"] = _buffer_count;
        payload["frequency_hz"] = CONTROL_SYSTEM_FREQUENCY_HZ;
        payload["continuous"] = true;
        
        // Create arrays for batch data using modern ArduinoJson 7 syntax
        JsonArray timestamps = payload["timestamps"].to<JsonArray>();
        JsonArray inputs = payload["inputs"].to<JsonArray>();
        JsonArray states_x1 = payload["states_x1"].to<JsonArray>();
        JsonArray states_x2 = payload["states_x2"].to<JsonArray>();
        JsonArray outputs_y1 = payload["outputs_y1"].to<JsonArray>();
        JsonArray outputs_y2 = payload["outputs_y2"].to<JsonArray>();
        
        // Copy data from buffer
        int read_start = (_buffer_write_index - _buffer_count + CONTROL_SYSTEM_BUFFER_SIZE) % CONTROL_SYSTEM_BUFFER_SIZE;
        
        for (int i = 0; i < _buffer_count; i++) {
            int index = (read_start + i) % CONTROL_SYSTEM_BUFFER_SIZE;
            
            timestamps.add(_data_buffer[index].timestamp);
            inputs.add(roundTo3Decimals(_data_buffer[index].input_value));
            states_x1.add(roundTo3Decimals(_data_buffer[index].state_x1));
            states_x2.add(roundTo3Decimals(_data_buffer[index].state_x2));
            outputs_y1.add(roundTo3Decimals(_data_buffer[index].output_y1));
            outputs_y2.add(roundTo3Decimals(_data_buffer[index].output_y2));
        }
        
        // Reset buffer
        _buffer_count = 0;
        _buffer_write_index = 0;
        
        xSemaphoreGive(_data_mutex);
        
        // Debug: Check JSON size using measureJson (more memory efficient)
        size_t json_size = measureJson(doc);
        Serial.printf("JSON size: %d bytes, sending %d samples\n", json_size, timestamps.size());
        
        // Send data directly (no String conversion needed)
        _postman.publish("data", doc);
        
        Serial.printf("Sent %d control system samples\n", timestamps.size());
    }
}

// Status reporting
const char* DriverControl::getCurrentMode() const {
    return _current_mode.c_str();
}
