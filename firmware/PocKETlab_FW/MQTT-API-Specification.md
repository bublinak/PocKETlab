# MQTT API Specification for PocKETlab

## Overview

This document specifies the MQTT communication protocol between the PocKETlab webapp and the measuring board. The API supports six measurement modes with various settings and real-time data exchange.

## MQTT Topics Structure

```
pocketlab/
├── command/          # Commands from webapp to measuring board
├── response/         # Responses from measuring board to webapp
├── data/            # Streaming measurement data
└── status/          # Status updates and error messages
```

## Message Format

All MQTT messages use JSON format with the following base structure:

```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "uuid-string",
  "type": "command|response|data|status",
  "payload": { /* mode-specific content */ }
}
```

## Measurement Modes

### 1. VA Characteristics Mode

**Purpose:** Voltage-current characteristic measurements with CV (Constant Voltage) or CC (Constant Current) control.

#### Command Message
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "12345678-1234-5678-9abc-123456789012",
  "type": "command",
  "payload": {
    "mode": "va",
    "settings": {
      "channel": "CH0|CH1|CH2",
      "mode_type": "CV|CC",
      "cv_settings": {
        "start_voltage": 0.0,
        "end_voltage": 5.0,
        "step_voltage": 0.1
      },
      "cc_settings": {
        "start_current": 0.0,
        "end_current": 1.0,
        "step_current": 0.01
      }
    }
  }
}
```

#### Response Message
```json
{
  "timestamp": "2024-01-15T10:30:01Z",
  "message_id": "87654321-4321-8765-cba9-876543210987",
  "type": "response",
  "payload": {
    "mode": "va",
    "status": "success|error",
    "message": "Measurement started|Error description",
    "estimated_duration": 120
  }
}
```

#### Data Stream Message
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "data-uuid-1",
  "type": "data",
  "payload": {
    "mode": "va",
    "data": [
      {"voltage": 0.0, "current": 0.001},
      {"voltage": 0.1, "current": 0.011},
      {"voltage": 0.2, "current": 0.021}
    ],
    "progress": 15.5,
    "completed": false
  }
}
```

**Channel Restrictions:**
- CH0, CH1: CV mode only
- CH2: CV and CC modes supported

---

### 2. Frequency/Phase Response (Bode) Mode

**Purpose:** Frequency response analysis with gain and phase measurements.

#### Command Message
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "bode-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "bode",
    "settings": {
      "channel": "CH0|CH1|CH2",
      "frequency_range": {
        "from": 10,
        "to": 10000,
        "points_per_decade": 10
      },
      "output_voltage": 1.0
    }
  }
}
```

#### Data Stream Message
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "bode-data-uuid",
  "type": "data",
  "payload": {
    "mode": "bode",
    "data": [
      {"frequency": 10.0, "gain": -3.0, "phase": -45.0},
      {"frequency": 31.6, "gain": -9.5, "phase": -71.6},
      {"frequency": 100.0, "gain": -20.0, "phase": -84.3}
    ],
    "progress": 45.2,
    "completed": false
  }
}
```

**Settings Constraints:**
- Frequency range: 1 Hz to 10 kHz
- Output voltage: 0.1V to 20V
- Maximum 500 measurement points

---

### 3. Step Response Mode

**Purpose:** Time-domain step response analysis.

#### Command Message
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "step-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "step",
    "settings": {
      "channel": "CH0|CH1|CH2",
      "voltage": 5.0,
      "measurement_time": 1.0
    }
  }
}
```

#### Data Stream Message
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "step-data-uuid",
  "type": "data",
  "payload": {
    "mode": "step",
    "data": [
      {"time": 0.000, "response": 0.000},
      {"time": 0.005, "response": 0.632},
      {"time": 0.010, "response": 0.865},
      {"time": 0.025, "response": 0.993}
    ],
    "progress": 25.0,
    "completed": false
  }
}
```

**Settings Constraints:**
- Voltage: 0V to 20V
- Measurement time: 0.001s to 10s
- Fixed 200 data points

---

### 4. Impulse Response Mode

**Purpose:** Time-domain impulse response analysis.

#### Command Message
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "impulse-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "impulse",
    "settings": {
      "voltage": 5.0,
      "duration_us": 10,
      "measurement_time": 0.1
    }
  }
}
```

#### Data Stream Message
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "impulse-data-uuid",
  "type": "data",
  "payload": {
    "mode": "impulse",
    "data": [
      {"time": 0.00000, "response": 0.000},
      {"time": 0.00001, "response": 5.000},
      {"time": 0.00005, "response": 3.679},
      {"time": 0.00010, "response": 1.353}
    ],
    "progress": 80.0,
    "completed": false
  }
}
```

**Settings Constraints:**
- Impulse voltage: 0V to 20V
- Duration: 1μs to 1000μs
- Measurement time: 0.001s to 2s

---

### 5. Testbed Mode

**Purpose:** Real-time voltage and current monitoring with live readings.

#### Command Message
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "testbed-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "testbed",
    "settings": {
      "target_voltage": 3.3,
      "current_limit": 0.5,
      "continuous_monitoring": true,
      "update_interval_ms": 100
    }
  }
}
```

#### Continuous Data Stream Message
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "testbed-data-uuid",
  "type": "data",
  "payload": {
    "mode": "testbed",
    "readings": {
      "output_voltage": 3.297,
      "output_current": 0.165,
      "input_ch0": 2.456,
      "input_ch1": 1.789
    },
    "status": "regulating|current_limited|fault",
    "continuous": true
  }
}
```

#### Stop Command
```json
{
  "timestamp": "2024-01-15T10:35:00Z",
  "message_id": "testbed-stop-uuid",
  "type": "command",
  "payload": {
    "mode": "testbed",
    "action": "stop"
  }
}
```

**Settings Constraints:**
- Target voltage: 0V to 20V
- Current limit: 0A to 3A
- Update interval: 50ms to 1000ms

---

### 6. Control Systems Toolbox Mode

**Purpose:** Advanced control system analysis with PID controllers and system modeling.

#### 6.1 Controller Mode - PID Configuration

**Command Message:**
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "cs-pid-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "control_system",
    "submode": "controller",
    "controller_type": "pid",
    "settings": {
      "pid_parameters": {
        "kp": 1.0,
        "ki": 0.1,
        "kd": 0.01
      },
      "simulation": {
        "setpoint": 1.0,
        "duration": 10.0,
        "time_step": 0.05
      },
      "controller_limits": {
        "max_output": 5.0,
        "min_output": -5.0,
        "anti_windup": true
      }
    }
  }
}
```

**Response with Simulation Data:**
```json
{
  "timestamp": "2024-01-15T10:30:01Z",
  "message_id": "cs-pid-resp-uuid",
  "type": "response",
  "payload": {
    "mode": "control_system",
    "submode": "controller",
    "controller_type": "pid",
    "status": "simulation_complete",
    "data": {
      "type": "pid_response",
      "time_series": [
        {"time": 0.000, "response": 0.000, "setpoint": 1.0, "error": 1.0, "controller_output": 1.0},
        {"time": 0.050, "response": 0.047, "setpoint": 1.0, "error": 0.953, "controller_output": 0.958},
        {"time": 0.100, "response": 0.090, "setpoint": 1.0, "error": 0.910, "controller_output": 0.920}
      ],
      "parameters": {
        "kp": 1.0,
        "ki": 0.1,
        "kd": 0.01
      },
      "performance_metrics": {
        "rise_time": 0.45,
        "settling_time": 2.1,
        "overshoot_percent": 8.5,
        "steady_state_error": 0.02
      }
    }
  }
}
```

#### 6.2 Controller Mode - File Upload

**Command Message:**
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "cs-file-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "control_system",
    "submode": "controller",
    "controller_type": "file",
    "settings": {
      "file_upload": {
        "filename": "custom_controller.ctl",
        "file_size": 2048,
        "checksum": "sha256-hash-string"
      }
    }
  }
}
```

**File Transfer Message (Binary Data):**
```json
{
  "timestamp": "2024-01-15T10:30:01Z",
  "message_id": "cs-file-data-uuid",
  "type": "data",
  "payload": {
    "mode": "control_system",
    "file_transfer": {
      "filename": "custom_controller.ctl",
      "chunk_number": 1,
      "total_chunks": 3,
      "data": "base64-encoded-file-chunk"
    }
  }
}
```

#### 6.3 System Mode - Model Upload

**Command Message:**
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "cs-system-cmd-uuid",
  "type": "command",
  "payload": {
    "mode": "control_system",
    "submode": "system",
    "settings": {
      "model_file": {
        "filename": "plant_model.mdl",
        "file_size": 4096,
        "checksum": "sha256-hash-string"
      },
      "simulation_settings": {
        "input_signal": "step|sine|ramp|custom",
        "amplitude": 1.0,
        "duration": 5.0,
        "time_step": 0.01
      }
    }
  }
}
```

**System Response:**
```json
{
  "timestamp": "2024-01-15T10:30:02Z",
  "message_id": "cs-system-resp-uuid",
  "type": "response",
  "payload": {
    "mode": "control_system",
    "submode": "system",
    "status": "model_loaded",
    "model_info": {
      "filename": "plant_model.mdl",
      "system_order": 2,
      "poles": [-1.0, -2.0],
      "zeros": [],
      "dc_gain": 1.0
    },
    "data": [
      {"time": 0.00, "input": 1.0, "output": 0.000},
      {"time": 0.01, "input": 1.0, "output": 0.010},
      {"time": 0.02, "input": 1.0, "output": 0.039}
    ]
  }
}
```

---

## Status and Error Messages

### Status Message Format
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "status-uuid",
  "type": "status",
  "payload": {
    "device_status": "ready|measuring|error|calibrating",
    "current_mode": "va|bode|step|impulse|testbed|control_system",
    "progress": 75.5,
    "estimated_remaining": 30,
    "hardware_status": {
      "temperature": 35.2,
      "voltage_rails": {
        "5v": 5.02,
        "3v3": 3.31,
        "neg5v": -4.98
      },
      "calibration_status": "valid|expired|in_progress"
    }
  }
}
```

### Error Message Format
```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "message_id": "error-uuid",
  "type": "status",
  "payload": {
    "error": true,
    "error_code": "E001",
    "error_message": "Voltage out of range",
    "error_context": {
      "mode": "va",
      "parameter": "end_voltage",
      "value": 25.0,
      "max_allowed": 20.0
    },
    "suggested_action": "Reduce end voltage to maximum 20V"
  }
}
```

## Error Codes

| Code | Description | Context |
|------|-------------|---------|
| E001 | Parameter out of range | Invalid settings value |
| E002 | Hardware fault | ADC/DAC malfunction |
| E003 | Calibration required | Measurements may be inaccurate |
| E004 | File format error | Invalid .ctl or .mdl file |
| E005 | Communication timeout | MQTT connection lost |
| E006 | Memory overflow | Too many data points requested |
| E007 | Channel conflict | Multiple modes using same channel |

## Implementation Notes

### QoS Levels
- Commands: QoS 1 (at least once delivery)
- Data streams: QoS 0 (best effort)
- Status/errors: QoS 1 (at least once delivery)

### Message Size Limits
- Command messages: 16 KB max
- Data messages: 64 KB max
- File transfer chunks: 32 KB max

### Rate Limiting
- Commands: 10 per second max
- Data publishing: Based on measurement mode requirements
- Status updates: 1 per second max

### Security Considerations
- All file uploads must be validated for correct format
- Parameter bounds checking on all numeric inputs
- Authentication required for control commands
- Encrypted MQTT connections recommended for production

## Client Implementation Example

### Python Client Snippet
```python
import paho.mqtt.client as mqtt
import json
import uuid
from datetime import datetime

class PocKETlabMQTT:
    def __init__(self, broker_host, broker_port=1883):
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.connect(broker_host, broker_port, 60)
    
    def send_command(self, mode, settings):
        message = {
            "timestamp": datetime.utcnow().isoformat() + "Z",
            "message_id": str(uuid.uuid4()),
            "type": "command",
            "payload": {
                "mode": mode,
                "settings": settings
            }
        }
        self.client.publish("pocketlab/command", json.dumps(message), qos=1)
    
    def start_va_measurement(self, channel="CH0", start_v=0, end_v=5, step_v=0.1):
        settings = {
            "channel": channel,
            "mode_type": "CV",
            "cv_settings": {
                "start_voltage": start_v,
                "end_voltage": end_v,
                "step_voltage": step_v
            }
        }
        self.send_command("va", settings)
```

This specification provides a comprehensive foundation for MQTT communication between the PocKETlab webapp and measuring board, supporting all implemented measurement modes with detailed parameter validation and error handling.
