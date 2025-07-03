import paho.mqtt.client as mqtt
import json
from datetime import datetime
import uuid
import time

class MqttClient:
    def __init__(self):
        self.client = None
        self.app = None
        self.client_id = f"pocketlab-webapp-{str(uuid.uuid4())}"
        self.data_store = {}
        self.ready_boards = set()

    def init_app(self, app):
        self.app = app
        self.client = mqtt.Client(client_id=self.client_id, clean_session=True)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

        broker_url = app.config['MQTT_BROKER_URL']
        broker_port = app.config['MQTT_BROKER_PORT']
        username = app.config.get('MQTT_USERNAME')
        password = app.config.get('MQTT_PASSWORD')
        keepalive = app.config.get('MQTT_KEEPALIVE', 60)

        if username:
            self.client.username_pw_set(username, password)
        
        try:
            self.client.connect(broker_url, broker_port, keepalive)
            self.client.loop_start()
        except Exception as e:
            print(f"MQTT connection error: {e}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT Broker with client ID: {self.client_id}")
            # Subscribe to general response, data, and status topics
            self.client.subscribe("pocketlab/+/response/#")
            self.client.subscribe("pocketlab/+/data/#")
            self.client.subscribe("pocketlab/+/status")
        else:
            print(f"Failed to connect, return code {rc}\n")

    def on_message(self, client, userdata, msg):
        try:
            topic_parts = msg.topic.split('/')
            payload = json.loads(msg.payload.decode())

            # Handle status messages for board discovery
            if len(topic_parts) == 3 and topic_parts[0] == 'pocketlab' and topic_parts[2] == 'status':
                board_id = topic_parts[1]
                if payload.get('payload', {}).get('device_status') == 'ready':
                    self.ready_boards.add(board_id)
                return

            message_id = payload.get('message_id')
            
            if not message_id:
                return

            # Store data based on message_id for later retrieval
            self.data_store[message_id] = payload

        except Exception as e:
            print(f"Error processing message: {e}")

    def get_ready_boards(self):
        return list(self.ready_boards)

    def publish_command(self, mode, settings, target_board):
        if not self.client.is_connected():
            raise ConnectionError("MQTT client not connected")

        topic = f"pocketlab/{target_board}/command"
        message_id = str(uuid.uuid4())
        
        payload = {
            "mode": mode,
            "settings": settings
        }

        message = {
            "timestamp": datetime.utcnow().isoformat() + "Z",
            "message_id": message_id,
            "type": "command",
            "payload": payload
        }
        
        self.client.publish(topic, json.dumps(message), qos=1)
        return message_id

    def get_data(self, message_id, timeout=10):
        start_time = time.time()
        while time.time() - start_time < timeout:
            if message_id in self.data_store:
                # For streaming data, we might need a more complex logic 
                # to wait for a "completed" flag.
                # For now, we return the first message received for that ID.
                return self.data_store.pop(message_id, None)
            time.sleep(0.1)
        return None
