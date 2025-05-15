from flask import Flask
from flask_sqlalchemy import SQLAlchemy
from flask_login import LoginManager
from flask_bcrypt import Bcrypt

db = SQLAlchemy()
login_manager = LoginManager()
bcrypt = Bcrypt()

def create_app(enable_registration_override=False):
    app = Flask(__name__)
    app.config.from_object('config.Config')
    app.config['ENABLE_REGISTRATION_OVERRIDE'] = enable_registration_override # Store override in app config

    db.init_app(app)
    login_manager.init_app(app)
    bcrypt.init_app(app)

    login_manager.login_view = 'main.login'
    login_manager.login_message_category = 'info'

    from app.routes import main_bp
    app.register_blueprint(main_bp)

    # Create database tables if they don't exist
    # This will run when the app is initialized.
    with app.app_context():
        db.create_all()

    return app
