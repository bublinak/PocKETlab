import os

class Config:
    SECRET_KEY = os.environ.get('SECRET_KEY') or 'you-will-never-guess'
    SQLALCHEMY_DATABASE_URI = os.environ.get('DATABASE_URL') or \
        'sqlite:///' + os.path.join(os.path.abspath(os.path.dirname(__file__)), 'app.db')
    SQLALCHEMY_TRACK_MODIFICATIONS = False

    # MQTT Configuration
    MQTT_BROKER_URL = os.environ.get('MQTT_BROKER_URL') or 'broker.hivemq.com'
    MQTT_BROKER_PORT = int(os.environ.get('MQTT_BROKER_PORT') or 1883)
    MQTT_USERNAME = os.environ.get('MQTT_USERNAME') or ''
    MQTT_PASSWORD = os.environ.get('MQTT_PASSWORD') or ''
    MQTT_KEEPALIVE = int(os.environ.get('MQTT_KEEPALIVE') or 60)
