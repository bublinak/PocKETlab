// Global variables
let currentTab = 'networks';
let networks = [];
let savedNetworks = [];
let deviceStatus = {};

// Initialize the application
document.addEventListener('DOMContentLoaded', function() {
    loadStatus();
    loadSavedNetworks();
    setupEventListeners();
});

// Setup event listeners
function setupEventListeners() {
    // Add network form
    document.getElementById('addNetworkForm').addEventListener('submit', function(e) {
        e.preventDefault();
        addNetwork();
    });

    // Settings form
    document.getElementById('settingsForm').addEventListener('submit', function(e) {
        e.preventDefault();
        saveSettings();
    });

    // OTA form
    document.getElementById('otaForm').addEventListener('submit', function(e) {
        e.preventDefault();
        uploadFirmware();
    });

    // WebUI form
    document.getElementById('webuiForm').addEventListener('submit', function(e) {
        e.preventDefault();
        uploadWebUI();
    });
}

// Tab management
function showTab(tabName) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    
    // Remove active class from all buttons
    document.querySelectorAll('.tab-button').forEach(button => {
        button.classList.remove('active');
    });
    
    // Show selected tab and activate button
    document.getElementById(tabName).classList.add('active');
    event.target.classList.add('active');
    
    currentTab = tabName;
    
    // Load data for specific tabs
    if (tabName === 'status') {
        loadStatus();
    } else if (tabName === 'settings') {
        loadSettings();
    } else if (tabName === 'update') {
        loadDeviceInfo();
    }
}

// Network scanning
function scanNetworks() {
    const button = event.target;
    const originalText = button.innerHTML;
    button.innerHTML = '<span class="loading"></span> Scanning...';
    button.disabled = true;

    fetch('/scan')
        .then(response => response.json())
        .then(data => {
            networks = data.networks || [];
            displayNetworks();
        })
        .catch(error => {
            console.error('Error scanning networks:', error);
            showAlert('Failed to scan networks', 'danger');
        })
        .finally(() => {
            button.innerHTML = originalText;
            button.disabled = false;
        });
}

// Display scanned networks
function displayNetworks() {
    const networkList = document.getElementById('networkList');
    
    if (networks.length === 0) {
        networkList.innerHTML = '<p>No networks found. Click "Scan Networks" to search for available WiFi networks.</p>';
        return;
    }

    let html = '<h4>Available Networks</h4>';
    networks.forEach(network => {
        const isSecure = network.encryption !== 'none';
        const signalStrength = getSignalStrength(network.rssi);
        
        html += `
            <div class="network-item">
                <div class="network-info">
                    <div class="network-name">
                        ${isSecure ? '🔒' : '📶'} ${network.ssid}
                    </div>
                    <div class="network-details">
                        Signal: ${signalStrength} | Channel: ${network.channel} | ${network.encryption}
                    </div>
                </div>
                <div class="network-actions">
                    <button class="btn btn-primary" onclick="connectToNetwork('${network.ssid}', ${isSecure})">
                        Connect
                    </button>
                </div>
            </div>
        `;
    });
    
    networkList.innerHTML = html;
}

// Get signal strength indicator
function getSignalStrength(rssi) {
    if (rssi > -50) return 'Excellent';
    if (rssi > -60) return 'Good';
    if (rssi > -70) return 'Fair';
    return 'Weak';
}

// Connect to a network
function connectToNetwork(ssid, requiresPassword) {
    let password = '';
    
    if (requiresPassword) {
        password = prompt(`Enter password for "${ssid}":`);
        if (password === null) return; // User cancelled
    }
    
    const data = { ssid: ssid, password: password };
    
    fetch('/addnetwork', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showAlert(`Successfully connected to ${ssid}`, 'success');
            loadSavedNetworks();
            loadStatus();
        } else {
            showAlert(`Failed to connect to ${ssid}: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        console.error('Error connecting to network:', error);
        showAlert('Failed to connect to network', 'danger');
    });
}

// Add network manually
function addNetwork() {
    const ssid = document.getElementById('ssid').value;
    const password = document.getElementById('password').value;
    
    const data = { ssid: ssid, password: password };
    
    fetch('/addnetwork', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showAlert(`Network "${ssid}" added successfully`, 'success');
            document.getElementById('addNetworkForm').reset();
            loadSavedNetworks();
        } else {
            showAlert(`Failed to add network: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        console.error('Error adding network:', error);
        showAlert('Failed to add network', 'danger');
    });
}

// Load saved networks
function loadSavedNetworks() {
    fetch('/networks')
        .then(response => response.json())
        .then(data => {
            savedNetworks = data.networks || [];
            displaySavedNetworks();
        })
        .catch(error => {
            console.error('Error loading saved networks:', error);
        });
}

// Display saved networks
function displaySavedNetworks() {
    const savedNetworksDiv = document.getElementById('savedNetworks');
    
    if (savedNetworks.length === 0) {
        savedNetworksDiv.innerHTML = '<p>No saved networks.</p>';
        return;
    }

    let html = '';
    savedNetworks.forEach(network => {
        const status = network.ssid === deviceStatus.connectedSSID ? 
            '<span style="color: green;">✓ Connected</span>' : 
            '<span style="color: gray;">Not connected</span>';
        
        html += `
            <div class="network-item">
                <div class="network-info">
                    <div class="network-name">🔒 ${network.ssid}</div>
                    <div class="network-details">${status}</div>
                </div>
                <div class="network-actions">
                    <button class="btn btn-danger" onclick="removeNetwork('${network.ssid}')">
                        Remove
                    </button>
                </div>
            </div>
        `;
    });
    
    savedNetworksDiv.innerHTML = html;
}

// Remove network
function removeNetwork(ssid) {
    if (!confirm(`Remove network "${ssid}"?`)) return;
    
    const data = { ssid: ssid };
    
    fetch('/removenetwork', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showAlert(`Network "${ssid}" removed`, 'success');
            loadSavedNetworks();
        } else {
            showAlert(`Failed to remove network: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        console.error('Error removing network:', error);
        showAlert('Failed to remove network', 'danger');
    });
}

// Load device status
function loadStatus() {
    fetch('/status')
        .then(response => response.json())
        .then(data => {
            deviceStatus = data;
            displayStatus();
        })
        .catch(error => {
            console.error('Error loading status:', error);
        });
}

// Display device status
function displayStatus() {
    const statusInfo = document.getElementById('statusInfo');
    
    const status = deviceStatus.connected ? 'Connected' : 'Disconnected';
    const statusColor = deviceStatus.connected ? 'green' : 'red';
    
    let html = `
        <div class="status-item">
            <span class="status-label">Connection Status:</span>
            <span class="status-value" style="color: ${statusColor};">${status}</span>
        </div>
    `;
    
    if (deviceStatus.connected) {
        html += `
            <div class="status-item">
                <span class="status-label">Network:</span>
                <span class="status-value">${deviceStatus.connectedSSID}</span>
            </div>
            <div class="status-item">
                <span class="status-label">IP Address:</span>
                <span class="status-value">${deviceStatus.ipAddress}</span>
            </div>
            <div class="status-item">
                <span class="status-label">Signal Strength:</span>
                <span class="status-value">${deviceStatus.rssi} dBm</span>
            </div>
        `;
    }
    
    html += `
        <div class="status-item">
            <span class="status-label">Device Name:</span>
            <span class="status-value">${deviceStatus.deviceName}</span>
        </div>
        <div class="status-item">
            <span class="status-label">MAC Address:</span>
            <span class="status-value">${deviceStatus.macAddress || 'Unknown'}</span>
        </div>        <div class="status-item">
            <span class="status-label">Free Heap:</span>
            <span class="status-value">${deviceStatus.freeHeap || 'Unknown'} bytes</span>
        </div>
        <div class="status-item">
            <span class="status-label">mDNS:</span>
            <span class="status-value">${deviceStatus.mdnsEnabled ? 'Enabled' : 'Disabled'}</span>
        </div>
        <div class="status-item">
            <span class="status-label">mDNS Name:</span>
            <span class="status-value">${deviceStatus.mdnsName || 'N/A'}.local</span>
        </div>
    `;
    
    statusInfo.innerHTML = html;
}

// Load settings
function loadSettings() {
    fetch('/settings')
        .then(response => response.json())
        .then(data => {
            // Populate settings form with current values
            document.getElementById('deviceName').value = data.deviceName || '';
            document.getElementById('hostname').value = data.hostname || '';
        })
        .catch(error => {
            console.error('Error loading settings:', error);
            // Fallback to status data
            document.getElementById('deviceName').value = deviceStatus.deviceName || '';
        });
}

// Save settings
function saveSettings() {
    const deviceName = document.getElementById('deviceName').value;
    const adminPassword = document.getElementById('adminPassword').value;
    const hostname = document.getElementById('hostname').value;
    
    const data = { 
        deviceName: deviceName,
        hostname: hostname
    };
    
    if (adminPassword) {
        data.adminPassword = adminPassword;
    }
    
    fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showAlert('Settings saved successfully', 'success');
            loadStatus();
            // Clear password field
            document.getElementById('adminPassword').value = '';
        } else {
            showAlert(`Failed to save settings: ${data.message}`, 'danger');
        }
    })
    .catch(error => {
        console.error('Error saving settings:', error);
        showAlert('Failed to save settings', 'danger');
    });
}

// Reboot device
function rebootDevice() {
    if (!confirm('Are you sure you want to reboot the device?')) return;
    
    fetch('/reboot', { method: 'POST' })
        .then(() => {
            showAlert('Device is rebooting...', 'warning');
            setTimeout(() => {
                window.location.reload();
            }, 10000);
        })
        .catch(error => {
            console.error('Error rebooting device:', error);
            showAlert('Failed to reboot device', 'danger');
        });
}

// Factory reset
function factoryReset() {
    if (!confirm('Are you sure you want to reset all settings? This will remove all saved networks and reset the device to factory defaults.')) return;
    if (!confirm('This action cannot be undone. Are you absolutely sure?')) return;
    
    fetch('/factory-reset', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showAlert('Factory reset completed. Device will reboot...', 'warning');
                setTimeout(() => {
                    window.location.reload();
                }, 5000);
            } else {
                showAlert(`Failed to reset: ${data.message}`, 'danger');
            }
        })
        .catch(error => {
            console.error('Error resetting settings:', error);
            showAlert('Failed to reset settings', 'danger');
        });
}

// Export network settings
function exportNetworks() {
    fetch('/networks')
        .then(response => response.json())
        .then(data => {
            const networks = data.networks || [];
            const exportData = {
                deviceName: deviceStatus.deviceName,
                exportDate: new Date().toISOString(),
                networks: networks.map(net => ({ ssid: net.ssid })) // Don't export passwords
            };
            
            const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `pocketlab-networks-${new Date().toISOString().split('T')[0]}.json`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            
            showAlert('Network settings exported', 'success');
        })
        .catch(error => {
            console.error('Error exporting networks:', error);
            showAlert('Failed to export networks', 'danger');
        });
}

// Clear all networks
function clearAllNetworks() {
    if (!confirm('Are you sure you want to remove all saved networks?')) return;
    
    Promise.all(savedNetworks.map(network => 
        fetch('/removenetwork', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid: network.ssid })
        })
    ))
    .then(() => {
        showAlert('All networks cleared', 'success');
        loadSavedNetworks();
        loadStatus();
    })
    .catch(error => {
        console.error('Error clearing networks:', error);
        showAlert('Failed to clear all networks', 'danger');
    });
}

// Upload firmware
function uploadFirmware() {
    const fileInput = document.getElementById('otaFile');
    const file = fileInput.files[0];
    
    if (!file) {
        showAlert('Please select a firmware file', 'warning');
        return;
    }
    
    const formData = new FormData();
    formData.append('update', file);
    
    const progressContainer = document.getElementById('firmwareProgress');
    const progressFill = document.getElementById('firmwareProgressFill');
    const progressText = document.getElementById('firmwareProgressText');
    
    progressContainer.style.display = 'block';
    
    const xhr = new XMLHttpRequest();
    
    xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressFill.style.width = percentComplete + '%';
            progressText.textContent = Math.round(percentComplete) + '%';
        }
    });
    
    xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
            progressFill.style.width = '100%';
            progressText.textContent = '100%';
            showAlert('Firmware uploaded successfully. Device will reboot...', 'success');
            setTimeout(() => {
                window.location.reload();
            }, 10000);
        } else {
            showAlert('Firmware upload failed', 'danger');
        }
        progressContainer.style.display = 'none';
    });
    
    xhr.addEventListener('error', function() {
        showAlert('Firmware upload failed', 'danger');
        progressContainer.style.display = 'none';
    });
    
    xhr.open('POST', '/ota');
    xhr.send(formData);
}

// Upload WebUI
function uploadWebUI() {
    const fileInput = document.getElementById('webuiFile');
    const file = fileInput.files[0];
    
    if (!file) {
        showAlert('Please select a WebUI ZIP file', 'warning');
        return;
    }
    
    if (!file.name.toLowerCase().endsWith('.zip')) {
        showAlert('Please select a ZIP file', 'warning');
        return;
    }
    
    const formData = new FormData();
    formData.append('webui', file);
    
    const progressContainer = document.getElementById('webuiProgress');
    const progressFill = document.getElementById('webuiProgressFill');
    const progressText = document.getElementById('webuiProgressText');
    
    progressContainer.style.display = 'block';
    
    const xhr = new XMLHttpRequest();
    
    xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressFill.style.width = percentComplete + '%';
            progressText.textContent = Math.round(percentComplete) + '%';
        }
    });
    
    xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
            progressFill.style.width = '100%';
            progressText.textContent = '100%';
            showAlert('Web UI uploaded successfully. Refreshing page...', 'success');
            setTimeout(() => {
                window.location.reload();
            }, 3000);
        } else {
            showAlert('Web UI upload failed', 'danger');
        }
        progressContainer.style.display = 'none';
    });
    
    xhr.addEventListener('error', function() {
        showAlert('Web UI upload failed', 'danger');
        progressContainer.style.display = 'none';
    });
    
    xhr.open('POST', '/upload');
    xhr.send(formData);
}

// Load device information
function loadDeviceInfo() {
    const deviceInfo = document.getElementById('deviceInfo');
    
    let html = `
        <div class="device-info-item">
            <span class="device-info-label">Device Name:</span>
            <span class="device-info-value">${deviceStatus.deviceName || 'Unknown'}</span>
        </div>
        <div class="device-info-item">
            <span class="device-info-label">MAC Address:</span>
            <span class="device-info-value">${deviceStatus.macAddress || 'Unknown'}</span>
        </div>
        <div class="device-info-item">
            <span class="device-info-label">IP Address:</span>
            <span class="device-info-value">${deviceStatus.ipAddress || 'Not connected'}</span>
        </div>
        <div class="device-info-item">
            <span class="device-info-label">Free Heap:</span>
            <span class="device-info-value">${deviceStatus.freeHeap || 'Unknown'} bytes</span>
        </div>
        <div class="device-info-item">
            <span class="device-info-label">Uptime:</span>
            <span class="device-info-value">${deviceStatus.uptime || 'Unknown'}</span>
        </div>
        <div class="device-info-item">
            <span class="device-info-label">mDNS Enabled:</span>
            <span class="device-info-value">${deviceStatus.mdnsEnabled ? 'Yes' : 'No'}</span>
        </div>
    `;
    
    if (deviceStatus.mdnsEnabled && deviceStatus.mdnsName) {
        html += `
            <div class="device-info-item">
                <span class="device-info-label">mDNS Name:</span>
                <span class="device-info-value">${deviceStatus.mdnsName}.local</span>
            </div>
        `;
    }
    
    deviceInfo.innerHTML = html;
}

// Show alert message
function showAlert(message, type) {
    // Remove existing alerts
    document.querySelectorAll('.alert').forEach(alert => alert.remove());
    
    const alert = document.createElement('div');
    alert.className = `alert alert-${type}`;
    alert.textContent = message;
    
    // Insert at the top of the current tab
    const activeTab = document.querySelector('.tab-content.active');
    activeTab.insertBefore(alert, activeTab.firstChild);
    
    // Auto-remove after 5 seconds
    setTimeout(() => {
        alert.remove();
    }, 5000);
}
