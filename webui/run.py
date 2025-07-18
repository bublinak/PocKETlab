from app import create_app
import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Run the Flask app.')
    parser.add_argument('--enable-registration', action='store_true', help='Enable user registration page even if users exist.')
    parser.add_argument('--mock-on', action='store_true', help='Use mock data instead of MQTT communication.')
    args = parser.parse_args()

    app = create_app(
        enable_registration_override=args.enable_registration,
        mock_on=args.mock_on
    )
    app.run(debug=True)
