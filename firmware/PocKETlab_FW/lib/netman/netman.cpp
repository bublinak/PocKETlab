#include "netman.h"

NetMan::NetMan(const char* deviceName, const char* adminPassword) 
    : _deviceName(deviceName), _adminPassword(adminPassword), 
      _configPortalActive(false), _otaEnabled(true),
      _lastConnectionAttempt(0), _currentNetworkIndex(0),
      _currentMode(MODE_STA), _apModeTimeout(0),
      _mdnsEnabled(true), _mdnsServiceName(""),
      _isWebUIUpload(false), _uploadBuffer("") {
    _server = new WebServer(80);
    _dnsServer = new DNSServer();
}

bool NetMan::begin() {
    Serial.println("NetMan: Initializing...");
    
    if (!_initSPIFFS()) {
        Serial.println("NetMan: SPIFFS initialization failed!");
        // Try to format SPIFFS and reinitialize
        Serial.println("NetMan: Attempting to format SPIFFS...");
        if (SPIFFS.format()) {
            Serial.println("NetMan: SPIFFS formatted successfully, retrying initialization...");
            if (!SPIFFS.begin(true)) {
                Serial.println("NetMan: SPIFFS initialization failed after format!");
                return false;
            }
            Serial.println("NetMan: SPIFFS successfully initialized after format");
        } else {
            Serial.println("NetMan: SPIFFS format failed!");
            return false;
        }
    }
      if (!_loadNetworks()) {
        Serial.println("NetMan: No saved networks found");
    }
      // Print diagnostic info
    printSPIFFSInfo();
    testSPIFFSWrite();
    
    // Check if we have web UI files in SPIFFS
    bool hasWebUI = hasWebUIFiles();
      // Try to connect to known networks first
    if (connectToKnownNetwork()) {
        // Connected successfully - always use STA mode when connected to WiFi
        _switchToMode(MODE_STA);
    } else {
        // No connection possible, start AP mode
        if (_knownNetworks.empty()) {
            Serial.println("NetMan: No saved networks found, starting AP mode");
        } else {
            Serial.println("NetMan: Failed to connect to any saved network, starting AP mode");
        }
        if (hasWebUI) {
            _switchToMode(MODE_AP_FULL);
        } else {
            _switchToMode(MODE_AP_BASIC);
        }
    }
    
    Serial.println("NetMan: Initialization complete");
    return true;
}

void NetMan::loop() {
    if (_configPortalActive) {
        _dnsServer->processNextRequest();
    }
    
    _server->handleClient();
    
    // Handle mode-specific logic
    if (_currentMode == MODE_AP_BASIC || _currentMode == MODE_AP_FULL) {
        // Check for AP mode timeout
        if (_apModeTimeout > 0 && millis() > _apModeTimeout) {
            Serial.println("NetMan: AP mode timeout, attempting to reconnect");
            if (connectToKnownNetwork()) {
                // When connected, always switch to STA mode
                _switchToMode(MODE_STA);
            } else {
                _apModeTimeout = millis() + AP_MODE_TIMEOUT; // Reset timeout
            }
        }
    } else {
        // Check WiFi connection every 30 seconds in STA mode
        if (millis() - _lastConnectionAttempt > 30000) {
            if (!isConnected()) {
                Serial.println("NetMan: Connection lost, attempting reconnection...");
                if (!connectToKnownNetwork()) {
                    // Switch to AP mode if connection fails
                    if (hasWebUIFiles()) {
                        _switchToMode(MODE_AP_FULL);
                    } else {
                        _switchToMode(MODE_AP_BASIC);
                    }
                }
            }
            _lastConnectionAttempt = millis();
        }    }
    
    if (_otaEnabled) {
        ArduinoOTA.handle();
    }
}

bool NetMan::addNetwork(const String& ssid, const String& password) {
    Serial.println("NetMan: addNetwork() called for SSID: " + ssid);
    
    // Check if network already exists
    for (size_t idx = 0; idx < _knownNetworks.size(); ++idx) {
        if (_knownNetworks[idx].ssid == ssid) {
            Serial.println("NetMan: Updating existing network");
            _knownNetworks[idx].password = password;
            _knownNetworks[idx].enabled = true;
            // Move this network to the front so it will be tried first even after reboot
            if (idx != 0) {
                WiFiCredentials preferred = _knownNetworks[idx];
                _knownNetworks.erase(_knownNetworks.begin() + idx);
                _knownNetworks.insert(_knownNetworks.begin(), preferred);
            }
            _currentNetworkIndex = 0;
            return _saveNetworks();
        }
    }
    
    // Add new network
    WiFiCredentials newNetwork;
    newNetwork.ssid = ssid;
    newNetwork.password = password;
    newNetwork.enabled = true;
    
    _knownNetworks.push_back(newNetwork);
    // Move newly added network to the front so it will be tried first even after reboot
    if (_knownNetworks.size() > 1) {
        WiFiCredentials preferred = _knownNetworks.back();
        _knownNetworks.pop_back();
        _knownNetworks.insert(_knownNetworks.begin(), preferred);
    }
    _currentNetworkIndex = 0;
    Serial.println("NetMan: Added new network to memory, now saving...");
    return _saveNetworks();
}

bool NetMan::removeNetwork(const String& ssid) {
    for (auto it = _knownNetworks.begin(); it != _knownNetworks.end(); ++it) {
        if (it->ssid == ssid) {
            _knownNetworks.erase(it);
            return _saveNetworks();
        }
    }
    return false;
}

bool NetMan::connectToKnownNetwork() {
    if (_knownNetworks.empty()) {
        return false;
    }
    
    WiFi.mode(WIFI_STA);
    
    for (size_t i = 0; i < _knownNetworks.size(); i++) {
        const auto& network = _knownNetworks[(_currentNetworkIndex + i) % _knownNetworks.size()];
        
        if (!network.enabled) continue;
        
        Serial.print("NetMan: Attempting to connect to ");
        Serial.println(network.ssid);
        
        WiFi.begin(network.ssid.c_str(), network.password.c_str());
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
            delay(100);
        }
          if (WiFi.status() == WL_CONNECTED) {
            Serial.print("NetMan: Connected to ");
            Serial.print(network.ssid);
            Serial.print(" (IP: ");
            Serial.print(WiFi.localIP());
            Serial.println(")");
            
            _currentNetworkIndex = (_currentNetworkIndex + i) % _knownNetworks.size();
            
            // Start mDNS in STA mode
            if (_mdnsEnabled) {
                _startMDNS();
            }
            
            if (_configPortalActive) {
                stopConfigPortal();
            }
            
            return true;
        }
    }
    
    Serial.println("NetMan: Failed to connect to any known network");
    return false;
}

void NetMan::startConfigPortal() {
    if (_configPortalActive) return;
    
    Serial.println("NetMan: Starting configuration portal");
    
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP((_deviceName + "_Config").c_str(), "configure");
    
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    
    _dnsServer->start(53, "*", apIP);
    _configPortalActive = true;
    
    Serial.print("NetMan: Config portal started at ");
    Serial.println(WiFi.softAPIP());
}

void NetMan::stopConfigPortal() {
    if (!_configPortalActive) return;
    
    Serial.println("NetMan: Stopping configuration portal");
    
    _dnsServer->stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _configPortalActive = false;
}

bool NetMan::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String NetMan::getConnectedSSID() {
    return WiFi.SSID();
}

String NetMan::getIPAddress() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    } else if (_configPortalActive) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

int NetMan::getRSSI() {
    return WiFi.RSSI();
}

void NetMan::setDeviceName(const String& name) {
    _deviceName = name;
    WiFi.setHostname(name.c_str());
}

void NetMan::setAdminPassword(const String& password) {
    _adminPassword = password;
}

bool NetMan::isConfigPortalActive() {
    return _configPortalActive;
}

void NetMan::enableOTA(bool enable) {
    _otaEnabled = enable;
    if (enable && isConnected()) {
        ArduinoOTA.setHostname(_deviceName.c_str());
        ArduinoOTA.setPassword(_adminPassword.c_str());
        
        ArduinoOTA.onStart([]() {
            Serial.println("OTA Start");
        });
        ArduinoOTA.onEnd([]() {
            Serial.println("OTA End");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
        });
        
        ArduinoOTA.begin();
    }
}

bool NetMan::isOTAEnabled() {
    return _otaEnabled;
}

void NetMan::enableMDNS(bool enable, const String& serviceName) {
    _mdnsEnabled = enable;
    if (!serviceName.isEmpty()) {
        _mdnsServiceName = serviceName;
    }
    
    if (enable && WiFi.status() == WL_CONNECTED) {
        _startMDNS();
    } else if (!enable) {
        _stopMDNS();
    }
}

bool NetMan::isMDNSEnabled() {
    return _mdnsEnabled;
}

String NetMan::getMDNSName() {
    if (_mdnsServiceName.isEmpty()) {
        return _deviceName;
    }
    return _mdnsServiceName;
}

bool NetMan::_initSPIFFS() {
    return SPIFFS.begin(true);
}

bool NetMan::_loadNetworks() {
    Serial.println("NetMan: _loadNetworks() called");
    
    // Ensure SPIFFS is mounted
    if (!SPIFFS.begin()) {
        Serial.println("NetMan: SPIFFS not mounted, attempting to mount...");
        if (!SPIFFS.begin(true)) {
            Serial.println("NetMan: SPIFFS mount failed");
            return false;
        }
        Serial.println("NetMan: SPIFFS mounted successfully");
    }
    
    Serial.print("NetMan: Looking for networks file: ");
    Serial.println(_networksFilePath);
    
    if (!SPIFFS.exists(_networksFilePath)) {
        Serial.println("NetMan: Networks file does not exist");
        return false;
    }
    
    File file = SPIFFS.open(_networksFilePath, "r");
    if (!file) {
        Serial.println("NetMan: Failed to open networks file for reading");
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.print("NetMan: Networks file size: ");
    Serial.println(fileSize);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("NetMan: Failed to parse networks file: ");
        Serial.println(error.c_str());
        return false;
    }
      _knownNetworks.clear();
    JsonArray networks = doc["networks"];
    
    Serial.print("NetMan: Found ");
    Serial.print(networks.size());
    Serial.println(" networks in file");
    
    for (JsonObject network : networks) {
        WiFiCredentials cred;
        cred.ssid = network["ssid"].as<String>();
        cred.password = network["password"].as<String>();
        cred.enabled = network["enabled"] | true;
        _knownNetworks.push_back(cred);
        Serial.println("NetMan: Loaded network: " + cred.ssid);
    }
    
    Serial.print("NetMan: Loaded ");
    Serial.print(_knownNetworks.size());
    Serial.println(" networks from SPIFFS");
    
    return true;
}

bool NetMan::_saveNetworks() {
    Serial.println("NetMan: _saveNetworks() called");
    
    // Ensure SPIFFS is mounted
    if (!SPIFFS.begin()) {
        Serial.println("NetMan: SPIFFS not mounted, attempting to mount...");
        if (!SPIFFS.begin(true)) {
            Serial.println("NetMan: SPIFFS mount failed");
            return false;
        }
        Serial.println("NetMan: SPIFFS mounted successfully");
    }
    
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    
    for (const auto& network : _knownNetworks) {
        JsonObject obj = networks.add<JsonObject>();
        obj["ssid"] = network.ssid;
        obj["password"] = network.password;
        obj["enabled"] = network.enabled;
        Serial.println("NetMan: Adding network to save: " + network.ssid);    }
    
    File file = SPIFFS.open(_networksFilePath, "w");
    if (!file) {
        Serial.println("NetMan: Failed to open networks file for writing: " + _networksFilePath);
        
        // Try to list the root directory to see what's there
        Serial.println("NetMan: Listing root directory...");
        listSPIFFSFiles();
        
        // Try to create the file with a different approach
        Serial.println("NetMan: Attempting to create file...");
        File testFile = SPIFFS.open(_networksFilePath, "w", true);
        if (!testFile) {
            Serial.println("NetMan: Still cannot create file!");
            return false;
        }
        testFile.close();
        
        // Try to open again
        file = SPIFFS.open(_networksFilePath, "w");
        if (!file) {
            Serial.println("NetMan: File creation failed completely");
            return false;
        }
        Serial.println("NetMan: File created successfully on second attempt");
    }
    
    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    Serial.print("NetMan: Saved ");
    Serial.print(_knownNetworks.size());
    Serial.print(" networks to SPIFFS, bytes written: ");
    Serial.println(bytesWritten);
    
    // Verify the file was written
    if (SPIFFS.exists(_networksFilePath)) {
        File testFile = SPIFFS.open(_networksFilePath, "r");
        if (testFile) {
            size_t fileSize = testFile.size();
            testFile.close();
            Serial.print("NetMan: Verified file exists, size: ");
            Serial.println(fileSize);
        }
    } else {
        Serial.println("NetMan: ERROR - File does not exist after save!");
    }
    
    return bytesWritten > 0;
}

void NetMan::_setupWebServer() {
    Serial.print("NetMan: Setting up web server for mode ");
    Serial.println(_currentMode);
    
    switch (_currentMode) {
        case MODE_STA:
            // In STA mode, use full UI if available, otherwise basic UI
            if (hasWebUIFiles()) {
                Serial.println("NetMan: Using full web server (STA mode, web UI files available)");
                _setupFullWebServer();
            } else {
                Serial.println("NetMan: Using basic web server (STA mode, no web UI files)");
                _setupBasicWebServer();
            }
            break;
        case MODE_AP_BASIC:
            Serial.println("NetMan: Using basic web server (AP mode)");
            _setupBasicWebServer();
            break;
        case MODE_AP_FULL:
            Serial.println("NetMan: Using full web server (AP mode)");
            _setupFullWebServer();
            break;
    }
    
    _server->begin();
    Serial.println("NetMan: Web server started");
}

void NetMan::_handleRoot() {
    String content = R"HTML(
<div class="container">
    <h1>)HTML" + _deviceName + R"HTML( Network Manager</h1>
    <div class="status-card">
        <h3>Connection Status</h3>
        <p id="status">Loading...</p>
        <p id="ip">Loading...</p>
        <p id="rssi">Loading...</p>
    </div>
    
    <div class="card">
        <h3>Quick Actions</h3>
        <button onclick="window.location.href='/networks'">Manage Networks</button>
        <button onclick="scanNetworks()">Scan Networks</button>
        <button onclick="window.location.href='/ota'">Firmware Update</button>
    </div>
    
    <div class="card">
        <h3>Add New Network</h3>
        <form onsubmit="addNetwork(event)">
            <input type="text" id="ssid" placeholder="SSID" required>
            <input type="password" id="password" placeholder="Password" required>
            <button type="submit">Add Network</button>
        </form>
    </div>
    
    <div class="card" id="scanResults" style="display:none;">
        <h3>Available Networks</h3>
        <div id="networks"></div>
    </div>
</div>

<script>
async function updateStatus() {
    try {
        const response = await fetch('/status');
        const data = await response.json();
        document.getElementById('status').textContent = data.connected ? 'Connected to ' + data.connectedSSID : 'Disconnected';
        document.getElementById('ip').textContent = 'IP: ' + data.ipAddress;
        document.getElementById('rssi').textContent = data.connected ? 'Signal: ' + data.rssi + ' dBm' : '';
    } catch (e) {
        console.error('Failed to update status:', e);
    }
}

async function addNetwork(event) {
    event.preventDefault();
    const ssid = document.getElementById('ssid').value;
    const password = document.getElementById('password').value;
    
    try {
        const response = await fetch('/addnetwork', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)
        });
        
        if (response.ok) {
            alert('Network added successfully!');
            document.getElementById('ssid').value = '';
            document.getElementById('password').value = '';
            updateStatus();
        } else {
            alert('Failed to add network');
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}

async function scanNetworks() {
    try {
        document.getElementById('networks').innerHTML = 'Scanning...';
        document.getElementById('scanResults').style.display = 'block';
        
        const response = await fetch('/scan');
        const data = await response.json();
        
        let html = '';
        data.networks.forEach(network => {
            html += '<div class="network-item">';
            html += '<span>' + network.ssid + ' (' + network.rssi + ' dBm)</span>';
            html += '<button onclick="addNetworkFromScan(\'' + network.ssid + '\')">' + 
                   (network.encrypted ? 'Add' : 'Connect') + '</button>';
            html += '</div>';
        });
        
        document.getElementById('networks').innerHTML = html;
    } catch (e) {
        document.getElementById('networks').innerHTML = 'Scan failed: ' + e.message;
    }
}

function addNetworkFromScan(ssid) {
    document.getElementById('ssid').value = ssid;
    document.getElementById('password').focus();
}

updateStatus();
setInterval(updateStatus, 10000);
</script>
)HTML";

    _server->send(200, "text/html", _generateHTML("Home", content));
}

void NetMan::_handleNetworks() {
    if (!_isAuthenticated()) {
        _server->sendHeader("Location", "/auth?returnTo=%2Fnetworks");
        _server->send(302);
        return;
    }
    
    String content = R"HTML(
<div class="container">
    <h1>Saved Networks</h1>
    <div class="card">
        <div id="networkList">Loading...</div>
    </div>
    <button onclick="window.location.href='/'">Back to Home</button>
</div>

<script>
async function loadNetworks() {
    try {
        const response = await fetch('/api/networks');
        const data = await response.json();
        
        let html = '';
        data.networks.forEach(network => {
            html += '<div class="network-item">';
            html += '<span>' + network.ssid + '</span>';
            html += '<button onclick="removeNetwork(\'' + network.ssid + '\')">Remove</button>';
            html += '</div>';
        });
        
        if (html === '') {
            html = '<p>No saved networks</p>';
        }
        
        document.getElementById('networkList').innerHTML = html;
    } catch (e) {
        document.getElementById('networkList').innerHTML = 'Failed to load networks';
    }
}

async function removeNetwork(ssid) {
    if (!confirm('Remove network "' + ssid + '"?')) return;
    
    try {
        const response = await fetch('/removenetwork', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'ssid=' + encodeURIComponent(ssid)
        });
        
        if (response.ok) {
            loadNetworks();
        } else {
            alert('Failed to remove network');
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}

loadNetworks();
</script>
)HTML";

    _server->send(200, "text/html", _generateHTML("Networks", content));
}

void NetMan::_handleAddNetwork() {
    String ssid, password;
    
    // Handle both form data and JSON
    if (_server->hasArg("plain")) {
        // JSON request
        JsonDocument doc;
        deserializeJson(doc, _server->arg("plain"));
        ssid = doc["ssid"].as<String>();
        password = doc["password"].as<String>();
    } else if (_server->hasArg("ssid")) {
        // Form data request
        ssid = _server->arg("ssid");
        password = _server->arg("password");
    } else {
        _server->send(400, "application/json", "{\"success\":false,\"message\":\"Missing SSID\"}");
        return;
    }
    
    if (addNetwork(ssid, password)) {
        _server->send(200, "application/json", "{\"success\":true,\"message\":\"Network added successfully\"}");
        
        // Try to connect to the new network immediately and switch to STA on success
        if (!isConnected()) {
            if (connectToKnownNetwork()) {
                _switchToMode(MODE_STA);
            }
        }
    } else {
        _server->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to add network\"}");
    }
}

void NetMan::_handleRemoveNetwork() {
    String ssid;
    
    // Handle URL parameter (for DELETE requests)
    if (_server->hasArg("ssid")) {
        ssid = _server->arg("ssid");
    } else {
        _server->send(400, "application/json", "{\"success\":false,\"message\":\"Missing SSID\"}");
        return;
    }
    
    if (removeNetwork(ssid)) {
        _server->send(200, "application/json", "{\"success\":true,\"message\":\"Network removed successfully\"}");
    } else {
        _server->send(404, "application/json", "{\"success\":false,\"message\":\"Network not found\"}");
    }
}

void NetMan::_handleScan() {
    _server->send(200, "application/json", _getScanResultsJSON());
}

void NetMan::_handleStatus() {
    JsonDocument doc;
    doc["connected"] = isConnected();
    doc["connectedSSID"] = getConnectedSSID();
    doc["ipAddress"] = getIPAddress();
    doc["rssi"] = getRSSI();
    doc["configPortal"] = isConfigPortalActive();
    doc["deviceName"] = _deviceName;
    doc["macAddress"] = WiFi.macAddress();
    doc["uptime"] = String(millis() / 1000) + " seconds";
    doc["mode"] = _currentMode;
    
    String response;
    serializeJson(doc, response);
    _server->send(200, "application/json", response);
}

void NetMan::_handleReboot() {
    _server->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
    delay(1000);
    ESP.restart();
}

void NetMan::_handleOTA() {
    if (!_isAuthenticated()) {
        _server->sendHeader("Location", "/auth?returnTo=%2Fota");
        _server->send(302);
        return;
    }
    
    String content = R"(
<div class="container">
    <h1>Firmware Update</h1>
    <div class="card">
        <h3>Upload New Firmware</h3>
        <form method="POST" action="/upload" enctype="multipart/form-data">
            <input type="file" name="firmware" accept=".bin" required>
            <button type="submit">Upload</button>
        </form>
        <div id="progress" style="display:none;">
            <div class="progress-bar">
                <div id="progressBar"></div>
            </div>
            <p id="progressText">0%</p>
        </div>
    </div>
    <button onclick="window.location.href='/'">Back to Home</button>
</div>

<script>
document.querySelector('form').addEventListener('submit', function(e) {
    document.getElementById('progress').style.display = 'block';
    // Progress tracking would need additional implementation
});
</script>
)";

    _server->send(200, "text/html", _generateHTML("Firmware Update", content));
}

void NetMan::_handleOTAUpload() {
    HTTPUpload& upload = _server->upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("NetMan: Upload Start: %s\n", upload.filename.c_str());
        
        // Check if this is a ZIP file for web UI
        if (upload.filename.endsWith(".zip")) {
            Serial.println("NetMan: ZIP file detected, preparing for web UI extraction");
            _isWebUIUpload = true;
            _uploadBuffer = "";
            return;
        }
        
        // Otherwise, handle as firmware upload
        _isWebUIUpload = false;
        String filename = upload.filename;
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        Serial.printf("NetMan: Update Start: %s\n", filename.c_str());
        
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_isWebUIUpload) {
            // Accumulate ZIP data
            for (size_t i = 0; i < upload.currentSize; i++) {
                _uploadBuffer += (char)upload.buf[i];
            }
            Serial.printf("NetMan: ZIP data accumulated: %d bytes total\n", _uploadBuffer.length());
        } else {
            // Write firmware data
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_isWebUIUpload) {
            Serial.printf("NetMan: ZIP upload complete, size: %d bytes\n", _uploadBuffer.length());
            _extractWebUIFromBuffer();
        } else {
            if (Update.end(true)) {
                Serial.printf("NetMan: Update Success: %u bytes\nRebooting...\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    }
}

void NetMan::_extractWebUIFromBuffer() {
    Serial.println("NetMan: Starting ZIP extraction...");
    
    if (_uploadBuffer.length() < 30) {
        Serial.println("NetMan: Error - ZIP file too small");
        return;
    }
    
    // Simple ZIP extraction - look for local file headers (PK\003\004)
    size_t pos = 0;
    int filesExtracted = 0;
    
    while (pos < _uploadBuffer.length() - 30) {
        // Look for local file header signature
        if (_uploadBuffer[pos] == 'P' && _uploadBuffer[pos+1] == 'K' && 
            _uploadBuffer[pos+2] == 0x03 && _uploadBuffer[pos+3] == 0x04) {
            
            Serial.printf("NetMan: Found file header at position %d\n", pos);
            
            // Extract file info from header
            uint16_t filenameLength = (_uploadBuffer[pos+27] << 8) | _uploadBuffer[pos+26];
            uint16_t extraFieldLength = (_uploadBuffer[pos+29] << 8) | _uploadBuffer[pos+28];
            uint32_t compressedSize = (_uploadBuffer[pos+21] << 24) | (_uploadBuffer[pos+20] << 16) | 
                                     (_uploadBuffer[pos+19] << 8) | _uploadBuffer[pos+18];
            uint16_t compressionMethod = (_uploadBuffer[pos+9] << 8) | _uploadBuffer[pos+8];
            
            // Only handle uncompressed files (method 0)
            if (compressionMethod != 0) {
                Serial.println("NetMan: Skipping compressed file (not supported)");
                pos += 30 + filenameLength + extraFieldLength + compressedSize;
                continue;
            }
            
            // Extract filename
            String filename = "";
            for (int i = 0; i < filenameLength; i++) {
                filename += _uploadBuffer[pos + 30 + i];
            }
            
            // Skip directories
            if (filename.endsWith("/")) {
                Serial.println("NetMan: Skipping directory: " + filename);
                pos += 30 + filenameLength + extraFieldLength + compressedSize;
                continue;
            }
            
            // Add leading slash if not present
            if (!filename.startsWith("/")) {
                filename = "/" + filename;
            }
            
            Serial.printf("NetMan: Extracting file: %s (size: %d bytes)\n", filename.c_str(), compressedSize);
            
            // Extract file data
            size_t dataStart = pos + 30 + filenameLength + extraFieldLength;
            if (dataStart + compressedSize <= _uploadBuffer.length()) {
                // Remove existing file
                if (SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                }
                
                // Write new file
                File file = SPIFFS.open(filename, "w");
                if (file) {
                    for (uint32_t i = 0; i < compressedSize; i++) {
                        file.write(_uploadBuffer[dataStart + i]);
                    }
                    file.close();
                    filesExtracted++;
                    Serial.printf("NetMan: Successfully extracted: %s\n", filename.c_str());
                } else {
                    Serial.printf("NetMan: Error - Could not create file: %s\n", filename.c_str());
                }
            } else {
                Serial.println("NetMan: Error - File data extends beyond buffer");
            }
            
            pos += 30 + filenameLength + extraFieldLength + compressedSize;
        } else {
            pos++;
        }
    }
    
    Serial.printf("NetMan: ZIP extraction complete. Extracted %d files\n", filesExtracted);
    
    // Clear the buffer to free memory
    _uploadBuffer = "";
    
    // Print SPIFFS info after extraction
    printSPIFFSInfo();
}

String NetMan::_getScanResultsJSON() {
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    
    int n = WiFi.scanNetworks();
    
    if (n == 0) {
        doc["message"] = "No networks found";
    } else {
        for (int i = 0; i < n; i++) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            
            // Add signal quality percentage
            int rssi = WiFi.RSSI(i);
            int quality = 0;
            if (rssi <= -100) {
                quality = 0;
            } else if (rssi >= -50) {
                quality = 100;
            } else {
                quality = 2 * (rssi + 100);
            }
            network["quality"] = quality;
            
            // Add encryption type string
            wifi_auth_mode_t encryption = WiFi.encryptionType(i);
            switch (encryption) {
                case WIFI_AUTH_OPEN:
                    network["encryption"] = "Open";
                    break;
                case WIFI_AUTH_WEP:
                    network["encryption"] = "WEP";
                    break;
                case WIFI_AUTH_WPA_PSK:
                    network["encryption"] = "WPA";
                    break;
                case WIFI_AUTH_WPA2_PSK:
                    network["encryption"] = "WPA2";
                    break;
                case WIFI_AUTH_WPA_WPA2_PSK:
                    network["encryption"] = "WPA/WPA2";
                    break;
                case WIFI_AUTH_WPA2_ENTERPRISE:
                    network["encryption"] = "WPA2-Enterprise";
                    break;
                default:
                    network["encryption"] = "Unknown";
                    break;
            }
        }    }
    
    WiFi.scanDelete();
    
    // Add connection status for basic interface
    doc["connected"] = isConnected();
    doc["ssid"] = getConnectedSSID();
    doc["ip"] = getIPAddress();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String NetMan::_getNetworksJSON() {
    JsonDocument doc;
    JsonArray nets = doc["networks"].to<JsonArray>();

    for (const auto& network : _knownNetworks) {
        JsonObject obj = nets.add<JsonObject>();
        obj["ssid"] = network.ssid;
        obj["enabled"] = network.enabled;
    }

    doc["count"] = _knownNetworks.size();

    String result;
    serializeJson(doc, result);
    return result;
}

String NetMan::_getDetailedStatus() {
    JsonDocument doc;
    
    // Network status
    doc["network"]["connected"] = isConnected();
    doc["network"]["ssid"] = getConnectedSSID();
    doc["network"]["ip"] = getIPAddress();
    doc["network"]["rssi"] = getRSSI();
    doc["network"]["gateway"] = WiFi.gatewayIP().toString();
    doc["network"]["dns"] = WiFi.dnsIP().toString();
    doc["network"]["subnet"] = WiFi.subnetMask().toString();
    
    // Device information
    doc["device"]["name"] = _deviceName;
    doc["device"]["mac"] = WiFi.macAddress();
    doc["device"]["mode"] = _currentMode;
    doc["device"]["configPortal"] = _configPortalActive;
    doc["device"]["otaEnabled"] = _otaEnabled;
    
    // System information
    doc["system"]["uptime"] = millis() / 1000;
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["chipModel"] = ESP.getChipModel();
    doc["system"]["chipRevision"] = ESP.getChipRevision();
    doc["system"]["cpuFreq"] = ESP.getCpuFreqMHz();
    doc["system"]["flashSize"] = ESP.getFlashChipSize();
    
    // SPIFFS information
    doc["storage"]["total"] = SPIFFS.totalBytes();
    doc["storage"]["used"] = SPIFFS.usedBytes();
    doc["storage"]["free"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    doc["storage"]["hasWebUI"] = hasWebUIFiles();
    
    // Network list
    JsonArray savedNetworks = doc["savedNetworks"].to<JsonArray>();
    for (const auto& network : _knownNetworks) {
        JsonObject net = savedNetworks.add<JsonObject>();
        net["ssid"] = network.ssid;
        net["enabled"] = network.enabled;
        // Don't include password for security
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

// ========== ADDITIONAL METHOD IMPLEMENTATIONS ==========

void NetMan::_handleSettings() {
    if (!_isAuthenticated()) {
        _server->send(401, "application/json", "{\"success\":false,\"message\":\"Authentication required\"}");
        return;
    }
    
    if (_server->method() == HTTP_POST) {
        // Save settings
        if (_server->hasArg("plain")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, _server->arg("plain"));
            
            if (error) {
                _server->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }
            
            if (_saveSettings(doc)) {
                _server->send(200, "application/json", "{\"success\":true,\"message\":\"Settings saved\"}");
            } else {
                _server->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save settings\"}");
            }
        } else {
            _server->send(400, "application/json", "{\"success\":false,\"message\":\"No data provided\"}");
        }
    } else {
        // Get settings
        JsonDocument doc;
        if (_loadSettings(doc)) {
            String response;
            serializeJson(doc, response);
            _server->send(200, "application/json", response);
        } else {
            _server->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to load settings\"}");
        }
    }
}

void NetMan::_handleFactoryReset() {
    if (!_isAuthenticated()) {
        _server->send(401, "application/json", "{\"success\":false,\"message\":\"Authentication required\"}");
        return;
    }
    
    Serial.println("NetMan: Performing factory reset");
    
    // Remove all saved networks
    _knownNetworks.clear();
    _saveNetworks();
    
    // Remove settings file
    if (SPIFFS.exists("/settings.json")) {
        SPIFFS.remove("/settings.json");
    }
    
    // Remove web UI files
    _removeWebUIFiles();
    
    _server->send(200, "application/json", "{\"success\":true,\"message\":\"Factory reset complete. Device will reboot.\"}");
    delay(2000);
    ESP.restart();
}

bool NetMan::hasWebUIFiles() {
    return SPIFFS.exists("/index.html") && SPIFFS.exists("/style.css");
}

NetManMode NetMan::getCurrentMode() {
    return _currentMode;
}

NetManMode NetMan::setMode(NetManMode mode) {
    if (mode == _currentMode) {
        Serial.println("NetMan: Already in requested mode");
        return _currentMode;
    }

    _switchToMode(mode);
    return mode;
}

void NetMan::_switchToMode(NetManMode mode) {
    bool modeChanged = (_currentMode != mode);
    
    if (modeChanged) {
        Serial.print("NetMan: Switching to mode ");
        Serial.println(mode);
        
        // Clean up current mode
        if (_server) {
            _server->close();
        }
        if (_dnsServer) {
            _dnsServer->stop();
            delete _dnsServer;
            _dnsServer = nullptr;
        }
        
        // Stop mDNS when switching away from STA mode (but not when going TO STA mode)
        if (_currentMode == MODE_STA && mode != MODE_STA) {
            _stopMDNS();
        }
        
        _currentMode = mode;
        
        switch (mode) {
            case MODE_STA:
                _setupSTAMode();
                break;
            case MODE_AP_BASIC:
                _setupAPMode(true);
                break;
            case MODE_AP_FULL:
                _setupAPMode(false);
                break;
        }
    } else {
        Serial.print("NetMan: Already in mode ");
        Serial.print(mode);
        Serial.println(", ensuring web server is running");
    }
    
    // Always set up web server, regardless of whether mode changed
    _setupWebServer();
}

void NetMan::_setupSTAMode() {
    WiFi.mode(WIFI_STA);
    _configPortalActive = false;
    
    // Start mDNS service for device discovery
    _startMDNS();
}

void NetMan::_startMDNS() {
    if (!_mdnsEnabled || WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    String hostname = _mdnsServiceName.isEmpty() ? _deviceName : _mdnsServiceName;
    hostname.toLowerCase();
    
    // Stop any existing mDNS service
    MDNS.end();
    
    if (MDNS.begin(hostname.c_str())) {
        Serial.println("NetMan: mDNS started as " + hostname + ".local");
        
        // Add service advertising
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "device", _deviceName);
        MDNS.addServiceTxt("http", "tcp", "version", "1.0");
        
        // Add additional service if OTA is enabled
        if (_otaEnabled) {
            MDNS.addService("arduino", "tcp", 3232);
            MDNS.addServiceTxt("arduino", "tcp", "board", "esp32s3");
            MDNS.addServiceTxt("arduino", "tcp", "version", "1.0");
        }
    } else {
        Serial.println("NetMan: mDNS failed to start");
    }
}

void NetMan::_stopMDNS() {
    MDNS.end();
    Serial.println("NetMan: mDNS stopped");
}

void NetMan::_setupAPMode(bool basicMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_deviceName.c_str(), _adminPassword.c_str());
    
    _configPortalActive = true;
    _apModeTimeout = millis() + AP_MODE_TIMEOUT;
    
    // Setup captive portal DNS
    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", WiFi.softAPIP());    
    Serial.print("NetMan: AP Mode started. IP: ");
    Serial.println(WiFi.softAPIP());
}

void NetMan::_setupBasicWebServer() {
    _server->on("/", HTTP_GET, [this]() { _handleBasicRoot(); });
    _server->on("/configure", HTTP_POST, [this]() { _handleBasicConfigure(); });
    _server->on("/upload", HTTP_POST, [this]() { _handleBasicUpload(); }, [this]() { _handleOTAUpload(); });
    _server->on("/scan", HTTP_GET, [this]() { _handleBasicScan(); });
    _server->on("/status", HTTP_GET, [this]() { _handleStatus(); });
    _server->on("/reboot", HTTP_POST, [this]() { _handleBasicReboot(); });
    _server->onNotFound([this]() { _handleNotFound(); });
}

void NetMan::_setupFullWebServer() {
    _server->on("/", HTTP_GET, [this]() { _handleRoot(); });
    _server->on("/networks", HTTP_GET, [this]() { _handleNetworks(); });
    // JSON API endpoint used by the Networks page to list saved networks
    _server->on("/api/networks", HTTP_GET, [this]() {
        _server->send(200, "application/json", _getNetworksJSON());
    });
    _server->on("/addnetwork", HTTP_POST, [this]() { _handleAddNetwork(); });
    _server->on("/removenetwork", HTTP_POST, [this]() { _handleRemoveNetwork(); });
    _server->on("/scan", HTTP_GET, [this]() { _handleScan(); });
    _server->on("/status", HTTP_GET, [this]() { _handleStatus(); });
    _server->on("/settings", HTTP_ANY, [this]() { _handleSettings(); });
    _server->on("/factory-reset", HTTP_POST, [this]() { _handleFactoryReset(); });
    _server->on("/reboot", HTTP_POST, [this]() { _handleReboot(); });
    _server->on("/ota", HTTP_POST, [this]() { _handleOTA(); }, [this]() { _handleOTAUpload(); });
    _server->on("/upload", HTTP_POST, [this]() { _handleWebUIUpload(); }, [this]() { _handleOTAUpload(); });
    _server->on("/auth", HTTP_ANY, [this]() { _handleAuth(); });
    _server->on("/login", HTTP_ANY, [this]() { 
        _server->sendHeader("Location", "/auth"); 
        _server->send(302); 
    });
    _server->onNotFound([this]() { _handleNotFound(); });
}

void NetMan::_handleBasicRoot() {
    Serial.println("NetMan: Serving basic HTML interface");
    String html = _generateBasicHTML();
    _server->send(200, "text/html", html);
}

void NetMan::_handleBasicConfigure() {
    Serial.println("NetMan: _handleBasicConfigure() called");
    
    String ssid = _server->arg("ssid");
    String password = _server->arg("password");
    
    Serial.println("NetMan: Received SSID: '" + ssid + "'");
    Serial.println("NetMan: Received password length: " + String(password.length()));
    
    if (ssid.length() > 0) {
        Serial.println("NetMan: Calling addNetwork()...");
        bool success = addNetwork(ssid, password);
        Serial.println("NetMan: addNetwork() returned: " + String(success ? "true" : "false"));
        
        _server->send(200, "text/plain", "Network configured. Rebooting...");
        delay(2000);
        ESP.restart();
    } else {
        Serial.println("NetMan: ERROR - Empty SSID received");
        _server->send(400, "text/plain", "Invalid network configuration");
    }
}

void NetMan::_handleBasicUpload() {
    _server->send(200, "text/plain", "Upload complete. Web interface updated. Please refresh the page in a few seconds.");
    
    // Small delay to ensure response is sent
    delay(500);
    
    // Restart the web server to use the new files
    _server->close();
    delay(100);
    _setupWebServer();
    
    Serial.println("NetMan: Web interface updated, server restarted");
}

void NetMan::_handleBasicScan() {
    _server->send(200, "application/json", _getScanResultsJSON());
}

void NetMan::_handleBasicReboot() {
    _server->send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart();
}

void NetMan::_handleNotFound() {
    if (_configPortalActive) {
        // In AP mode, redirect to home for captive portal
        _handleRoot();
    } else {
        _server->send(404, "text/plain", "Not Found");
    }
}

void NetMan::_handleAuth() {
    if (_server->method() == HTTP_POST) {
        String password = _server->arg("password");
        
        if (password == _adminPassword) {
            _setAuthCookie();
            _server->send(200, "application/json", "{\"success\":true,\"message\":\"Authenticated\"}");
        } else {
            _server->send(401, "application/json", "{\"success\":false,\"message\":\"Invalid password\"}");
        }
    } else {
        // Show login form
        String content = R"(
<div class='container'>
    <h1>Login Required</h1>
    <div class='card'>
        <form onsubmit='login(event)'>
            <input type='password' id='password' placeholder='Admin Password' required>
            <button type='submit'>Login</button>
        </form>
    </div>
</div>

<script>
async function login(event) {
    event.preventDefault();
    const password = document.getElementById('password').value;
    
    try {
        const response = await fetch('/auth', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'password=' + encodeURIComponent(password)
        });
        
        const data = await response.json();
        if (data.success) {
            const params = new URLSearchParams(window.location.search);
            const returnTo = params.get('returnTo') || '/';
            window.location.href = returnTo;
        } else {
            alert('Invalid password');
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}
</script>
)";
        
        _server->send(401, "text/html", _generateHTML("Login", content));
    }
}

String NetMan::_generateHTML(const String& title, const String& content) {
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" + title + R"(</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin: 20px 0; }
        h1, h2, h3 { color: #333; }
        button { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; }
        button:hover { background: #0056b3; }
        input, select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; margin: 5px; }
        .status-card { background: #e8f5e8; }
        .network-item { border: 1px solid #ddd; padding: 10px; margin: 5px 0; border-radius: 4px; }
    </style>
</head>
<body>
    )" + content + R"(
</body>
</html>
)";
    return html;
}

String NetMan::_generateBasicHTML() {
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)HTML" + _deviceName + R"HTML( Setup</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 600px; margin: 0 auto; }
        .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin: 20px 0; }
        h1, h2 { color: #333; text-align: center; }
        button { background: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; width: 100%; margin: 10px 0; }
        button:hover { background: #0056b3; }
        input, select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; margin: 5px 0; width: 100%; box-sizing: border-box; }
        .network-item { border: 1px solid #ddd; padding: 10px; margin: 5px 0; border-radius: 4px; cursor: pointer; }
        .network-item:hover { background: #f0f0f0; }
    </style>
</head>
<body>    <div class="container">
        <h1>)HTML" + _deviceName + R"HTML( Setup</h1>
        
        <div class="card">
            <h2>Device Status</h2>
            <p><strong>Connection:</strong> <span id="status">Loading...</span></p>
            <p><strong>IP Address:</strong> <span id="ip">Loading...</span></p>
            <p><strong>mDNS Name:</strong> <span id="mdns">)HTML" + getMDNSName() + R"HTML(</span></p>
            <p><strong>Web UI:</strong> <span id="webui">)HTML" + (hasWebUIFiles() ? "Full UI Available" : "Basic UI Only") + R"HTML(</span></p>
        </div>
        
        <div class="card">
            <h2>WiFi Configuration</h2>
            <form onsubmit="saveNetwork(event)">
                <input type="text" id="ssid" placeholder="Network Name (SSID)" required>
                <input type="password" id="password" placeholder="Password">
                <button type="submit">Connect</button>
            </form>
            <button onclick="scanNetworks()">Scan Networks</button>
            <div id="networks"></div>
        </div>
        
        <div class="card">
            <h2>Web Interface Upload</h2>
            <p><strong>Upload a ZIP file containing the full web interface (index.html, style.css, app.js, etc.)</strong></p>
            <form onsubmit="uploadFile(event)" enctype="multipart/form-data">
                <input type="file" id="zipfile" accept=".zip" required>
                <button type="submit">Upload Web Interface</button>
            </form>
        </div>
        
        <div class="card">
            <button onclick="reboot()">Reboot Device</button>
        </div>
    </div>
    
    <script>
        function saveNetwork(event) {
            event.preventDefault();
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            
            fetch('/configure', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)
            }).then(() => {
                alert('Network configured. Device will reboot.');
            });
        }
        
        function scanNetworks() {
            fetch('/scan').then(r => r.json()).then(data => {
                const div = document.getElementById('networks');
                div.innerHTML = '';
                data.networks.forEach(net => {
                    const item = document.createElement('div');
                    item.className = 'network-item';
                    item.innerHTML = net.ssid + ' (' + net.rssi + 'dBm) ' + (net.encrypted ? '🔒' : '');
                    item.onclick = () => document.getElementById('ssid').value = net.ssid;
                    div.appendChild(item);
                });
            });
        }
          function uploadFile(event) {
            event.preventDefault();
            const file = document.getElementById('zipfile').files[0];
            if (file) {
                if (!file.name.toLowerCase().endsWith('.zip')) {
                    alert('Please select a ZIP file');
                    return;
                }
                
                const button = event.target.querySelector('button[type="submit"]');
                const originalText = button.textContent;
                button.textContent = 'Uploading...';
                button.disabled = true;
                
                const formData = new FormData();
                formData.append('file', file);
                
                fetch('/upload', {method: 'POST', body: formData})
                .then(response => {
                    if (response.ok) {
                        alert('Upload complete. Web interface updated. Please refresh the page.');
                        setTimeout(() => window.location.reload(), 2000);
                    } else {
                        alert('Upload failed. Please try again.');
                    }
                })
                .catch(error => {
                    console.error('Upload error:', error);
                    alert('Upload failed. Please try again.');
                })
                .finally(() => {
                    button.textContent = originalText;
                    button.disabled = false;
                });
            }
        }
          function reboot() {
            if (confirm('Reboot device?')) {
                fetch('/reboot', {method: 'POST'});
            }
        }        function updateStatus() {
            fetch('/status').then(r => r.json()).then(data => {
                if (data.connected) {
                    document.getElementById('status').textContent = 'Connected to ' + data.connectedSSID;
                    document.getElementById('ip').textContent = data.ipAddress;
                } else {
                    document.getElementById('status').textContent = 'Not connected to WiFi';
                    document.getElementById('ip').textContent = 'AP Mode: )HTML" + WiFi.softAPIP().toString() + R"HTML(';
                }
            }).catch(err => {
                console.error('Status update failed:', err);
                document.getElementById('status').textContent = 'Status unavailable';
                document.getElementById('ip').textContent = 'Unknown';
            });
        }
        
        // Update status on page load
        window.onload = updateStatus;
    </script>
</body>
</html>
)HTML";
    return html;
}

void NetMan::_handleWebUIUpload() {
    if (!_isAuthenticated()) {
        _server->sendHeader("Location", "/auth");
        _server->send(302);
        return;
    }
    
    // Send success response for WebUI uploads
    _server->send(200, "application/json", "{\"success\":true,\"message\":\"Web interface updated successfully. Refreshing page...\"}");
    
    Serial.println("NetMan: WebUI upload completed in full mode");
    
    // Small delay to ensure response is sent
    delay(500);
    
    // Check if we need to switch modes based on available files
    if (hasWebUIFiles()) {
        Serial.println("NetMan: WebUI files available, staying in full mode");
    } else {
        Serial.println("NetMan: No WebUI files found, may need to reload");
    }
}

bool NetMan::_isAuthenticated() {
    String authCookie = _getAuthCookie();
    return authCookie.startsWith("authenticated_");
}

String NetMan::_getAuthCookie() {
    if (_server->hasHeader("Cookie")) {
        String cookie = _server->header("Cookie");
        int start = cookie.indexOf("auth=");
        if (start != -1) {
            start += 5;
            int end = cookie.indexOf(";", start);
            if (end == -1) end = cookie.length();
            return cookie.substring(start, end);
        }
    }
    return "";
}

void NetMan::_setAuthCookie() {
    String token = "authenticated_" + String(millis());
    _server->sendHeader("Set-Cookie", "auth=" + token + "; Max-Age=3600; Path=/");
}

void NetMan::_clearAuthCookie() {
    _server->sendHeader("Set-Cookie", "auth=; Max-Age=0; Path=/");
}

bool NetMan::_saveSettings(const JsonDocument& settings) {
    File file = SPIFFS.open("/settings.json", "w");
    if (!file) {
        Serial.println("NetMan: Failed to open settings file for writing");
        return false;
    }
    
    size_t bytesWritten = serializeJson(settings, file);
    file.close();
    
    Serial.println("NetMan: Settings saved");
    return bytesWritten > 0;
}

bool NetMan::_loadSettings(JsonDocument& settings) {
    if (!SPIFFS.exists("/settings.json")) {
        // Create default settings
        settings["deviceName"] = _deviceName;
        settings["otaEnabled"] = _otaEnabled;
        settings["apTimeout"] = AP_MODE_TIMEOUT / 1000;
        return true;
    }
    
    File file = SPIFFS.open("/settings.json", "r");
    if (!file) {
        Serial.println("NetMan: Failed to open settings file");
        return false;
    }
    
    DeserializationError error = deserializeJson(settings, file);
    file.close();
    
    if (error) {
        Serial.println("NetMan: Failed to parse settings file");
        return false;
    }
    
    // Apply settings
    if (settings["deviceName"].is<String>()) {
        setDeviceName(settings["deviceName"]);
    }
    if (settings["otaEnabled"].is<bool>()) {
        enableOTA(settings["otaEnabled"]);
    }
    
    return true;
}

void NetMan::_removeWebUIFiles() {
    // List of web UI files to remove
    const char* webUIFiles[] = {
        "/index.html",
        "/style.css", 
        "/app.js",
        "/setup.html",
        nullptr
    };
    
    for (int i = 0; webUIFiles[i] != nullptr; i++) {
        if (SPIFFS.exists(webUIFiles[i])) {
            SPIFFS.remove(webUIFiles[i]);
            Serial.print("NetMan: Removed ");
            Serial.println(webUIFiles[i]);
        }
    }
}

void NetMan::printSPIFFSInfo() {
    Serial.println("=== SPIFFS Diagnostic Info ===");
    
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS: Not mounted, attempting to mount...");
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS: Mount failed!");
            return;
        }
        Serial.println("SPIFFS: Mounted successfully");
    }
    
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    
    Serial.printf("SPIFFS Total: %u bytes\n", totalBytes);
    Serial.printf("SPIFFS Used:  %u bytes\n", usedBytes);
    Serial.printf("SPIFFS Free:  %u bytes\n", freeBytes);
    Serial.printf("SPIFFS Usage: %.1f%%\n", (float)usedBytes / totalBytes * 100);
    
    Serial.print("Networks file exists: ");
    Serial.println(SPIFFS.exists(_networksFilePath) ? "YES" : "NO");
    
    if (SPIFFS.exists(_networksFilePath)) {
        File file = SPIFFS.open(_networksFilePath, "r");
        if (file) {
            Serial.printf("Networks file size: %u bytes\n", file.size());
            file.close();
        }
    }
    
    Serial.println("=== End SPIFFS Info ===");
}

void NetMan::listSPIFFSFiles() {
    Serial.println("=== SPIFFS File List ===");
    
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS: Not mounted!");
        return;
    }
    
    File root = SPIFFS.open("/");
    if (!root) {
        Serial.println("Failed to open SPIFFS root directory");
        return;
    }
    
    if (!root.isDirectory()) {
        Serial.println("Root is not a directory");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("DIR:  %s\n", file.name());
        } else {
            Serial.printf("FILE: %s (%u bytes)\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
    
    Serial.println("=== End File List ===");
}

void NetMan::testSPIFFSWrite() {
    Serial.println("=== SPIFFS Write Test ===");
    
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS: Not mounted!");
        return;
    }
    
    String testFilePath = "/test.txt";
    String testContent = "Hello SPIFFS Test";
    
    // Try to write a test file
    File testFile = SPIFFS.open(testFilePath, "w");
    if (!testFile) {
        Serial.println("Failed to create test file");
        return;
    }
    
    size_t written = testFile.print(testContent);
    testFile.close();
    
    Serial.printf("Wrote %u bytes to test file\n", written);
    
    // Try to read it back
    testFile = SPIFFS.open(testFilePath, "r");
    if (!testFile) {
        Serial.println("Failed to open test file for reading");
        return;
    }
    
    String readContent = testFile.readString();
    testFile.close();
    
    Serial.println("Read back: " + readContent);
    
    // Clean up
    SPIFFS.remove(testFilePath);
    
    if (readContent == testContent) {
        Serial.println("SPIFFS write/read test: PASSED");
    } else {
        Serial.println("SPIFFS write/read test: FAILED");
    }
    
    Serial.println("=== End SPIFFS Test ===");
}
