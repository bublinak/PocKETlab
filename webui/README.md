# Measuring Device Frontend

This is a simple Flask application that serves as a frontend for a measuring device.

## Features

- VA characteristics measurement
- Frequency and phase response (Bode plot)
- Step response
- Impulse response
- User authentication
- Data export to CSV

## Setup

1.  **Create a virtual environment:**
    ```bash
    python -m venv venv
    ```
2.  **Activate the virtual environment:**
    -   Windows:
        ```bash
        venv\\Scripts\\activate
        ```
    -   macOS/Linux:
        ```bash
        source venv/bin/activate
        ```
3.  **Install dependencies:**
    ```bash
    pip install -r requirements.txt
    ```
4.  **Run the application:**
    ```bash
    python run.py
    ```
    The application will be available at \`http://127.0.0.1:5000\`.

## First-time User Registration
Navigate to \`http://127.0.0.1:5000/register\` to create the first user account. After the first user is created, the registration page will be disabled.
