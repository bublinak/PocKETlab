from flask import Blueprint, render_template, redirect, url_for, flash, request, jsonify, Response, current_app, session
from flask_login import login_user, logout_user, login_required, current_user
from app import db, mqtt_client
from app.models import User
import csv
from io import StringIO
import numpy as np # For linspace and logspace
import json

main_bp = Blueprint('main', __name__)

@main_bp.route('/')
@login_required
def index():
    return render_template('index.html')

@main_bp.route('/login', methods=['GET', 'POST'])
def login():
    if current_user.is_authenticated:
        return redirect(url_for('main.index'))
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        user = User.query.filter_by(username=username).first()
        if user and user.check_password(password):
            login_user(user)
            next_page = request.args.get('next')
            return redirect(next_page) if next_page else redirect(url_for('main.index'))
        else:
            flash('Login Unsuccessful. Please check username and password', 'danger')
    return render_template('login.html')

@main_bp.route('/logout')
def logout():
    logout_user()
    return redirect(url_for('main.login'))

@main_bp.route('/register', methods=['GET', 'POST']) # Added for initial user setup
def register():
    enable_override = current_app.config.get('ENABLE_REGISTRATION_OVERRIDE', False)
    if not enable_override and request.method == 'GET' and User.query.first():
        flash('Registration is disabled as a user already exists. Use --enable-registration to override.', 'info')
        return redirect(url_for('main.login'))

    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')

        if not enable_override and User.query.first() and not User.query.filter_by(username=username).first():
             flash('Registration was just completed by another user or is disabled. Please login.', 'info')
             return redirect(url_for('main.login'))

        if User.query.filter_by(username=username).first():
            flash('Username already exists.', 'warning')
            return redirect(url_for('main.register'))
        
        user = User(username=username)
        user.set_password(password)
        db.session.add(user)
        db.session.commit()
        flash('Registration successful! Please log in.', 'success')
        return redirect(url_for('main.login'))
    return render_template('register.html')


@main_bp.route('/get_boards')
@login_required
def get_boards():
    if not current_app.config.get('MOCK_ON'):
        boards = mqtt_client.get_ready_boards()
        return jsonify(boards)
    return jsonify([]) # Return empty list in mock mode

def generate_va_data(settings):
    channel = settings.get('va_channel')
    mode_type = settings.get('va_mode_type') # CV or CC
    
    data = []
    num_points = 20 # Default number of points

    if mode_type == 'CV':
        start_v = float(settings.get('va_start_voltage', 0))
        end_v = float(settings.get('va_end_voltage', 1))
        step_v = float(settings.get('va_step_voltage', 0.1))
        if step_v <= 0: step_v = 0.1 # Avoid division by zero or infinite loop
        
        voltages = np.arange(start_v, end_v + step_v, step_v)
        if len(voltages) > 200: voltages = np.linspace(start_v, end_v, 200) # Limit points

        for v in voltages:
            # Simulate current based on voltage (e.g., simple resistor)
            # Add some channel-specific behavior if desired
            current = v / 100 + np.random.rand() * 0.0 # 100 Ohm resistor + noise
            if channel == 'CH1':
                current *= 1.2
            elif channel == 'CH2':
                current *= 1.5
            data.append({"voltage": round(v, 3), "current": round(current, 4)})
    elif mode_type == 'CC' and channel == 'CH2': # CC only for CH2
        start_i = float(settings.get('va_start_current', 0))
        end_i = float(settings.get('va_end_current', 0.1))
        step_i = float(settings.get('va_step_current', 0.01))
        if step_i <= 0: step_i = 0.01

        currents = np.arange(start_i, end_i + step_i, step_i)
        if len(currents) > 200: currents = np.linspace(start_i, end_i, 200)

        for i in currents:
            # Simulate voltage based on current (e.g., diode-like behavior)
            voltage = 0.7 + np.log1p(i * 1000) * 0.1 + np.random.rand() * 0.0 # Simplified diode
            data.append({"voltage": round(voltage, 3), "current": round(i, 4)})
    return data

def generate_bode_data(settings):
    # channel = settings.get(\'bode_channel\') # Use if channel affects data
    freq_from = float(settings.get('bode_freq_from', 10))
    freq_to = float(settings.get('bode_freq_to', 10000))
    freq_points_per_decade = int(settings.get('bode_freq_steps', 10))
    output_voltage = float(settings.get('bode_output_voltage', 1))

    if freq_from <=0: freq_from = 1
    if freq_to > 10000: freq_to = 10000
    if freq_to < freq_from: freq_to = freq_from + 100

    num_decades = np.log10(freq_to / freq_from)
    num_points = max(10, int(num_decades * freq_points_per_decade)) # Ensure at least 10 points
    if num_points > 500: num_points = 500 # Cap points

    frequencies = np.logspace(np.log10(freq_from), np.log10(freq_to), num=num_points)
    data = []
    # Simulate a simple RC filter for Bode plot
    r = 1000  # 1k Ohm
    c = 1e-6  # 1uF
    
    for freq in frequencies:
        omega = 2 * np.pi * freq
        xc = 1 / (omega * c)
        # Voltage divider: Vout = Vin * (Xc / sqrt(R^2 + Xc^2))
        # Gain in dB: 20 * log10(|Vout/Vin|)
        # Phase: atan(-R / Xc)
        
        # Impedance of capacitor
        z_c = -1j / (omega * c) 
        # Transfer function H(s) = Zc / (R + Zc) for a low pass RC
        h_s = z_c / (r + z_c)
        
        gain_val = np.abs(h_s)
        gain_db = 20 * np.log10(gain_val if gain_val > 1e-9 else 1e-9) # Avoid log(0)
        phase_rad = np.angle(h_s)
        phase_deg = np.degrees(phase_rad)
        
        # Simulate effect of output_voltage (though typically Bode is normalized)
        gain_db += 5 * np.log10(output_voltage if output_voltage > 0.1 else 0.1)


        data.append({"frequency": round(freq, 2), "gain": round(gain_db, 2), "phase": round(phase_deg, 2)})
    return data

def generate_step_data(settings):
    # channel = settings.get(\'step_channel\') # Use if channel affects data
    voltage = float(settings.get('step_voltage', 5))
    measurement_time = float(settings.get('step_time', 1))
    if measurement_time <=0: measurement_time = 0.1
    if measurement_time > 10: measurement_time = 10 # Cap time

    num_points = 200
    time_points = np.linspace(0, measurement_time, num_points)
    data = []
    # Simulate a first-order system response: V(t) = V_final * (1 - exp(-t/tau))
    tau = measurement_time / 5 # Time constant, system reaches ~99% in 5*tau
    
    for t in time_points:
        response = voltage * (1 - np.exp(-t / tau)) + (np.random.rand()-0.5)*0.00*voltage
        data.append({"time": round(t, 4), "response": round(response, 3)})
    return data

def generate_impulse_data(settings):
    impulse_voltage = float(settings.get('impulse_voltage', 5))
    # impulse_duration_us = float(settings.get(\'impulse_duration\', 10)) # Duration might affect peak or shape
    measurement_time = float(settings.get('impulse_time', 0.1))
    if measurement_time <=0: measurement_time = 0.01
    if measurement_time > 2: measurement_time = 2 # Cap time

    num_points = 200
    time_points = np.linspace(0, measurement_time, num_points)
    data = []
    # Simulate a second-order system impulse response: V(t) = (A/tau) * t * exp(-t/tau) or similar
    tau = measurement_time / 10 # Time constant, adjust for desired decay
    peak_time = tau # For this simplified model

    for t in time_points:
        # A simple decaying exponential scaled by voltage
        # response = impulse_voltage * np.exp(-t / tau) * (1 - np.exp(-t*5/tau)) # more complex shape
        if t < peak_time/10 and peak_time > 0: # very short pulse effect
             response = (impulse_voltage / (peak_time/10)) * t
        else:
             response = impulse_voltage * np.exp(-(t-peak_time/10) / tau)
        response += (np.random.rand()-0.5)*0.00*impulse_voltage
        data.append({"time": round(t, 5), "response": round(response, 3)})
    return data

def generate_testbed_data(settings):
    # Simulate setting voltage and current limit
    # In a real scenario, this would interact with the device
    target_voltage = float(settings.get('testbed_target_voltage', 0))
    current_limit = float(settings.get('testbed_current_limit', 0.1))

    # Simulate some readings
    # These would come from actual device measurements
    # For now, let's make them somewhat responsive to settings
    actual_output_voltage = target_voltage * (np.random.rand() * 0.1 + 0.95) # Simulate slight variation
    
    # Simulate current based on a dummy load and limit
    # Assume a 10 Ohm load for this simulation if nothing is connected
    simulated_load_resistance = 10 # Ohms
    potential_current = actual_output_voltage / simulated_load_resistance
    actual_output_current = min(potential_current, current_limit)
    
    # If current is limited, voltage might drop (simplified)
    if potential_current > current_limit:
        actual_output_voltage = actual_output_current * simulated_load_resistance * 0.98 # Simulate voltage drop due to current limiting

    input_ch0_voltage = np.random.rand() * 5 # Random voltage for CH0
    input_ch1_voltage = np.random.rand() * 3.3 # Random voltage for CH1
    
    return {
        "output_voltage": round(actual_output_voltage, 3),
        "output_current": round(actual_output_current, 4),
        "input_ch0": round(input_ch0_voltage, 3),
        "input_ch1": round(input_ch1_voltage, 3)
    }

def generate_control_system_data(settings):
    cs_mode = settings.get('cs_mode', 'controller')
    controller_type = settings.get('cs_controller_type', 'pid') # Default to PID if controller mode
    file_name = settings.get('cs_model_file_name') # Get the filename from the form

    file_info_text = "No file specified."
    expected_extension = ".mdl" if cs_mode == 'system' else ".ctl"
    if file_name:
        file_info_text = f"File: {file_name} (expected {expected_extension})"
        if not file_name.lower().endswith(expected_extension):
            file_info_text += f" - Warning: Incorrect file type for mode."


    if cs_mode == 'controller':
        if controller_type == 'pid':
            kp = float(settings.get('cs_pid_kp', 1.0))
            ki = float(settings.get('cs_pid_ki', 0.1))
            kd = float(settings.get('cs_pid_kd', 0.01))

            setpoint = 1.0
            y_plant = 0.0  # Plant output
            integral_error_val = 0.0
            previous_error_val = 0.0
            
            dt = 0.05  # Time step
            sim_duration = 10.0  # Simulate for 10 seconds
            num_steps = int(sim_duration / dt)
            
            response_data = []

            # Plant parameters (simple first-order system: y[k+1] = plant_a * y[k] + plant_b * u[k])
            plant_a = 0.95 
            plant_b = 0.05 

            max_u = 5.0 # Max controller output
            min_u = -5.0 # Min controller output
            
            # Max integral error (anti-windup), scaled by Ki to be somewhat reasonable
            max_integral = 20.0 / (ki if ki != 0 else 1.0) 


            for i in range(num_steps):
                time = i * dt
                
                error_val = setpoint - y_plant
                
                integral_error_val += error_val * dt
                integral_error_val = max(min(integral_error_val, max_integral), -max_integral) # Anti-windup
                
                derivative_error_val = (error_val - previous_error_val) / dt if i > 0 and dt > 0 else 0.0
                
                u_controller = kp * error_val + ki * integral_error_val + kd * derivative_error_val
                u_controller = max(min(u_controller, max_u), min_u) # Saturate controller output
                
                y_plant = plant_a * y_plant + plant_b * u_controller # Update plant
                
                response_data.append({"time": round(time, 3), "response": round(y_plant, 4)})
                previous_error_val = error_val
            
            return {
                "type": "pid_response", 
                "data": response_data, 
                "params": {"kp": kp, "ki": ki, "kd": kd},
                "file_info": file_info_text,
                "status": "PID Controller simulation complete."
            }
        else: # Other controller types
            return {
                "type": "controller_status",
                "status": f"Controller mode active ({controller_type}). Waiting for configuration.",
                "file_info": file_info_text
            }
    
    elif cs_mode == 'system':
        return {
            "type": "system_status",
            "status": "Controlled system mode active. Waiting for model execution.",
            "file_info": file_info_text
        }
    
    return {"type": "unknown_cs_state", "status": "Unknown control system state."}


@main_bp.route('/measure', methods=['POST'])
@login_required
def measure():
    if current_app.config.get('MOCK_ON'):
        measure_mode = request.form.get('measure_mode')
        settings = request.form
        data = []
        if measure_mode == 'va':
            data = generate_va_data(settings)
        elif measure_mode == 'bode':
            data = generate_bode_data(settings)
        elif measure_mode == 'step':
            data = generate_step_data(settings)
        elif measure_mode == 'impulse':
            data = generate_impulse_data(settings)
        elif measure_mode == 'testbed':
            data = generate_testbed_data(settings)
        elif measure_mode == 'control_system':
            data = generate_control_system_data(settings)
        return jsonify(data)
    else:
        try:
            print(f"[DEBUG] Starting /measure endpoint processing")
            measure_mode = request.form.get('measure_mode')
            print(f"[DEBUG] measure_mode: {measure_mode}")
            # Remove cs_model_file_input_name and cs_model_data from settings to avoid sending raw data
            settings = {k: v for k, v in request.form.items() if k not in ['measure_mode', 'target_board', 'cs_model_file_input_name', 'cs_model_data']}
            print(f"[DEBUG] settings: {settings}")
            target_board = request.form.get('target_board', 'default')
            print(f"[DEBUG] target_board: {target_board}")

            # Special handling for control system mode (system)
            if measure_mode == 'control_system' and settings.get('cs_mode') == 'system':
                print(f"[DEBUG] Processing control system mode with cs_mode=system")
                print(f"[DEBUG] request.files keys: {list(request.files.keys())}")
                print(f"[DEBUG] request.form keys: {list(request.form.keys())}")
                
                # Check if model data was sent as JSON string (new method)
                model_data_str = request.form.get('cs_model_data')
                if model_data_str:
                    print(f"[DEBUG] Found pre-uploaded model data")
                    try:
                        mdl_data = json.loads(model_data_str)
                        print(f"[DEBUG] Model data parsing successful, keys: {list(mdl_data.keys())}")
                    except Exception as e:
                        print(f"[DEBUG] ERROR: Model data parsing failed: {e}")
                        return jsonify({"error": f"Failed to parse model data: {e}"}), 400
                else:
                    # Fallback to file upload method
                    mdl_file = request.files.get('cs_model_file_input')
                    print(f"[DEBUG] mdl_file: {mdl_file}")
                    if mdl_file:
                        print(f"[DEBUG] mdl_file.filename: {mdl_file.filename}")
                    
                    if not mdl_file:
                        print(f"[DEBUG] ERROR: No model file uploaded and no model data provided")
                        return jsonify({"error": "No model file uploaded. Please use the Upload Model button first."}), 400
                    if not mdl_file.filename.endswith('.mdl'):
                        print(f"[DEBUG] ERROR: Wrong file extension: {mdl_file.filename}")
                        return jsonify({"error": f"Uploaded file '{mdl_file.filename}' is not a .mdl file."}), 400
                    
                    mdl_file.seek(0, 2)  # Seek to end to check size
                    file_size = mdl_file.tell()
                    mdl_file.seek(0)  # Reset to start
                    print(f"[DEBUG] File size: {file_size} bytes")
                    
                    if file_size == 0:
                        print(f"[DEBUG] ERROR: Empty file")
                        return jsonify({"error": "Uploaded .mdl file is empty."}), 400
                    
                    try:
                        mdl_data = json.load(mdl_file)
                        print(f"[DEBUG] JSON parsing successful, keys: {list(mdl_data.keys())}")
                    except Exception as e:
                        print(f"[DEBUG] ERROR: JSON parsing failed: {e}")
                        return jsonify({"error": f"Failed to parse uploaded model file as JSON: {e}"}), 400
                
                # Validate required fields in mdl_data
                required_fields = ['matrices', 'input_voltage_range', 'output_voltage_range', 'input_mapping', 'output_mapping']
                missing_fields = [f for f in required_fields if f not in mdl_data]
                if missing_fields:
                    print(f"[DEBUG] ERROR: Missing required fields: {missing_fields}")
                    return jsonify({"error": f"Model file missing required fields: {', '.join(missing_fields)}"}), 400
                
                print(f"[DEBUG] All required fields present")
                
                try:
                    settings['system_model'] = {
                        'A': mdl_data['matrices']['A'],
                        'B': mdl_data['matrices']['B'],
                        'C': mdl_data['matrices']['C'],
                        'D': mdl_data['matrices']['D'],
                        'input_voltage_range': mdl_data['input_voltage_range'],
                        'output_voltage_range': mdl_data['output_voltage_range'],
                        'input_mapping': mdl_data['input_mapping'],
                        'output_mapping': mdl_data['output_mapping'],
                        'system_name': mdl_data.get('system_name'),
                        'system_type': mdl_data.get('system_type')
                    }
                    print(f"[DEBUG] Model data successfully added to settings")
                except Exception as e:
                    print(f"[DEBUG] ERROR: Model structure error: {e}")
                    return jsonify({"error": f"Model file structure error: {e}"}), 400

            print(f"[DEBUG] Final settings being sent to device: {settings}")
            
            # Flush graph buffers before starting new measurement
            print(f"[DEBUG] Flushing buffers for board: {target_board}, mode: {measure_mode}")
            mqtt_client.flush_mode_buffer(target_board, measure_mode)
            
            message_id = mqtt_client.publish_command(measure_mode, settings, target_board)
            print(f"[DEBUG] MQTT message_id: {message_id}")
            
            # Wait for the response from the board to confirm command was received
            print(f"[DEBUG] Waiting for response with message_id: {message_id}")
            response_data = mqtt_client.get_data(message_id, timeout=5)
            
            # If no direct message_id match, try to get response by board_id and mode
            if not response_data:
                print(f"[DEBUG] No direct message_id match, trying board_id/mode correlation")
                response_data = mqtt_client.get_response_by_board_mode(target_board, measure_mode, timeout=10)
            
            if response_data:
                print(f"[DEBUG] Received response: {response_data}")
                response_payload = response_data.get('payload', {})
                response_status = response_payload.get('status')
                response_message = response_payload.get('message', '')
                response_mode = response_payload.get('mode')
                
                if response_status == 'success':
                    print(f"[DEBUG] {measure_mode} command accepted by device")
                    
                    # For continuous modes (testbed, control_system), return streaming started
                    if measure_mode in ['testbed', 'control_system']:
                        return jsonify({
                            "status": "started",
                            "message": response_message or f"{measure_mode.replace('_', ' ').title()} mode started successfully. Data will be streamed in real-time.",
                            "type": f"{measure_mode}_started",
                            "mode": settings.get('cs_mode', 'unknown') if measure_mode == 'control_system' else measure_mode,
                            "estimated_duration": response_payload.get('estimated_duration')
                        })
                    
                    # For measurement modes (va, bode, step, impulse), return streaming started
                    elif measure_mode in ['va', 'bode', 'step', 'impulse']:
                        return jsonify({
                            "status": "started", 
                            "message": response_message or f"{measure_mode.upper()} measurement started successfully. Data will be collected in real-time.",
                            "type": f"{measure_mode}_started",
                            "mode": measure_mode,
                            "estimated_duration": response_payload.get('estimated_duration')
                        })
                    
                    # For other modes, return the response data directly
                    else:
                        return jsonify(response_payload.get('data', []))
                        
                else:
                    # Command failed
                    error_msg = response_message or f"Device rejected {measure_mode} command"
                    print(f"[DEBUG] Device rejected command: {error_msg}")
                    return jsonify({"error": error_msg}), 400
                    
            else:
                print(f"[DEBUG] No response received from device")
                return jsonify({"error": "Request timed out. No response from device."}), 504

        except ConnectionError as e:
            return jsonify({"error": str(e)}), 503
        except Exception as e:
            return jsonify({"error": f"An unexpected error occurred: {e}"}), 500

@main_bp.route('/stop', methods=['POST'])
@login_required
def stop():
    """Stop continuous measurement modes (testbed, control systems)"""
    try:
        print(f"[DEBUG] Stop endpoint called")
        measure_mode = request.form.get('measure_mode')
        target_board = request.form.get('target_board', 'default')
        print(f"[DEBUG] Stopping measure_mode: {measure_mode}, target_board: {target_board}")
        
        if measure_mode in ['testbed', 'control_system']:
            # Send stop command via MQTT
            if not current_app.config.get('MOCK_ON'):
                from app import mqtt_client
                if mqtt_client and mqtt_client.is_connected():
                    import uuid
                    import time
                    
                    # Create proper MQTT API compliant stop command
                    command = {
                        "timestamp": str(int(time.time() * 1000)),
                        "message_id": str(uuid.uuid4()),
                        "type": "command",
                        "payload": {
                            "mode": measure_mode,
                            "action": "stop"
                        }
                    }
                    topic = f"pocketlab/{target_board}/command"  # Use 'command' not 'commands'
                    mqtt_client.client.publish(topic, json.dumps(command), qos=1)
                    print(f"[DEBUG] Published stop command to {topic}: {command}")
                    
                    # Clear continuous data buffer for this board
                    mqtt_client.clear_continuous_data(target_board)
                    print(f"[DEBUG] Cleared continuous data for board: {target_board}")
                    
            return jsonify({
                "status": "stopped",
                "message": f"{measure_mode.replace('_', ' ').title()} mode stopped successfully"
            })
        else:
            return jsonify({
                "status": "not_applicable", 
                "message": "Stop is only applicable for continuous modes"
            })
            
    except Exception as e:
        print(f"[DEBUG] ERROR in stop endpoint: {e}")
        return jsonify({"error": str(e)}), 500

@main_bp.route('/export_csv', methods=['POST'])
@login_required
def export_csv():
    data_to_export = request.json.get('data')
    if not data_to_export:
        return jsonify({"error": "No data to export"}), 400

    si = StringIO()
    cw = csv.writer(si)

    if not data_to_export: # Should not happen if button is enabled correctly
        return Response(status=204)

    # Write header
    # Adjust for testbed and control_system data which are not lists of dicts
    if isinstance(data_to_export, list) and data_to_export:
        headers = list(data_to_export[0].keys())
        cw.writerow(headers)
        # Write data rows
        for row in data_to_export:
            cw.writerow([row[h] for h in headers])
    elif isinstance(data_to_export, dict): # For single dict data like testbed
        headers = list(data_to_export.keys())
        cw.writerow(headers)
        cw.writerow([data_to_export[h] for h in headers])
    else: # No data or unsupported format
        return Response(status=204)


    output = si.getvalue()
    return Response(
        output,
        mimetype="text/csv",
        headers={"Content-disposition":
                 "attachment; filename=measurement_data.csv"})

@main_bp.route('/get_continuous_data', methods=['GET'])
@login_required
def get_continuous_data():
    """Get continuous data from MQTT for real-time graphing"""
    try:
        board_id = request.args.get('board_id', 'default')
        mode = request.args.get('mode', 'control_system')
        print(f"[DEBUG] Getting continuous data for board: {board_id}, mode: {mode}")
        
        if current_app.config.get('MOCK_ON'):
            # Return mock data for testing based on mode (ONLY continuous modes)
            if mode == 'testbed':
                import numpy as np
                mock_data = {
                    "mode": "testbed",
                    "readings": {
                        "output_voltage": round(3.3 + np.random.rand() * 0.1, 3),
                        "output_current": round(0.15 + np.random.rand() * 0.05, 4),
                        "input_ch0": round(2.5 + np.random.rand() * 0.5, 3),
                        "input_ch1": round(1.8 + np.random.rand() * 0.3, 3)
                    },
                    "status": "regulating",
                    "continuous": True
                }
                return jsonify(mock_data)
            elif mode == 'control_system':
                import numpy as np
                import time
                current_time = int(time.time() * 1000)  # milliseconds
                timestamps = [current_time + i * 100 for i in range(10)]
                inputs = (np.random.rand(10) - 0.5) * 2  # Random values between -1 and 1
                states_x1 = np.cumsum(np.random.randn(10) * 10)  # Random walk
                states_x2 = np.cumsum(np.random.randn(10) * 20)  # Random walk
                
                mock_data = {
                    "type": "data",
                    "mode": "control_system", 
                    "payload": {
                        "sample_count": 10,
                        "frequency_hz": 10,
                        "continuous": True,
                        "timestamps": timestamps,
                        "inputs": inputs.tolist(),
                        "states_x1": states_x1.tolist(),
                        "states_x2": states_x2.tolist(),
                        "outputs_y1": states_x1.tolist(),  # Same as states for this example
                        "outputs_y2": states_x2.tolist()
                    }
                }
                return jsonify([mock_data])
            else:
                return jsonify([])
        
        from app import mqtt_client
        if mqtt_client:
            if mode == 'control_system':
                # Get control system data (returns list of data packets)
                continuous_data = mqtt_client.get_control_system_data(board_id)
                print(f"[DEBUG] Retrieved {len(continuous_data)} control system data points")
                return jsonify(continuous_data)
            elif mode == 'testbed':
                # Get latest testbed readings
                testbed_data = mqtt_client.get_testbed_data(board_id)
                print(f"[DEBUG] Retrieved testbed data: {testbed_data}")
                return jsonify(testbed_data if testbed_data else {})
            else:
                print(f"[DEBUG] Mode {mode} is not a continuous mode")
                return jsonify({})
        else:
            return jsonify({})
            
    except Exception as e:
        print(f"[DEBUG] ERROR in get_continuous_data: {e}")
        return jsonify({"error": str(e)}), 500

@main_bp.route('/get_measurement_data', methods=['GET'])
@login_required
def get_measurement_data():
    """Get measurement data for non-continuous modes (VA, Bode, Step, Impulse)"""
    try:
        board_id = request.args.get('board_id', 'default')
        mode = request.args.get('mode', 'va')
        print(f"[DEBUG] Getting measurement data for board: {board_id}, mode: {mode}")
        
        if current_app.config.get('MOCK_ON'):
            # Return mock data for testing based on mode
            if mode == 'va':
                import numpy as np
                mock_data = {
                    "mode": "va",
                    "data": [
                        {"voltage": float(v), "current": float(v * 0.001 + np.random.rand() * 0.0001)}
                        for v in np.arange(0, 5, 0.1)
                    ],
                    "progress": min(100, np.random.rand() * 100),
                    "completed": np.random.rand() > 0.8
                }
                return jsonify(mock_data)
            elif mode in ['bode', 'step', 'impulse']:
                import numpy as np
                mock_data = {
                    "mode": mode,
                    "data": [
                        {"x": float(x), "y": float(np.sin(x) + np.random.rand() * 0.1)}
                        for x in np.arange(0, 10, 0.1)
                    ],
                    "progress": min(100, np.random.rand() * 100),
                    "completed": np.random.rand() > 0.8
                }
                return jsonify(mock_data)
            else:
                return jsonify({})
        
        from app import mqtt_client
        if mqtt_client and mode in ['va', 'bode', 'step', 'impulse']:
            # Get accumulated measurement data for non-continuous modes
            measurement_data = mqtt_client.get_measurement_data(board_id, mode)
            print(f"[DEBUG] Retrieved {mode} data: {len(measurement_data.get('data', [])) if measurement_data else 0} points")
            return jsonify(measurement_data if measurement_data else {})
        else:
            return jsonify({})
            
    except Exception as e:
        print(f"[DEBUG] ERROR in get_measurement_data: {e}")
        return jsonify({"error": str(e)}), 500
