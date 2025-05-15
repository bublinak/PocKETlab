from flask import Blueprint, render_template, redirect, url_for, flash, request, jsonify, Response, current_app
from flask_login import login_user, logout_user, login_required, current_user
from app import db
from app.models import User
import csv
from io import StringIO
import numpy as np # For linspace and logspace

main_bp = Blueprint('main', __name__)

# Dummy data for demonstration
mock_data = {
    "va": [{"voltage": i * 0.1, "current": i * 0.05 + 0.1} for i in range(10)],
    "bode": [{"frequency": 10**i, "gain": 20 - i*5, "phase": -i*45} for i in range(1, 5)],
    "step": [{"time": i * 0.01, "response": 1 - 0.5**(i*0.1)} for i in range(100)],
    "impulse": [{"time": i * 0.01, "response": (0.5**(i*0.05)) * ((i*0.1)**2)} for i in range(100)]
}

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

@main_bp.route('/measure', methods=['POST'])
@login_required
def measure():
    measure_mode = request.form.get('measure_mode')
    settings = request.form # Get all form data

    data = []
    if measure_mode == 'va':
        data = generate_va_data(settings)
    elif measure_mode == 'bode':
        data = generate_bode_data(settings)
    elif measure_mode == 'step':
        data = generate_step_data(settings)
    elif measure_mode == 'impulse':
        data = generate_impulse_data(settings)
    
    return jsonify(data)

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
    headers = list(data_to_export[0].keys())
    cw.writerow(headers)
    # Write data rows
    for row in data_to_export:
        cw.writerow([row[h] for h in headers])

    output = si.getvalue()
    return Response(
        output,
        mimetype="text/csv",
        headers={"Content-disposition":
                 "attachment; filename=measurement_data.csv"})
