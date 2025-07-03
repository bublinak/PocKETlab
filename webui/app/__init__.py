from flask import Flask
from flask_sqlalchemy import SQLAlchemy
from flask_login import LoginManager
from flask_bcrypt import Bcrypt
from .mqtt_client import MqttClient

db = SQLAlchemy()
login_manager = LoginManager()
bcrypt = Bcrypt()
mqtt_client = MqttClient()

def create_app(enable_registration_override=False, mock_on=False):
    app = Flask(__name__)
    app.config.from_object('config.Config')
    app.config['ENABLE_REGISTRATION_OVERRIDE'] = enable_registration_override # Store override in app config
    app.config['MOCK_ON'] = mock_on

    db.init_app(app)
    login_manager.init_app(app)
    bcrypt.init_app(app)

    if not mock_on:
        mqtt_client.init_app(app)

    login_manager.login_view = 'main.login'
    login_manager.login_message_category = 'info'

    from app.routes import main_bp
    app.register_blueprint(main_bp)

    # Create database tables if they don't exist
    # This will run when the app is initialized.
    with app.app_context():
        db.create_all()

    return app
