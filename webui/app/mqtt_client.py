try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    print("Warning: paho-mqtt not available. MQTT functionality disabled.")

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
        self.continuous_data_store = {}  # Store continuous data streams by board_id (testbed, control_system)
        self.measurement_data_store = {}  # Store measurement data by board_id for non-continuous modes (va, bode, step, impulse)
        self.response_store = {}  # Store latest response by board_id for correlation
        
        # In mock mode, add some default boards
        if not MQTT_AVAILABLE:
            self.ready_boards.add('pocketlab_01')
            self.ready_boards.add('default')

    def init_app(self, app):
        self.app = app
        
        if not MQTT_AVAILABLE:
            print("MQTT not available - running in mock mode")
            return
            
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
            self.client.subscribe("pocketlab/+/response")
            self.client.subscribe("pocketlab/+/response/#")
            self.client.subscribe("pocketlab/+/data")
            self.client.subscribe("pocketlab/+/data/#")
            self.client.subscribe("pocketlab/+/status")
            print(f"[MQTT] Subscribed to topics: pocketlab/+/response, pocketlab/+/data, pocketlab/+/status")
        else:
            print(f"Failed to connect, return code {rc}\n")

    def on_message(self, client, userdata, msg):
        try:
            topic_parts = msg.topic.split('/')
            payload = json.loads(msg.payload.decode())
            
            print(f"[MQTT] Received message on topic: {msg.topic}")
            print(f"[MQTT] Topic parts: {topic_parts}")
            # Check for mode in both locations for debugging
            top_level_mode = payload.get('mode')
            payload_mode = payload.get('payload', {}).get('mode')
            detected_mode = top_level_mode or payload_mode
            print(f"[MQTT] Payload type: {payload.get('type')}, mode: {detected_mode} (top: {top_level_mode}, payload: {payload_mode})")

            # Handle status messages for board discovery
            if len(topic_parts) == 3 and topic_parts[0] == 'pocketlab' and topic_parts[2] == 'status':
                board_id = topic_parts[1]
                if payload.get('payload', {}).get('device_status') == 'ready':
                    self.ready_boards.add(board_id)
                    print(f"[MQTT] Board {board_id} marked as ready")
                return

            # Handle data messages based on mode
            if len(topic_parts) >= 3 and topic_parts[0] == 'pocketlab' and ('data' in topic_parts[2]):
                board_id = topic_parts[1]
                message_type = payload.get('type')
                # Check for mode in both locations - top level first, then inside payload
                mode = payload.get('mode')
                if not mode:
                    mode = payload.get('payload', {}).get('mode')
                
                print(f"[MQTT] Processing data message: board_id={board_id}, mode={mode}, message_type={message_type}")
                
                if message_type == 'data' and mode:
                    self._handle_data_message(board_id, mode, payload)
                    return
                elif message_type == 'data' and not mode:
                    print(f"[MQTT] ERROR: Data message with no mode detected")
                    return

            # Handle response messages
            if len(topic_parts) >= 3 and topic_parts[0] == 'pocketlab' and ('response' in topic_parts[2]):
                board_id = topic_parts[1]
                message_type = payload.get('type')
                
                if message_type == 'response':
                    self._handle_response_message(board_id, payload)
                    return

            # Handle messages with message_id for direct response matching
            message_id = payload.get('message_id')
            if message_id:
                # Store data based on message_id for later retrieval
                self.data_store[message_id] = payload
                print(f"[MQTT] Stored message with ID: {message_id}")

        except Exception as e:
            print(f"[MQTT] Error processing message: {e}")

    def _handle_data_message(self, board_id, mode, payload):
        """Handle data messages based on measurement mode"""
        try:
            if mode == 'va':
                self._handle_va_data(board_id, payload)
            elif mode == 'bode':
                self._handle_bode_data(board_id, payload)
            elif mode == 'step':
                self._handle_step_data(board_id, payload)
            elif mode == 'impulse':
                self._handle_impulse_data(board_id, payload)
            elif mode == 'testbed':
                self._handle_testbed_data(board_id, payload)
            elif mode == 'control_system':
                self._handle_control_system_data(board_id, payload)
            else:
                print(f"[MQTT] Unknown data mode: {mode}")
        except Exception as e:
            print(f"[MQTT] Error handling {mode} data: {e}")

    def _handle_response_message(self, board_id, payload):
        """Handle response messages from the board"""
        try:
            mode = payload.get('payload', {}).get('mode')
            status = payload.get('payload', {}).get('status')
            message_id = payload.get('message_id')
            
            print(f"[MQTT] Response from {board_id}: mode={mode}, status={status}, msg_id={message_id}")
            
            # Store response for retrieval by message_id (for direct matching)
            if message_id:
                self.data_store[message_id] = payload
            
            # Also store by board_id for correlation when message_ids don't match
            response_key = f"{board_id}_{mode}"
            self.response_store[response_key] = payload
            print(f"[MQTT] Stored response for key: {response_key}")
                
        except Exception as e:
            print(f"[MQTT] Error handling response: {e}")

    def _handle_va_data(self, board_id, payload):
        """Handle VA characteristics data (non-continuous measurement mode)"""
        payload_data = payload.get('payload', {})
        data_points = payload_data.get('data', [])
        progress = payload_data.get('progress', 0)
        completed = payload_data.get('completed', False)
        
        print(f"[MQTT] VA data from {board_id}: {len(data_points)} points, {progress:.1f}% complete")
        
        # Store VA data in measurement data store (not continuous data store)
        if board_id not in self.measurement_data_store:
            self.measurement_data_store[board_id] = {}
        
        if 'va' not in self.measurement_data_store[board_id]:
            self.measurement_data_store[board_id]['va'] = {
                'mode': 'va',
                'data': [],
                'progress': 0,
                'completed': False
            }
        
        # Accumulate data points from bulk messages
        self.measurement_data_store[board_id]['va']['data'].extend(data_points)
        self.measurement_data_store[board_id]['va']['progress'] = progress
        self.measurement_data_store[board_id]['va']['completed'] = completed
        
        # Limit stored points to prevent memory issues
        if len(self.measurement_data_store[board_id]['va']['data']) > 1000:
            self.measurement_data_store[board_id]['va']['data'] = self.measurement_data_store[board_id]['va']['data'][-1000:]

    def _handle_bode_data(self, board_id, payload):
        """Handle Bode plot data (non-continuous measurement mode)"""
        payload_data = payload.get('payload', {})
        data_points = payload_data.get('data', [])
        progress = payload_data.get('progress', 0)
        completed = payload_data.get('completed', False)
        
        print(f"[MQTT] Bode data from {board_id}: {len(data_points)} points, {progress:.1f}% complete")
        
        # Store Bode data in measurement data store (not continuous data store)
        if board_id not in self.measurement_data_store:
            self.measurement_data_store[board_id] = {}
        
        if 'bode' not in self.measurement_data_store[board_id]:
            self.measurement_data_store[board_id]['bode'] = {
                'mode': 'bode',
                'data': [],
                'progress': 0,
                'completed': False
            }
        
        # Accumulate data points from bulk messages
        self.measurement_data_store[board_id]['bode']['data'].extend(data_points)
        self.measurement_data_store[board_id]['bode']['progress'] = progress
        self.measurement_data_store[board_id]['bode']['completed'] = completed

    def _handle_step_data(self, board_id, payload):
        """Handle step response data (non-continuous measurement mode)"""
        payload_data = payload.get('payload', {})
        data_points = payload_data.get('data', [])
        progress = payload_data.get('progress', 0)
        completed = payload_data.get('completed', False)
        
        print(f"[MQTT] Step data from {board_id}: {len(data_points)} points, {progress:.1f}% complete")
        
        # Store step data in measurement data store (not continuous data store)
        if board_id not in self.measurement_data_store:
            self.measurement_data_store[board_id] = {}
        
        if 'step' not in self.measurement_data_store[board_id]:
            self.measurement_data_store[board_id]['step'] = {
                'mode': 'step',
                'data': [],
                'progress': 0,
                'completed': False
            }
        
        # Accumulate data points from bulk messages
        self.measurement_data_store[board_id]['step']['data'].extend(data_points)
        self.measurement_data_store[board_id]['step']['progress'] = progress
        self.measurement_data_store[board_id]['step']['completed'] = completed

    def _handle_impulse_data(self, board_id, payload):
        """Handle impulse response data (non-continuous measurement mode)"""
        payload_data = payload.get('payload', {})
        data_points = payload_data.get('data', [])
        progress = payload_data.get('progress', 0)
        completed = payload_data.get('completed', False)
        
        print(f"[MQTT] Impulse data from {board_id}: {len(data_points)} points, {progress:.1f}% complete")
        
        # Store impulse data in measurement data store (not continuous data store)
        if board_id not in self.measurement_data_store:
            self.measurement_data_store[board_id] = {}
        
        if 'impulse' not in self.measurement_data_store[board_id]:
            self.measurement_data_store[board_id]['impulse'] = {
                'mode': 'impulse',
                'data': [],
                'progress': 0,
                'completed': False
            }
        
        # Accumulate data points from bulk messages
        self.measurement_data_store[board_id]['impulse']['data'].extend(data_points)
        self.measurement_data_store[board_id]['impulse']['progress'] = progress
        self.measurement_data_store[board_id]['impulse']['completed'] = completed

    def _handle_testbed_data(self, board_id, payload):
        """Handle testbed continuous data"""
        payload_data = payload.get('payload', {})
        readings = payload_data.get('readings', {})
        status = payload_data.get('status', 'unknown')
        continuous = payload_data.get('continuous', False)
        
        print(f"[MQTT] Testbed data from {board_id}: V={readings.get('output_voltage', 'N/A')}V, I={readings.get('output_current', 'N/A')}A")
        
        # Store latest testbed readings (overwrite previous)
        self.continuous_data_store[board_id] = {
            'mode': 'testbed',
            'readings': readings,
            'status': status,
            'continuous': continuous,
            'timestamp': payload.get('timestamp')
        }

    def _handle_control_system_data(self, board_id, payload):
        """Handle control system continuous data"""
        payload_data = payload.get('payload', {})
        sample_count = payload_data.get('sample_count', 0)
        continuous = payload_data.get('continuous', False)
        
        print(f"[MQTT] Control system data from {board_id}: {sample_count} samples")
        
        # Store continuous data for real-time updates
        if board_id not in self.continuous_data_store:
            self.continuous_data_store[board_id] = []
        
        # Keep only the latest data points (limit to prevent memory issues)
        self.continuous_data_store[board_id].append(payload)
        if len(self.continuous_data_store[board_id]) > 100:  # Keep last 100 data points
            self.continuous_data_store[board_id] = self.continuous_data_store[board_id][-100:]

    def get_ready_boards(self):
        return list(self.ready_boards)

    def publish_command(self, mode, settings, target_board):
        if not MQTT_AVAILABLE:
            print(f"[MOCK] Would publish command - mode: {mode}, target: {target_board}")
            message_id = str(uuid.uuid4())
            
            # In mock mode, simulate immediate response storage
            mock_response = {
                "timestamp": str(int(time.time() * 1000)),
                "message_id": f"response-{int(time.time() * 1000)}",
                "type": "response",
                "payload": {
                    "mode": mode,
                    "status": "success",
                    "message": f"{mode.upper()} measurement started",
                    "estimated_duration": 10 if mode == 'va' else 5
                }
            }
            
            # Store mock response for both direct and correlation lookup
            self.data_store[message_id] = mock_response
            response_key = f"{target_board}_{mode}"
            self.response_store[response_key] = mock_response
            print(f"[MOCK] Stored mock response for message_id: {message_id} and key: {response_key}")
            
            return message_id
            
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

    def get_response_by_board_mode(self, board_id, mode, timeout=10):
        """Get response by board_id and mode when message_ids don't match"""
        response_key = f"{board_id}_{mode}"
        start_time = time.time()
        while time.time() - start_time < timeout:
            if response_key in self.response_store:
                return self.response_store.pop(response_key, None)
            time.sleep(0.1)
        return None

    def get_continuous_data(self, board_id):
        """Get all continuous data for a board and clear the buffer"""
        data = self.continuous_data_store.get(board_id, [])
        if data:
            self.continuous_data_store[board_id] = []  # Clear after reading
        return data

    def get_latest_continuous_data(self, board_id):
        """Get the latest continuous data point without clearing the buffer"""
        data = self.continuous_data_store.get(board_id, [])
        return data[-1] if data else None

    def get_measurement_data(self, board_id, mode):
        """Get accumulated measurement data for non-continuous modes (va, bode, step, impulse)"""
        board_data = self.measurement_data_store.get(board_id, {})
        if mode in board_data:
            return board_data[mode]
        return None

    def get_testbed_data(self, board_id):
        """Get latest testbed readings"""
        data = self.continuous_data_store.get(board_id, {})
        if isinstance(data, dict) and data.get('mode') == 'testbed':
            return data
        return None

    def get_control_system_data(self, board_id):
        """Get control system data and clear the buffer"""
        data = self.continuous_data_store.get(board_id, [])
        if isinstance(data, list) and data:
            self.continuous_data_store[board_id] = []  # Clear after reading
            return data
        return []

    def clear_continuous_data(self, board_id):
        """Clear continuous data for a specific board"""
        if board_id in self.continuous_data_store:
            # Handle both list format (control_system) and dict format (other modes)
            current_data = self.continuous_data_store[board_id]
            if isinstance(current_data, list):
                self.continuous_data_store[board_id] = []
            elif isinstance(current_data, dict):
                self.continuous_data_store[board_id] = {}
            else:
                # Fallback - just clear it
                self.continuous_data_store[board_id] = []

    def is_measurement_complete(self, board_id, mode):
        """Check if a measurement is complete for modes that have completion status"""
        data = self.continuous_data_store.get(board_id, {})
        if isinstance(data, dict) and data.get('mode') == mode:
            return data.get('completed', False)
        return False

    def is_connected(self):
        """Check if MQTT client is connected"""
        if not MQTT_AVAILABLE:
            return False
        return self.client and self.client.is_connected()

    def flush_all_buffers(self, board_id=None):
        """Flush all data buffers for a specific board or all boards"""
        if board_id:
            # Clear continuous data store for specific board
            if board_id in self.continuous_data_store:
                self.continuous_data_store[board_id] = []
            # Clear measurement data store for specific board
            if board_id in self.measurement_data_store:
                self.measurement_data_store[board_id] = {}
            # Clear response store for this board
            keys_to_remove = [key for key in self.response_store.keys() if key.startswith(f"{board_id}_")]
            for key in keys_to_remove:
                del self.response_store[key]
            print(f"[MQTT] Flushed all buffers for board: {board_id}")
        else:
            # Clear all buffers
            self.continuous_data_store.clear()
            self.measurement_data_store.clear()
            self.response_store.clear()
            print(f"[MQTT] Flushed all buffers for all boards")

    def flush_mode_buffer(self, board_id, mode):
        """Flush buffer for specific board and mode"""
        if mode in ['control_system', 'testbed']:
            # Continuous modes use continuous_data_store
            if mode == 'control_system':
                # Control system uses list format
                if board_id in self.continuous_data_store and isinstance(self.continuous_data_store[board_id], list):
                    self.continuous_data_store[board_id] = []
            elif mode == 'testbed':
                # Testbed uses dict format
                if board_id in self.continuous_data_store:
                    if isinstance(self.continuous_data_store[board_id], dict) and self.continuous_data_store[board_id].get('mode') == mode:
                        self.continuous_data_store[board_id] = {
                            'mode': mode,
                            'readings': {},
                            'continuous': True
                        }
        elif mode in ['va', 'bode', 'step', 'impulse']:
            # Non-continuous modes use measurement_data_store
            if board_id in self.measurement_data_store and mode in self.measurement_data_store[board_id]:
                self.measurement_data_store[board_id][mode] = {
                    'mode': mode,
                    'data': [],
                    'progress': 0,
                    'completed': False
                }
        
        # Clear response store for this board and mode
        response_key = f"{board_id}_{mode}"
        if response_key in self.response_store:
            del self.response_store[response_key]
        
        print(f"[MQTT] Flushed buffer for board: {board_id}, mode: {mode}")
