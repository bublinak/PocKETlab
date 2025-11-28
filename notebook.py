import json
import numpy as np
from scipy.integrate import solve_ivp
import matplotlib.pyplot as plt

# 1. Parse the Model from the provided JSON data
json_data = """
{
  "system_name": "Mathematical pendulum",
  "system_type": "State-Space",
  "matrices": {
    "A": [
      [0.0, 1.0],
      [-10.0, -0.1]
    ],
    "B": [
      [0.0],
      [10.0]
    ],
    "C": [
      [1.0, 0.0],
      [0.0, 1.0]
    ],
    "D": [
      [0.0],
      [0.0]
    ]
  },
  "input_voltage_range": {
    "min_volts": 0.0,
    "max_volts": 5.0,
    "zero_offset": 0
  },
  "output_voltage_range": {
    "min_volts": 0.0,
    "max_volts": 5.0,
    "zero_offset": 2.5
  },
  "input_mapping": [
    {
      "input_name": "Angular velocity",
      "matrix_column_index": 0
    },
    {
      "input_name": "Angular acceleration",
      "matrix_column_index": 1
    }
  ],
  "output_mapping": [
    {
      "output_name": "Angle",
      "matrix_row_index": 0
    },
    {
      "output_name": "Angular velocity",
      "matrix_row_index": 1
    }
  ]
}
"""

# Load the JSON data into a Python dictionary
model_data = json.loads(json_data)

# Extract matrices and convert them to NumPy arrays for calculations
A = np.array(model_data['matrices']['A'])
B = np.array(model_data['matrices']['B'])
C = np.array(model_data['matrices']['C'])
D = np.array(model_data['matrices']['D'])

print("System Name:", model_data['system_name'])
print("Matrix A:\n", A)
print("Matrix B:\n", B)
print("Matrix C:\n", C)
print("Matrix D:\n", D)

# 2. Set up Simulation Parameters
# Time vector for the simulation
t_start = 0
t_end = 10
t_eval = np.linspace(t_start, t_end, 500) # 500 points from 0 to 10 seconds

# Initial conditions for the state vector x = [angle, angular_velocity]
# Let's start with an initial angle of 0.5 rad (~28 deg) and zero velocity
x0 = [0.5, 0.0]

# Define a simple step input signal u(t)
# The input will be 0 for t < 1s, and 0.2 for t >= 1s
# This simulates a sudden constant force/torque applied to the system.
def input_signal(t):
    if t < 1.0:
        return np.array([0.0])
    else:
        return np.array([0.2])

# 3. Define the System Dynamics for the solver
# This function calculates the derivative of the state vector (dx/dt)
# based on the state-space equation: dx/dt = Ax + Bu
def state_space_model(t, x, A, B, u_func):
    """
    Defines the differential equation for the state-space system.
    
    Args:
        t: Time
        x: State vector [angle, angular_velocity]
        A: System matrix
        B: Input matrix
        u_func: A function that returns the input u at time t
    
    Returns:
        dxdt: The derivative of the state vector
    """
    u = u_func(t)
    dxdt = A @ x + B @ u
    return dxdt

# 4. Run the Simulation
# Use solve_ivp to integrate the differential equation
solution = solve_ivp(
    fun=state_space_model,      # The function defining the model
    t_span=(t_start, t_end),    # Time interval to solve for
    y0=x0,                      # Initial state
    t_eval=t_eval,              # Time points to evaluate the solution at
    args=(A, B, input_signal)   # Extra arguments to pass to the model function
)

# The results are in solution.t (time) and solution.y (states)
# solution.y has states as rows, so we transpose it for easier access
time_history = solution.t
state_history = solution.y.T

# 5. Calculate Outputs
# Calculate the output vector y for each point in time using y = Cx + Du
# Pre-allocate an array for the output history
output_history = np.zeros((len(time_history), C.shape[0]))
for i, t in enumerate(time_history):
    x_t = state_history[i, :]
    u_t = input_signal(t)
    y_t = C @ x_t + D @ u_t
    output_history[i, :] = y_t

# 6. Plot Results
plt.style.use('seaborn-v0_8-whitegrid')
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

# Plot 1: State variables over time
ax1.plot(time_history, state_history[:, 0], label='Angle (x1)')
ax1.plot(time_history, state_history[:, 1], label='Angular Velocity (x2)')
ax1.set_title('State Variables vs. Time')
ax1.set_ylabel('Value')
ax1.legend()
ax1.grid(True)

# Plot 2: Output variables over time
# Note: In this case, C is the identity matrix and D is zero, so y = x.
# The output plot will look identical to the state plot.
ax2.plot(time_history, output_history[:, 0], label='Output 1 (Angle)')
ax2.plot(time_history, output_history[:, 1], label='Output 2 (Angular Velocity)', linestyle='--')
ax2.set_title('Output Variables vs. Time')
ax2.set_xlabel('Time (s)')
ax2.set_ylabel('Value')
ax2.legend()
ax2.grid(True)

plt.tight_layout()
plt.show()