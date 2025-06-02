#ifndef NETMAN_H
#define NETMAN_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <vector>

// Operation modes
enum NetManMode {
    MODE_STA,           // Station mode - connected to WiFi
    MODE_AP_BASIC,      // AP mode with basic setup interface
    MODE_AP_FULL        // AP mode with full web interface from SPIFFS
};

// ZIP file constants
#define ZIP_LOCAL_FILE_SIGNATURE 0x04034b50
#define ZIP_CENTRAL_DIR_SIGNATURE 0x02014b50
#define ZIP_END_CENTRAL_DIR_SIGNATURE 0x06054b50

// ZIP file structures
struct ZipLocalFileHeader {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLength;
    uint16_t extraFieldLength;
};

struct WiFiCredentials {
    String ssid;
    String password;
    bool enabled;
};

class NetMan {
public:
    NetMan(const char* deviceName = "PocKETlab", const char* adminPassword = "admin123");
    
    bool begin();
    void loop();
    
    // WiFi Management
    bool addNetwork(const String& ssid, const String& password);
    bool removeNetwork(const String& ssid);
    bool connectToKnownNetwork();
    void startConfigPortal();
    void stopConfigPortal();
    
    // Network Status
    bool isConnected();
    String getConnectedSSID();
    String getIPAddress();
    int getRSSI();
    
    // Configuration
    void setDeviceName(const String& name);
    void setAdminPassword(const String& password);
    bool isConfigPortalActive();
    NetManMode setMode(NetManMode mode);
      // OTA
    void enableOTA(bool enable = true);
    bool isOTAEnabled();
    
    // mDNS
    void enableMDNS(bool enable = true, const String& serviceName = "");
    bool isMDNSEnabled();
    String getMDNSName();
    
    // New functionality
    bool hasWebUIFiles();
    NetManMode getCurrentMode();
      // Diagnostic functions
    void printSPIFFSInfo();
    void listSPIFFSFiles();
    void testSPIFFSWrite();

private:
    WebServer* _server;
    DNSServer* _dnsServer;
    String _deviceName;
    String _adminPassword;
    bool _configPortalActive;
    bool _otaEnabled;
    unsigned long _lastConnectionAttempt;
    int _currentNetworkIndex;    std::vector<WiFiCredentials> _knownNetworks;
    
    // New mode management
    NetManMode _currentMode;
    unsigned long _apModeTimeout;
    static const unsigned long AP_MODE_TIMEOUT = 300000; // 5 minutes
    
    // mDNS support
    bool _mdnsEnabled;
    String _mdnsServiceName;
    
    // SPIFFS Management
    bool _initSPIFFS();
    bool _loadNetworks();
    bool _saveNetworks();
    String _networksFilePath = "/networks.json";
      // Mode management
    void _switchToMode(NetManMode mode);
    void _setupAPMode(bool basicMode = true);
    void _setupSTAMode();
    
    // mDNS management
    void _startMDNS();
    void _stopMDNS();
    
    // ZIP file handling
    bool _extractZipToSPIFFS(const uint8_t* zipData, size_t zipSize);
    bool _readZipLocalFileHeader(const uint8_t* data, size_t offset, ZipLocalFileHeader& header);
    
    // Web Server Handlers
    void _setupWebServer();
    void _setupBasicWebServer();
    void _setupFullWebServer();
    void _handleRoot();
    void _handleNetworks();
    void _handleAddNetwork();
    void _handleRemoveNetwork();
    void _handleScan();
    void _handleStatus();
    void _handleReboot();    void _handleOTA();
    void _handleOTAUpload();
    void _handleAuth();
    void _handleNotFound();
    void _handleSettings();
    void _handleFactoryReset();
      // Basic mode handlers
    void _handleBasicRoot();
    void _handleBasicConfigure();
    void _handleBasicUpload();
    void _handleBasicScan();
    void _handleBasicReboot();
    
    // Full mode WebUI upload handler
    void _handleWebUIUpload();
    
    // Authentication
    bool _isAuthenticated();
    String _getAuthCookie();
    void _setAuthCookie();
    void _clearAuthCookie();
      // Utilities
    String _generateHTML(const String& title, const String& content);
    String _generateBasicHTML();
    String _getNetworksJSON();
    String _getScanResultsJSON();
    String _getDetailedStatus();
    void _connectToNextNetwork();
    
    // Settings management
    bool _saveSettings(const JsonDocument& settings);
    bool _loadSettings(JsonDocument& settings);
    void _removeWebUIFiles();
    
    // Enhanced ZIP handling
    bool _validateZipFile(const uint8_t* zipData, size_t zipSize);
    
    // Upload handling
    bool _isWebUIUpload;
    String _uploadBuffer;
    void _extractWebUIFromBuffer();
};

#endif // NETMAN_H
