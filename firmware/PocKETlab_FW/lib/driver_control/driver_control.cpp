#include "driver_control.h"
#include <Arduino.h>
#include <math.h>

// Basic constructor
DriverControl::DriverControl(PostmanMQTT& postman, PocKETlabIO& io) : _postman(postman), _io(io), 
    _testbed_running(false), _control_system_running(false), _va_running(false),
    _bode_running(false), _step_running(false), _impulse_running(false) {
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
    
    // Initialize Bode measurement
    _bode_buffer_count = 0;
    _bode_last_measurement = 0;
    _bode_measurement_delay_ms = 50;  // 50ms between frequency measurements
    
    // Initialize Step measurement
    _step_buffer_count = 0;
    _step_last_measurement = 0;
    
    // Initialize Impulse measurement
    _impulse_buffer_count = 0;
    _impulse_last_measurement = 0;
    
    // Initialize current mode tracking
    _current_mode = "none";
    for (int i = 0; i < 4; ++i) { _testbed_da_value_v[i] = NAN; _testbed_db_value_v[i] = NAN; }
}

// Basic destructor
DriverControl::~DriverControl() {
    // Stop control system task if running
    stopControlSystemTask();
    
    // Stop VA measurement if running
    stopVAMeasurement();
    
    // Stop Bode measurement if running
    stopBodeMeasurement();
    
    // Stop Step measurement if running
    stopStepMeasurement();
    
    // Stop Impulse measurement if running
    stopImpulseMeasurement();
    
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

            // DA/DB channels: publish voltages only (0 or 3.3 for digital; 0..3.3 for analog)
            JsonArray daArr = readings["da"].to<JsonArray>();
            for (uint8_t i = 0; i < 4; ++i) {
                float v = _testbed_da_value_v[i];
                if (isnan(v)) {
                    // If not explicitly set, read analog voltage from pin
                    v = _io.analogReadDA(i);
                }
                daArr.add(roundTo3Decimals(v));
            }

            JsonArray dbArr = readings["db"].to<JsonArray>();
            for (uint8_t i = 0; i < 4; ++i) {
                float v = _testbed_db_value_v[i];
                if (isnan(v)) {
                    // Fallback to digital read mapped to 0/3.3V
                    int pin = (i==0?PIN_DB0:i==1?PIN_DB1:i==2?PIN_DB2:PIN_DB3);
                    v = (digitalRead(pin) == HIGH) ? 3.3f : 0.0f;
                }
                dbArr.add(roundTo3Decimals(v));
            }

            payload["status"] = "regulating"; // Placeholder
            payload["continuous"] = true;

            _postman.publish("data", doc);
        }
    }
    
    // Handle buffered data sending for control system (every 200ms to match buffer fill rate)
    if (_control_system_running) {
        // Buffer fills every 200ms at 100Hz with 20 samples (20 / 100Hz = 0.2s)
        if (millis() - _last_data_send >= 200) {
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
    
    // Handle Bode characteristics measurement
    if (_bode_running) {
        if (millis() - _bode_last_measurement >= _bode_measurement_delay_ms) {
            performBodeMeasurement();
            _bode_last_measurement = millis();
        }
    }
    
    // Handle Step response measurement
    if (_step_running) {
        performStepMeasurement();
    }
    
    // Handle Impulse response measurement
    if (_impulse_running) {
        performImpulseMeasurement();
    }
}

void DriverControl::handleVA(JsonObjectConst settings) {
    Serial.println("Handling VA Characteristics command");
    
    // Stop any existing VA measurement
    stopVAMeasurement();
    
    // Parse settings - support both API spec format and legacy flat format
    String channel;
    String mode_type;
    
    // Try API spec format first (nested structure)
    if (settings["channel"].is<const char*>()) {
        channel = settings["channel"].as<String>();
    } else if (settings["va_channel"].is<const char*>()) {
        // Legacy flat format
        channel = settings["va_channel"].as<String>();
    } else {
        _postman.sendError("E001", "Missing channel parameter", "va", "channel", "", "Provide CH0, CH1, or CH2");
        return;
    }
    
    if (settings["mode_type"].is<const char*>()) {
        mode_type = settings["mode_type"].as<String>();
    } else if (settings["va_mode_type"].is<const char*>()) {
        // Legacy flat format
        mode_type = settings["va_mode_type"].as<String>();
    } else {
        _postman.sendError("E001", "Missing mode_type parameter", "va", "mode_type", "", "Provide CV or CC");
        return;
    }
    
    // Validate channel and mode combination
    if (!isValidVAChannel(channel, mode_type)) {
        _postman.sendError("E007", "Channel conflict or invalid mode", "va", "channel/mode_type", 
                          (channel + "/" + mode_type).c_str(), "Valid channels: CH0, CH1, CH2; Valid modes: CV, CC");
        return;
    }
    
    // Configure measurement
    _va_config.channel = channel;
    _va_config.mode_type = mode_type;
    
    // Parse shunt resistance (required for current calculation)
    if (settings["shunt_resistance"].is<float>()) {
        _va_config.shunt_resistance = settings["shunt_resistance"].as<float>();
    } else {
        _va_config.shunt_resistance = 1.0f;  // Default 1 Ohm if not specified
    }
    
    // Validate shunt resistance
    if (_va_config.shunt_resistance <= 0) {
        _postman.sendError("E001", "Invalid shunt resistance", "va", "shunt_resistance", "", 
                          "Shunt resistance must be > 0 Ohms");
        return;
    }
    
    // Initialize CC mode output voltage
    _va_config.cc_output_voltage = 0.0f;
    _va_config.output_voltage = 0.0f;  // Start from 0V output
    _va_config.capped = false;
    
    // Set max output voltage based on channel
    if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
        _va_config.max_output_voltage = _io.getSignalVoltageRange();  // ~13.7V
    } else {
        _va_config.max_output_voltage = _io.getPowerVoltageRange();   // ~13.5V
    }
    
    // Parse voltage/current settings - support both nested and flat formats
    if (mode_type == "CV") {
        // Try nested cv_settings first (API spec format)
        if (settings["cv_settings"].is<JsonObjectConst>()) {
            JsonObjectConst cv = settings["cv_settings"].as<JsonObjectConst>();
            _va_config.start_voltage = cv["start_voltage"].as<float>();
            _va_config.end_voltage = cv["end_voltage"].as<float>();
            _va_config.step_voltage = cv["step_voltage"].as<float>();
        } else {
            // Legacy flat format
            _va_config.start_voltage = settings["va_start_voltage"].as<float>();
            _va_config.end_voltage = settings["va_end_voltage"].as<float>();
            _va_config.step_voltage = settings["va_step_voltage"].as<float>();
        }
        
        // Validate CV parameters
        if (_va_config.start_voltage < 0 || _va_config.end_voltage > 20 || 
            _va_config.start_voltage >= _va_config.end_voltage || _va_config.step_voltage <= 0) {
            _postman.sendError("E001", "Voltage parameter out of range", "va", "voltage", "", 
                              "Voltage must be 0-20V, start < end, step > 0");
            return;
        }
        // Calculate total steps for CV mode
        _va_config.total_steps = (int)(((_va_config.end_voltage - _va_config.start_voltage) / _va_config.step_voltage) + 1);
        
    } else if (mode_type == "CC") {
        // Try nested cc_settings first (API spec format)
        if (settings["cc_settings"].is<JsonObjectConst>()) {
            JsonObjectConst cc = settings["cc_settings"].as<JsonObjectConst>();
            _va_config.start_current = cc["start_current"].as<float>();
            _va_config.end_current = cc["end_current"].as<float>();
            _va_config.step_current = cc["step_current"].as<float>();
        } else {
            // Legacy flat format
            _va_config.start_current = settings["va_start_current"].as<float>();
            _va_config.end_current = settings["va_end_current"].as<float>();
            _va_config.step_current = settings["va_step_current"].as<float>();
        }
        
        // Validate CC parameters
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
    
    // Stop any existing Bode measurement
    stopBodeMeasurement();
    
    _current_mode = "bode";
    
    // Parse settings according to API spec
    String channel;
    if (settings["channel"].is<const char*>()) {
        channel = settings["channel"].as<String>();
    } else {
        _postman.sendError("E001", "Missing channel parameter", "bode", "channel", "", "Provide CH0, CH1, or CH2");
        return;
    }
    
    // Parse frequency range
    JsonObjectConst freq_range = settings["frequency_range"].as<JsonObjectConst>();
    if (freq_range.isNull()) {
        _postman.sendError("E001", "Missing frequency_range parameter", "bode", "frequency_range", "", 
                          "Provide from, to, and points_per_decade");
        return;
    }
    
    float freq_from = freq_range["from"].as<float>();
    float freq_to = freq_range["to"].as<float>();
    int points_per_decade = freq_range["points_per_decade"].as<int>();
    float output_voltage = settings["output_voltage"].as<float>();
    
    // Validate parameters according to API spec constraints
    if (freq_from < 1 || freq_to > 10000 || freq_from >= freq_to) {
        _postman.sendError("E001", "Frequency range out of bounds", "bode", "frequency_range", "", 
                          "Frequency must be 1Hz to 10kHz, from < to");
        return;
    }
    
    if (output_voltage < 0.1 || output_voltage > 20) {
        _postman.sendError("E001", "Output voltage out of range", "bode", "output_voltage", "", 
                          "Output voltage must be 0.1V to 20V");
        return;
    }
    
    if (points_per_decade < 1 || points_per_decade > 100) {
        _postman.sendError("E001", "Points per decade out of range", "bode", "points_per_decade", "", 
                          "Points per decade must be 1 to 100");
        return;
    }
    
    // Configure Bode measurement
    _bode_config.channel = channel;
    _bode_config.freq_from = freq_from;
    _bode_config.freq_to = freq_to;
    _bode_config.points_per_decade = points_per_decade;
    _bode_config.output_voltage = output_voltage;
    _bode_config.total_points = calculateTotalBodePoints();
    _bode_config.current_point = 0;
    
    // Validate total points (max 500 per API spec)
    if (_bode_config.total_points > 500) {
        _postman.sendError("E006", "Too many measurement points", "bode", "points", "", 
                          "Maximum 500 measurement points allowed");
        return;
    }
    
    // Initialize measurement state
    _bode_buffer_count = 0;
    _bode_running = true;
    _bode_last_measurement = millis();
    
    // Calculate estimated duration
    int estimated_duration = (_bode_config.total_points * _bode_measurement_delay_ms) / 1000 + 5;
    
    // Send success response
    _postman.sendResponse("bode", "success", "Bode measurement started", estimated_duration);
    
    Serial.printf("Bode measurement started: %s, %.1fHz-%.1fHz, %d points/decade, %d total points\n", 
                  channel.c_str(), freq_from, freq_to, points_per_decade, _bode_config.total_points);
}

void DriverControl::handleStep(JsonObjectConst settings) {
    Serial.println("Handling Step Response command");
    
    // Stop any existing Step measurement
    stopStepMeasurement();
    
    _current_mode = "step";

    // Parse settings according to API spec
    String channel;
    if (settings["channel"].is<const char*>()) {
        channel = settings["channel"].as<String>();
    } else {
        _postman.sendError("E001", "Missing channel parameter", "step", "channel", "", "Provide CH0, CH1, or CH2");
        return;
    }
    
    float voltage = settings["voltage"].as<float>();
    float measurement_time = settings["measurement_time"].as<float>();

    Serial.printf("Channel: %s, Voltage: %.2fV, Time: %.3fs\n", channel.c_str(), voltage, measurement_time);

    // Validate parameters according to API spec constraints
    if (voltage < 0 || voltage > 20) {
        _postman.sendError("E001", "Voltage out of range", "step", "voltage", "", 
                          "Voltage must be 0V to 20V");
        return;
    }
    
    if (measurement_time < 0.001 || measurement_time > 10) {
        _postman.sendError("E001", "Measurement time out of range", "step", "measurement_time", "", 
                          "Measurement time must be 0.001s to 10s");
        return;
    }

    // Configure Step measurement
    _step_config.channel = channel;
    _step_config.voltage = voltage;
    _step_config.measurement_time = measurement_time;
    _step_config.total_points = STEP_DATA_POINTS;  // Fixed 200 points per API spec
    _step_config.current_point = 0;
    _step_config.time_step = measurement_time / (STEP_DATA_POINTS - 1);  // Time between samples
    _step_config.start_time = 0;  // Will be set when measurement starts
    
    // Initialize measurement state
    _step_buffer_count = 0;
    _step_running = true;
    _step_last_measurement = 0;
    
    // Calculate estimated duration
    int estimated_duration = (int)(measurement_time + 2);  // measurement time + overhead
    
    // Send success response
    _postman.sendResponse("step", "success", "Step measurement started", estimated_duration);

    Serial.printf("Step measurement started: %s, %.2fV, %.3fs, %d points\n", 
                  channel.c_str(), voltage, measurement_time, _step_config.total_points);
}

void DriverControl::handleImpulse(JsonObjectConst settings) {
    Serial.println("Handling Impulse Response command");
    
    // Stop any existing Impulse measurement
    stopImpulseMeasurement();
    
    _current_mode = "impulse";

    // Parse settings according to API spec
    float voltage = settings["voltage"].as<float>();
    int duration_us = settings["duration_us"].as<int>();
    float measurement_time = settings["measurement_time"].as<float>();

    Serial.printf("Voltage: %.2fV, Duration: %dus, Time: %.3fs\n", voltage, duration_us, measurement_time);

    // Validate parameters according to API spec constraints
    if (voltage < 0 || voltage > 20) {
        _postman.sendError("E001", "Impulse voltage out of range", "impulse", "voltage", "", 
                          "Voltage must be 0V to 20V");
        return;
    }
    
    if (duration_us < 1 || duration_us > 1000) {
        _postman.sendError("E001", "Impulse duration out of range", "impulse", "duration_us", "", 
                          "Duration must be 1μs to 1000μs");
        return;
    }
    
    if (measurement_time < 0.001 || measurement_time > 2) {
        _postman.sendError("E001", "Measurement time out of range", "impulse", "measurement_time", "", 
                          "Measurement time must be 0.001s to 2s");
        return;
    }

    // Configure Impulse measurement
    _impulse_config.voltage = voltage;
    _impulse_config.duration_us = duration_us;
    _impulse_config.measurement_time = measurement_time;
    _impulse_config.total_points = IMPULSE_DATA_POINTS;  // Fixed 200 points
    _impulse_config.current_point = 0;
    _impulse_config.time_step = measurement_time / (IMPULSE_DATA_POINTS - 1);
    _impulse_config.start_time = 0;  // Will be set when measurement starts
    _impulse_config.impulse_applied = false;
    
    // Initialize measurement state
    _impulse_buffer_count = 0;
    _impulse_running = true;
    _impulse_last_measurement = 0;
    
    // Calculate estimated duration
    int estimated_duration = (int)(measurement_time + 1);  // measurement time + overhead
    
    // Send success response
    _postman.sendResponse("impulse", "success", "Impulse measurement started", estimated_duration);

    Serial.printf("Impulse measurement started: %.2fV, %dus, %.3fs, %d points\n", 
                  voltage, duration_us, measurement_time, _impulse_config.total_points);
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

    // Apply power settings
    _io.setPowerVoltage(target_voltage);
    _io.setPowerCurrent(current_limit);
    _io.updateAllDACs();

    // Optional Signal DAC outputs (channels 0 and 1) in volts after amplifier
    // Accepts either object: {"ch0": <V>, "ch1": <V>} or array: [<V0>, <V1>]
    if (settings["signal"].is<JsonObjectConst>()) {
        JsonObjectConst sig = settings["signal"].as<JsonObjectConst>();
        bool ok = true;
        if (sig["ch0"].is<float>()) {
            ok &= _io.setSignalVoltage(SIGNAL_CHANNEL_A, sig["ch0"].as<float>());
        }
        if (sig["ch1"].is<float>()) {
            ok &= _io.setSignalVoltage(SIGNAL_CHANNEL_B, sig["ch1"].as<float>());
        }
        _io.updateAllDACs();
        if (!ok) {
            String rangeStr = String("0-") + String(_io.getSignalVoltageRange(), 2) + "V";
            _postman.sendError("E001", "Signal voltage out of range", "testbed", "signal", "", rangeStr.c_str());
            return;
        }
    } else if (settings["signal"].is<JsonArrayConst>()) {
        JsonArrayConst sig = settings["signal"].as<JsonArrayConst>();
        bool ok = true;
        if (sig.size() > 0 && sig[0].is<float>()) {
            ok &= _io.setSignalVoltage(SIGNAL_CHANNEL_A, sig[0].as<float>());
        }
        if (sig.size() > 1 && sig[1].is<float>()) {
            ok &= _io.setSignalVoltage(SIGNAL_CHANNEL_B, sig[1].as<float>());
        }
        _io.updateAllDACs();
        if (!ok) {
            String rangeStr = String("0-") + String(_io.getSignalVoltageRange(), 2) + "V";
            _postman.sendError("E001", "Signal voltage out of range", "testbed", "signal", "", rangeStr.c_str());
            return;
        }
    }

    // Optional DA per-pin configuration/output
    if (settings["da"].is<JsonArrayConst>()) {
        JsonArrayConst daCfg = settings["da"].as<JsonArrayConst>();
        for (uint8_t i = 0; i < daCfg.size() && i < 4; ++i) {
            JsonObjectConst ch = daCfg[i];
            const char* mode = ch["mode"].as<const char*>(); // "digital"|"analog"
            float value_v = NAN;
            if (ch["value"].is<float>()) value_v = ch["value"].as<float>();
            // Legacy support: level 0/1
            if (isnan(value_v) && ch["level"].is<int>()) value_v = (ch["level"].as<int>() != 0) ? 3.3f : 0.0f;
            if (mode && strcmp(mode, "analog") == 0) {
                if (isnan(value_v)) value_v = 0.0f;
                _io.analogWriteDAVoltage(i, value_v);
                _testbed_da_value_v[i] = value_v;
            } else if (mode && strcmp(mode, "digital") == 0) {
                bool high = (!isnan(value_v) && value_v >= 1.65f);
                _io.digitalWriteDA(i, high);
                _testbed_da_value_v[i] = high ? 3.3f : 0.0f;
            }
        }
    }

    // Optional DB per-pin configuration/output
    if (settings["db"].is<JsonArrayConst>()) {
        JsonArrayConst dbCfg = settings["db"].as<JsonArrayConst>();
        for (uint8_t i = 0; i < dbCfg.size() && i < 4; ++i) {
            JsonObjectConst ch = dbCfg[i];
            const char* mode = ch["mode"].as<const char*>(); // "digital"|"analog"
            float value_v = NAN;
            if (ch["value"].is<float>()) value_v = ch["value"].as<float>();
            if (isnan(value_v) && ch["level"].is<int>()) value_v = (ch["level"].as<int>() != 0) ? 3.3f : 0.0f;
            if (mode && strcmp(mode, "analog") == 0) {
                if (isnan(value_v)) value_v = 0.0f;
                _io.analogWriteDBVoltage(i, value_v);
                _testbed_db_value_v[i] = value_v;
            } else if (mode && strcmp(mode, "digital") == 0) {
                bool high = (!isnan(value_v) && value_v >= 1.65f);
                _io.digitalWriteDB(i, high);
                _testbed_db_value_v[i] = high ? 3.3f : 0.0f;
            }
        }
    }

    // Start/stop streaming
    _testbed_running = continuous_monitoring;
    _testbed_update_interval = update_interval_ms;
    _testbed_last_update = 0;

    _postman.sendResponse("testbed", "success", "Testbed mode activated");
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
    // Round to 3 decimal places for voltage display
    return round(value * 1000.0) / 1000.0;
}

float DriverControl::roundTo6Decimals(float value) {
    // Round to 6 decimal places for high-precision current measurements
    return round(value * 1000000.0) / 1000000.0;
}

// VA characteristics helper functions

bool DriverControl::isValidVAChannel(const String& channel, const String& mode_type) {
    // All channels support both CV and CC modes
    if ((channel == "CH0" || channel == "CH1" || channel == "CH2") && 
        (mode_type == "CV" || mode_type == "CC")) {
        return true;
    }
    return false;
}

void DriverControl::performVAMeasurement() {
    if (!_va_running || _va_config.current_step >= _va_config.total_steps) {
        stopVAMeasurement();
        return;
    }
    
    float device_voltage, current;
    const int NUM_SAMPLES = 8;  // Increased sampling for better noise averaging
    const int SAMPLE_DELAY_MS = 2;  // Delay between samples
    const float VOLTAGE_STEP_INCREMENT = 0.05f;  // Output voltage increment per iteration
    const int MAX_ITERATIONS = 50;  // Max iterations to reach target device voltage
    const float DEVICE_VOLTAGE_TOLERANCE = 0.02f;  // 20mV tolerance for device voltage
    
    if (_va_config.mode_type == "CV") {
        // Constant Voltage mode: target is device voltage (V_A - V_B)
        float target_device_voltage = _va_config.start_voltage + (_va_config.current_step * _va_config.step_voltage);
        _va_config.target_device_voltage = target_device_voltage;
        
        // Closed-loop: adjust output voltage until device voltage reaches target
        bool target_reached = false;
        bool voltage_capped = false;
        
        for (int iter = 0; iter < MAX_ITERATIONS && !target_reached && !voltage_capped; iter++) {
            // Set the voltage on the appropriate drive channel
            if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
                _io.setSignalVoltage(SIGNAL_CHANNEL_A, _va_config.output_voltage);
            } else if (_va_config.channel == "CH2") {
                _io.setPowerVoltage(_va_config.output_voltage);
            }
            _io.updateAllDACs();
            
            // Wait for voltage to settle
            delay(10);
            
            // Measure device voltage
            float voltage_a = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
            float voltage_b = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
            device_voltage = voltage_a - voltage_b;
            
            // Check if we've reached target device voltage
            if (device_voltage >= target_device_voltage - DEVICE_VOLTAGE_TOLERANCE) {
                target_reached = true;
            } else {
                // Increase output voltage
                _va_config.output_voltage += VOLTAGE_STEP_INCREMENT;
                
                // Check if we've hit the maximum output voltage
                if (_va_config.output_voltage >= _va_config.max_output_voltage) {
                    _va_config.output_voltage = _va_config.max_output_voltage;
                    voltage_capped = true;
                    _va_config.capped = true;
                }
            }
        }
        
        // Apply final output voltage and take averaged measurement
        if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_A, _va_config.output_voltage);
        } else if (_va_config.channel == "CH2") {
            _io.setPowerVoltage(_va_config.output_voltage);
        }
        _io.updateAllDACs();
        delay(20);  // Extra settling time for final measurement
        
        // Multi-sample measurement with RMS calculation for noise reduction
        float voltage_a_sq_sum = 0.0f;
        float voltage_b_sq_sum = 0.0f;
        float power_current_sq_sum = 0.0f;
        
        // Discard first reading (may be noisy after DAC update)
        _io.readSignalVoltage(SIGNAL_CHANNEL_A);
        _io.readSignalVoltage(SIGNAL_CHANNEL_B);
        delay(2);
        
        for (int i = 0; i < NUM_SAMPLES; i++) {
            float va = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
            float vb = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
            voltage_a_sq_sum += va * va;
            voltage_b_sq_sum += vb * vb;
            if (_va_config.channel == "CH2") {
                float pc = _io.readPowerCurrent();
                power_current_sq_sum += pc * pc;
            }
            
            if (i < NUM_SAMPLES - 1) {
                delay(SAMPLE_DELAY_MS);
            }
        }
        
        // Calculate RMS values
        float voltage_a = sqrt(voltage_a_sq_sum / NUM_SAMPLES);
        float voltage_b = sqrt(voltage_b_sq_sum / NUM_SAMPLES);
        
        // Device voltage = V_A - V_B (voltage across the device under test)
        device_voltage = voltage_a - voltage_b;
        
        // Current calculation: I = V_shunt / R_shunt
        if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
            current = voltage_b / _va_config.shunt_resistance;
        } else {
            current = sqrt(power_current_sq_sum / NUM_SAMPLES);  // RMS of power current
        }
        
        // If voltage was capped and we didn't reach target, end measurement early
        if (voltage_capped && device_voltage < target_device_voltage - DEVICE_VOLTAGE_TOLERANCE) {
            Serial.printf("VA measurement capped: output=%.2fV, device=%.3fV, target=%.3fV\n",
                         _va_config.output_voltage, device_voltage, target_device_voltage);
        }
        
    } else if (_va_config.mode_type == "CC") {
        // Constant Current mode: adjust voltage to achieve target current
        float target_current = _va_config.start_current + (_va_config.current_step * _va_config.step_current);
        
        // CC closed-loop control parameters
        const float CC_GAIN = 0.5f;  // Proportional gain for current control
        const int CC_ITERATIONS = 10;  // Max iterations to reach target current
        const float CC_TOLERANCE = 0.01f;  // Current tolerance (1%)
        
        float measured_current = 0.0f;
        
        // Initialize output voltage on first step
        if (_va_config.current_step == 0) {
            _va_config.cc_output_voltage = target_current * _va_config.shunt_resistance;  // Initial estimate
        }
        
        // Closed-loop iteration to achieve target current
        for (int iter = 0; iter < CC_ITERATIONS; iter++) {
            // Set the output voltage
            if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
                _io.setSignalVoltage(SIGNAL_CHANNEL_A, _va_config.cc_output_voltage);
            } else if (_va_config.channel == "CH2") {
                _io.setPowerCurrent(target_current);  // CH2 has direct current control
                _io.updateAllDACs();
                break;  // No need for closed-loop on CH2
            }
            _io.updateAllDACs();
            
            delay(5);  // Short settling time
            
            // Measure current
            float voltage_b = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
            measured_current = voltage_b / _va_config.shunt_resistance;
            
            // Check if we're within tolerance
            float current_error = target_current - measured_current;
            if (abs(current_error) < CC_TOLERANCE * target_current) {
                break;  // Close enough
            }
            
            // Adjust output voltage based on error
            _va_config.cc_output_voltage += current_error * _va_config.shunt_resistance * CC_GAIN;
            
            // Clamp output voltage to valid range
            if (_va_config.cc_output_voltage < 0) _va_config.cc_output_voltage = 0;
            if (_va_config.cc_output_voltage >= _va_config.max_output_voltage) {
                _va_config.cc_output_voltage = _va_config.max_output_voltage;
                _va_config.capped = true;
                break;  // Can't go higher
            }
        }
        
        // Final measurement with RMS calculation
        float voltage_a_sq_sum = 0.0f;
        float voltage_b_sq_sum = 0.0f;
        float power_current_sq_sum = 0.0f;
        
        for (int i = 0; i < NUM_SAMPLES; i++) {
            float va = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
            float vb = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
            voltage_a_sq_sum += va * va;
            voltage_b_sq_sum += vb * vb;
            if (_va_config.channel == "CH2") {
                float pc = _io.readPowerCurrent();
                power_current_sq_sum += pc * pc;
            }
            
            if (i < NUM_SAMPLES - 1) {
                delay(SAMPLE_DELAY_MS);
            }
        }
        
        // Calculate RMS values
        float voltage_a = sqrt(voltage_a_sq_sum / NUM_SAMPLES);
        float voltage_b = sqrt(voltage_b_sq_sum / NUM_SAMPLES);
        
        // Device voltage = V_A - V_B
        device_voltage = voltage_a - voltage_b;
        
        // Current calculation
        if (_va_config.channel == "CH0" || _va_config.channel == "CH1") {
            current = voltage_b / _va_config.shunt_resistance;
        } else {
            current = sqrt(power_current_sq_sum / NUM_SAMPLES);  // RMS of power current
        }
    }
    
    // Calculate progress based on actual device voltage reached vs target range
    float voltage_range = _va_config.end_voltage - _va_config.start_voltage;
    float progress;
    bool completed;
    
    if (_va_config.mode_type == "CV") {
        // For CV mode, progress is based on device voltage achieved
        progress = ((device_voltage - _va_config.start_voltage) / voltage_range) * 100.0f;
        if (progress < 0) progress = 0;
        if (progress > 100) progress = 100;
        
        // Completed if we reached end voltage OR if output is capped
        completed = (device_voltage >= _va_config.end_voltage - 0.02f) || _va_config.capped;
    } else {
        // For CC mode, progress is based on step count
        progress = (float)(_va_config.current_step + 1) / _va_config.total_steps * 100.0f;
        completed = (_va_config.current_step + 1) >= _va_config.total_steps || _va_config.capped;
    }
    
    // Store data point in buffer
    if (_va_buffer_count < VA_BUFFER_SIZE) {
        _va_data_buffer[_va_buffer_count].voltage = device_voltage;
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
    // Use high precision for current to avoid stair-stepping with small currents
    JsonArray data_array = payload["data"].to<JsonArray>();
    for (int i = 0; i < _va_buffer_count; i++) {
        JsonObject data_point = data_array.add<JsonObject>();
        data_point["voltage"] = roundTo6Decimals(_va_data_buffer[i].voltage);
        data_point["current"] = roundTo6Decimals(_va_data_buffer[i].current);
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
        
        // Clear buffer to prevent further data sending
        _va_buffer_count = 0;
        
        // Reset outputs to safe values
        _io.setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
        _io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
        _io.setPowerVoltage(0.0);
        _io.setPowerCurrent(0.0);  // Also reset current limit to 0
 
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
    } else if (strcmp(mode, "bode") == 0) {
        // Stop Bode measurement
        stopBodeMeasurement();
        _postman.sendResponse("bode", "success", "Bode measurement stopped");
        Serial.println("Bode measurement stopped via MQTT command");
    } else if (strcmp(mode, "step") == 0) {
        // Stop Step measurement
        stopStepMeasurement();
        _postman.sendResponse("step", "success", "Step measurement stopped");
        Serial.println("Step measurement stopped via MQTT command");
    } else if (strcmp(mode, "impulse") == 0) {
        // Stop Impulse measurement
        stopImpulseMeasurement();
        _postman.sendResponse("impulse", "success", "Impulse measurement stopped");
        Serial.println("Impulse measurement stopped via MQTT command");
    } else if (strcmp(mode, "testbed") == 0) {
        // Stop testbed mode
        _testbed_running = false;
        _current_mode = "none";
        _postman.sendResponse("testbed", "success", "Testbed mode stopped");
        Serial.println("Testbed mode stopped via MQTT command");
    } else {
        // Unknown mode
        _postman.sendError("E005", "Invalid stop mode", "stop", "mode", mode, "Use 'control_system', 'va', 'bode', 'step', 'impulse', or 'testbed'");
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

// ============================================================================
// Bode characteristics helper functions
// ============================================================================

int DriverControl::calculateTotalBodePoints() {
    // Calculate number of decades
    float decades = log10(_bode_config.freq_to / _bode_config.freq_from);
    return (int)(decades * _bode_config.points_per_decade) + 1;
}

float DriverControl::calculateBodeFrequency(int point_index) {
    // Calculate frequency for logarithmic spacing
    float decades = log10(_bode_config.freq_to / _bode_config.freq_from);
    float fraction = (float)point_index / (_bode_config.total_points - 1);
    return _bode_config.freq_from * pow(10, fraction * decades);
}

void DriverControl::performBodeMeasurement() {
    if (!_bode_running || _bode_config.current_point >= _bode_config.total_points) {
        stopBodeMeasurement();
        return;
    }
    
    // Calculate current frequency
    float frequency = calculateBodeFrequency(_bode_config.current_point);
    
    // Set the output signal at the current frequency
    // For actual implementation, this would generate a sine wave at this frequency
    // and measure the input/output amplitude and phase difference
    
    // Set output voltage amplitude on appropriate channel
    if (_bode_config.channel == "CH0") {
        _io.setSignalVoltage(SIGNAL_CHANNEL_A, _bode_config.output_voltage);
    } else if (_bode_config.channel == "CH1") {
        _io.setSignalVoltage(SIGNAL_CHANNEL_B, _bode_config.output_voltage);
    } else if (_bode_config.channel == "CH2") {
        _io.setPowerVoltage(_bode_config.output_voltage);
    }
    _io.updateAllDACs();
    
    // Wait for system to settle at this frequency
    // In a real implementation, we'd generate a sine wave and perform FFT or
    // synchronous detection to measure gain and phase
    delay(10);
    
    // Measure response - placeholder implementation
    // In real implementation: apply sine wave, measure output, calculate gain/phase
    float input_amplitude = _bode_config.output_voltage;
    float output_amplitude;
    
    if (_bode_config.channel == "CH0") {
        output_amplitude = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
    } else if (_bode_config.channel == "CH1") {
        output_amplitude = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
    } else {
        output_amplitude = _io.readPowerVoltage();
    }
    
    // Calculate gain in dB (simplified - real implementation needs proper signal analysis)
    float gain_db = 20.0 * log10(output_amplitude / input_amplitude + 0.001);  // Add small value to avoid log(0)
    
    // Phase calculation would require time-domain analysis or synchronous detection
    // For now, use a placeholder that simulates typical first-order system behavior
    float phase_deg = -atan(frequency / 100.0) * 180.0 / M_PI;  // Placeholder phase
    
    // Store data point in buffer
    if (_bode_buffer_count < BODE_BUFFER_SIZE) {
        _bode_data_buffer[_bode_buffer_count].frequency = frequency;
        _bode_data_buffer[_bode_buffer_count].gain = gain_db;
        _bode_data_buffer[_bode_buffer_count].phase = phase_deg;
        _bode_buffer_count++;
    }
    
    // Calculate progress
    float progress = (float)(_bode_config.current_point + 1) / _bode_config.total_points * 100.0f;
    bool completed = (_bode_config.current_point + 1) >= _bode_config.total_points;
    
    // Send buffered data if buffer is full or measurement is completed
    if (_bode_buffer_count >= BODE_BUFFER_SIZE || completed) {
        sendBufferedBodeData(completed);
    }
    
    // Move to next frequency point
    _bode_config.current_point++;
    
    if (completed) {
        Serial.println("Bode measurement completed");
        _bode_running = false;
        _current_mode = "none";
    }
}

void DriverControl::sendBufferedBodeData(bool completed) {
    if (_bode_buffer_count == 0) return;
    
    JsonDocument doc;
    
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());
    
    doc["timestamp"] = timestamp;
    doc["message_id"] = "bode-data-" + String(millis());
    doc["type"] = "data";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = "bode";
    
    // Create data array with all buffered measurement points
    JsonArray data_array = payload["data"].to<JsonArray>();
    for (int i = 0; i < _bode_buffer_count; i++) {
        JsonObject data_point = data_array.add<JsonObject>();
        data_point["frequency"] = roundTo3Decimals(_bode_data_buffer[i].frequency);
        data_point["gain"] = roundTo3Decimals(_bode_data_buffer[i].gain);
        data_point["phase"] = roundTo3Decimals(_bode_data_buffer[i].phase);
    }
    
    float progress = (float)(_bode_config.current_point + 1) / _bode_config.total_points * 100.0f;
    payload["progress"] = roundTo3Decimals(progress);
    payload["completed"] = completed;
    
    _postman.publish("data", doc);
    
    Serial.printf("Bode buffered data sent: %d points, Progress=%.1f%%, Completed=%s\n", 
                  _bode_buffer_count, progress, completed ? "true" : "false");
    
    // Reset buffer
    _bode_buffer_count = 0;
}

void DriverControl::stopBodeMeasurement() {
    if (_bode_running) {
        // Send any remaining buffered data
        if (_bode_buffer_count > 0) {
            sendBufferedBodeData(true);
        }
        
        _bode_running = false;
        _current_mode = "none";
        _bode_buffer_count = 0;
        
        // Reset outputs to safe values
        _io.setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
        _io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
        _io.setPowerVoltage(0.0);
        _io.updateAllDACs();
        
        Serial.println("Bode measurement stopped and outputs reset");
    }
}

// ============================================================================
// Step response helper functions
// ============================================================================

void DriverControl::performStepMeasurement() {
    // Initialize measurement on first call
    if (_step_config.start_time == 0) {
        _step_config.start_time = micros();
        
        // Apply step voltage to channel
        if (_step_config.channel == "CH0") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_A, _step_config.voltage);
        } else if (_step_config.channel == "CH1") {
            _io.setSignalVoltage(SIGNAL_CHANNEL_B, _step_config.voltage);
        } else if (_step_config.channel == "CH2") {
            _io.setPowerVoltage(_step_config.voltage);
        }
        _io.updateAllDACs();
        
        Serial.println("Step voltage applied");
    }
    
    // Calculate elapsed time in seconds
    unsigned long elapsed_us = micros() - _step_config.start_time;
    float elapsed_s = elapsed_us / 1000000.0f;
    
    // Check if it's time for next sample
    float expected_time = _step_config.current_point * _step_config.time_step;
    
    if (elapsed_s >= expected_time && _step_config.current_point < _step_config.total_points) {
        // Read response from the channel
        float response;
        if (_step_config.channel == "CH0") {
            response = _io.readSignalVoltage(SIGNAL_CHANNEL_A);
        } else if (_step_config.channel == "CH1") {
            response = _io.readSignalVoltage(SIGNAL_CHANNEL_B);
        } else {
            response = _io.readPowerVoltage();
        }
        
        // Store data point
        if (_step_buffer_count < STEP_DATA_POINTS) {
            _step_data_buffer[_step_buffer_count].time = elapsed_s;
            _step_data_buffer[_step_buffer_count].response = response;
            _step_buffer_count++;
        }
        
        _step_config.current_point++;
        
        // Check if measurement is complete
        if (_step_config.current_point >= _step_config.total_points) {
            sendBufferedStepData(true);
            stopStepMeasurement();
            return;
        }
        
        // Send partial data every 50 points
        if (_step_buffer_count >= 50) {
            float progress = (float)_step_config.current_point / _step_config.total_points * 100.0f;
            sendBufferedStepData(false);
        }
    }
}

void DriverControl::sendBufferedStepData(bool completed) {
    if (_step_buffer_count == 0) return;
    
    JsonDocument doc;
    
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());
    
    doc["timestamp"] = timestamp;
    doc["message_id"] = "step-data-" + String(millis());
    doc["type"] = "data";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = "step";
    
    // Create data array with all buffered measurement points
    JsonArray data_array = payload["data"].to<JsonArray>();
    for (int i = 0; i < _step_buffer_count; i++) {
        JsonObject data_point = data_array.add<JsonObject>();
        data_point["time"] = _step_data_buffer[i].time;  // Keep full precision for time
        data_point["response"] = roundTo3Decimals(_step_data_buffer[i].response);
    }
    
    float progress = (float)_step_config.current_point / _step_config.total_points * 100.0f;
    payload["progress"] = roundTo3Decimals(progress);
    payload["completed"] = completed;
    
    _postman.publish("data", doc);
    
    Serial.printf("Step buffered data sent: %d points, Progress=%.1f%%, Completed=%s\n", 
                  _step_buffer_count, progress, completed ? "true" : "false");
    
    // Reset buffer
    _step_buffer_count = 0;
}

void DriverControl::stopStepMeasurement() {
    if (_step_running) {
        // Send any remaining buffered data
        if (_step_buffer_count > 0) {
            sendBufferedStepData(true);
        }
        
        _step_running = false;
        _current_mode = "none";
        _step_buffer_count = 0;
        _step_config.start_time = 0;
        
        // Reset outputs to safe values
        _io.setSignalVoltage(SIGNAL_CHANNEL_A, 0.0);
        _io.setSignalVoltage(SIGNAL_CHANNEL_B, 0.0);
        _io.setPowerVoltage(0.0);
        _io.updateAllDACs();
        
        Serial.println("Step measurement stopped and outputs reset");
    }
}

// ============================================================================
// Impulse response helper functions
// ============================================================================

void DriverControl::performImpulseMeasurement() {
    // Initialize measurement on first call
    if (_impulse_config.start_time == 0) {
        _impulse_config.start_time = micros();
        
        // Apply impulse voltage to CH2 (power channel)
        _io.setPowerVoltage(_impulse_config.voltage);
        _io.updateAllDACs();
        
        // Wait for impulse duration
        delayMicroseconds(_impulse_config.duration_us);
        
        // Remove impulse (set to 0V)
        _io.setPowerVoltage(0.0);
        _io.updateAllDACs();
        
        _impulse_config.impulse_applied = true;
        Serial.printf("Impulse applied: %.2fV for %dus\n", 
                      _impulse_config.voltage, _impulse_config.duration_us);
    }
    
    // Calculate elapsed time in seconds
    unsigned long elapsed_us = micros() - _impulse_config.start_time;
    float elapsed_s = elapsed_us / 1000000.0f;
    
    // Check if it's time for next sample
    float expected_time = _impulse_config.current_point * _impulse_config.time_step;
    
    if (elapsed_s >= expected_time && _impulse_config.current_point < _impulse_config.total_points) {
        // Read response from power channel
        float response = _io.readPowerVoltage();
        
        // Store data point
        if (_impulse_buffer_count < IMPULSE_DATA_POINTS) {
            _impulse_data_buffer[_impulse_buffer_count].time = elapsed_s;
            _impulse_data_buffer[_impulse_buffer_count].response = response;
            _impulse_buffer_count++;
        }
        
        _impulse_config.current_point++;
        
        // Check if measurement is complete
        if (_impulse_config.current_point >= _impulse_config.total_points) {
            sendBufferedImpulseData(true);
            stopImpulseMeasurement();
            return;
        }
        
        // Send partial data every 50 points
        if (_impulse_buffer_count >= 50) {
            sendBufferedImpulseData(false);
        }
    }
}

void DriverControl::sendBufferedImpulseData(bool completed) {
    if (_impulse_buffer_count == 0) return;
    
    JsonDocument doc;
    
    char timestamp[30];
    snprintf(timestamp, sizeof(timestamp), "%lu", millis());
    
    doc["timestamp"] = timestamp;
    doc["message_id"] = "impulse-data-" + String(millis());
    doc["type"] = "data";
    
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["mode"] = "impulse";
    
    // Create data array with all buffered measurement points
    JsonArray data_array = payload["data"].to<JsonArray>();
    for (int i = 0; i < _impulse_buffer_count; i++) {
        JsonObject data_point = data_array.add<JsonObject>();
        data_point["time"] = _impulse_data_buffer[i].time;  // Keep full precision for time
        data_point["response"] = roundTo3Decimals(_impulse_data_buffer[i].response);
    }
    
    float progress = (float)_impulse_config.current_point / _impulse_config.total_points * 100.0f;
    payload["progress"] = roundTo3Decimals(progress);
    payload["completed"] = completed;
    
    _postman.publish("data", doc);
    
    Serial.printf("Impulse buffered data sent: %d points, Progress=%.1f%%, Completed=%s\n", 
                  _impulse_buffer_count, progress, completed ? "true" : "false");
    
    // Reset buffer
    _impulse_buffer_count = 0;
}

void DriverControl::stopImpulseMeasurement() {
    if (_impulse_running) {
        // Send any remaining buffered data
        if (_impulse_buffer_count > 0) {
            sendBufferedImpulseData(true);
        }
        
        _impulse_running = false;
        _current_mode = "none";
        _impulse_buffer_count = 0;
        _impulse_config.start_time = 0;
        _impulse_config.impulse_applied = false;
        
        // Reset outputs to safe values
        _io.setPowerVoltage(0.0);
        _io.updateAllDACs();
        
        Serial.println("Impulse measurement stopped and outputs reset");
    }
}

// Status reporting
const char* DriverControl::getCurrentMode() const {
    return _current_mode.c_str();
}
