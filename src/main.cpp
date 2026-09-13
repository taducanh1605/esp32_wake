#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <ping/ping_sock.h>
#include <DNSServer.h>
#include <vector>

#include <HTTPServer.hpp>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <ResourceNode.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include "tls_cert.h"
#include <fauxmoESP.h>

using namespace httpsserver;

#define RELAY_PIN 5
#define RESET_HOLD_PIN 9
#define LED_PIN 8
#define AP_SSID "ESP32-Wake-Setup"
#define AP_PASSWORD "12345678"
#define CONFIG_NAMESPACE "wakecfg"
#define RESET_HOLD_MS 10000UL

Preferences preferences;

// Embedded self-signed cert (src/tls_cert.h); HTTP is meant for a future reverse proxy, HTTPS for direct browser use.
SSLCert tlsCert = SSLCert(const_cast<unsigned char *>(cert_der), cert_der_len, const_cast<unsigned char *>(key_der), key_der_len);
HTTPServer *httpServer = nullptr;
HTTPSServer *httpsServer = nullptr;
std::vector<ResourceNode *> routeNodes;
ResourceNode *captivePortalNode = nullptr;

// Redirects captive-portal probe requests (Android/iOS/Windows "Sign in to network") to the setup page.
const byte DNS_PORT = 53;
DNSServer dnsServer;
bool dnsServerActive = false;

// Switching the HTTP/HTTPS server ports must happen from loop(), never from inside a request
// handler that server is currently running (that would delete the object out from under itself).
enum class PendingServerAction { NONE, START_STA, START_AP };
PendingServerAction pendingServerAction = PendingServerAction::NONE;
unsigned long pendingServerActionAt = 0;

// Local Alexa control (no cloud/account needed): emulates a Philips Hue bulb so "discover devices"
// finds it on the LAN. Only started once we're on the home WiFi (STA), since it needs port 80,
// which our own HTTP server already owns while in AP setup mode.
#define ALEXA_DEVICE_NAME "MY PC"
fauxmoESP fauxmo;
bool fauxmoStarted = false;

// Default values applied on first boot and factory reset. Change a default here only;
// every other place in the file reads from these constants instead of repeating literals.
const char *const DEFAULT_TARGET_IP = "192.168.1.135";
const uint16_t DEFAULT_CONFIG_PORT = 2443;
const bool DEFAULT_USE_POWER_LED_PIN = false;
const int DEFAULT_POWER_LED_PIN = 2;
const char *const DEFAULT_ADMIN_PASSWORD_HASH = "";
const bool DEFAULT_AUTO_RECONNECT_ENABLED = true;

String wifiSsid = "";
String wifiPassword = "";
String targetIp = DEFAULT_TARGET_IP;
uint16_t configPort = DEFAULT_CONFIG_PORT;
uint16_t httpsPort = 2444;
bool usePowerLedPin = DEFAULT_USE_POWER_LED_PIN;
int powerLedPin = DEFAULT_POWER_LED_PIN;
String adminPasswordHash = DEFAULT_ADMIN_PASSWORD_HASH;
bool autoReconnectEnabled = DEFAULT_AUTO_RECONNECT_ENABLED;
// Set on successful /login; gates access to the config page ('/') whenever adminPasswordHash is set.
String sessionToken = "";

void handleWifiGotIPv6(arduino_event_id_t event, arduino_event_info_t info) {
  if (event != ARDUINO_EVENT_WIFI_STA_GOT_IP6) return;

  IPv6Address address(info.got_ip6.ip6_info.ip.addr);
  Serial.print("[STA] IPv6 address assigned: ");
  Serial.println(address);
}

String escapeHtml(const String &input) {
  String out;
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

bool connectToSavedWifi(uint32_t timeoutMs);
void startAccessPoint();

static inline uint32_t rotr32(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

// Self-contained SHA-256 (no external crypto dependency) used only to hash the control password.
static void sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
  static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };
  uint32_t h[8] = {
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
  };

  size_t newLen = ((((len + 8) / 64) + 1) * 64);
  uint8_t *msg = new uint8_t[newLen]();
  memcpy(msg, data, len);
  msg[len] = 0x80;
  uint64_t bitsLen = (uint64_t)len * 8;
  for (int i = 0; i < 8; ++i) {
    msg[newLen - 1 - i] = (uint8_t)(bitsLen >> (8 * i));
  }

  for (size_t chunkStart = 0; chunkStart < newLen; chunkStart += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (msg[chunkStart + i * 4] << 24) | (msg[chunkStart + i * 4 + 1] << 16) |
             (msg[chunkStart + i * 4 + 2] << 8) | (msg[chunkStart + i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
      uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;
      hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  delete[] msg;

  for (int i = 0; i < 8; ++i) {
    digest[i * 4] = (uint8_t)(h[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)(h[i]);
  }
}

String sha256Hex(const String &input) {
  uint8_t digest[32];
  sha256((const uint8_t *)input.c_str(), input.length(), digest);
  static const char hexChars[] = "0123456789abcdef";
  String hex;
  hex.reserve(64);
  for (int i = 0; i < 32; ++i) {
    hex += hexChars[(digest[i] >> 4) & 0xF];
    hex += hexChars[digest[i] & 0xF];
  }
  return hex;
}

bool hasSavedWifiConfig() {
  return preferences.begin(CONFIG_NAMESPACE, false) && preferences.isKey("wifi_ssid");
}

void clearSavedConfig() {
  wifiSsid = "";
  wifiPassword = "";
  targetIp = DEFAULT_TARGET_IP;
  configPort = DEFAULT_CONFIG_PORT;
  usePowerLedPin = DEFAULT_USE_POWER_LED_PIN;
  powerLedPin = DEFAULT_POWER_LED_PIN;
  adminPasswordHash = DEFAULT_ADMIN_PASSWORD_HASH;
  autoReconnectEnabled = DEFAULT_AUTO_RECONNECT_ENABLED;
  sessionToken = "";

  preferences.begin(CONFIG_NAMESPACE, false);
  preferences.clear();
  preferences.putBool("factory_reset_pending", true);
  preferences.end();

  Serial.println("[CFG] All saved settings cleared and reset flag set.");
}

void setStatusLed(bool enabled) {
  digitalWrite(LED_PIN, enabled ? LOW : HIGH);
}

void blinkLed(int times, int onMs, int offMs) {
  for (int i = 0; i < times; ++i) {
    setStatusLed(true);
    delay(onMs);
    setStatusLed(false);
    delay(offMs);
  }
  setStatusLed(false);
}

void loadConfig() {
  if (!preferences.begin(CONFIG_NAMESPACE, false)) {
    Serial.println("[CFG] Preferences init failed.");
    return;
  }

  if (preferences.isKey("wifi_ssid")) {
    wifiSsid = preferences.getString("wifi_ssid");
  }
  if (preferences.isKey("wifi_password")) {
    wifiPassword = preferences.getString("wifi_password");
  }
  if (preferences.isKey("target_ip")) {
    targetIp = preferences.getString("target_ip");
  }
  if (preferences.isKey("server_port")) {
    configPort = preferences.getUShort("server_port");
  }
  usePowerLedPin = preferences.getBool("use_pwrled", DEFAULT_USE_POWER_LED_PIN);
  powerLedPin = preferences.getInt("pwrled_pin", DEFAULT_POWER_LED_PIN);
  adminPasswordHash = preferences.getString("admin_pwd_hash", DEFAULT_ADMIN_PASSWORD_HASH);
  autoReconnectEnabled = preferences.getBool("auto_reconnect", DEFAULT_AUTO_RECONNECT_ENABLED);
  preferences.end();
}

void saveConfig() {
  preferences.begin(CONFIG_NAMESPACE, false);
  preferences.putString("wifi_ssid", wifiSsid);
  preferences.putString("wifi_password", wifiPassword);
  preferences.putString("target_ip", targetIp);
  preferences.putUShort("server_port", configPort);
  preferences.putBool("use_pwrled", usePowerLedPin);
  preferences.putInt("pwrled_pin", powerLedPin);
  preferences.putString("admin_pwd_hash", adminPasswordHash);
  preferences.putBool("auto_reconnect", autoReconnectEnabled);
  preferences.putBool("factory_reset_pending", false);
  preferences.end();
  Serial.printf("[CFG] Saved: SSID=%s target=%s port=%u\n", wifiSsid.c_str(), targetIp.c_str(), configPort);
}

bool isFactoryResetRequested() {
  pinMode(RESET_HOLD_PIN, INPUT_PULLUP);

  if (digitalRead(RESET_HOLD_PIN) == HIGH) {
    return false;
  }

  Serial.println("[CFG] Button low at startup; ignoring boot-time hold to avoid reset loop.");
  return false;
}

void monitorFactoryResetHold() {
  static unsigned long holdStart = 0;
  static bool holdDetected = false;
  static bool bootInitialized = false;

  if (!bootInitialized) {
    bootInitialized = true;
    if (digitalRead(RESET_HOLD_PIN) == LOW) {
      Serial.println("[CFG] Reset button is already held at boot; waiting for stable state.");
      holdDetected = false;
      holdStart = 0;
      return;
    }
  }

  if (digitalRead(RESET_HOLD_PIN) == LOW) {
    if (!holdDetected) {
      holdDetected = true;
      holdStart = millis();
      Serial.println("[CFG] Reset hold detected. LED will blink to confirm.");
      blinkLed(3, 150, 150);
    }

    if (millis() - holdStart >= RESET_HOLD_MS) {
      Serial.printf("[CFG] Reset hold reached %lu ms => wiping settings and restarting.\n", RESET_HOLD_MS);
      blinkLed(10, 120, 120);
      clearSavedConfig();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      if (httpServer) httpServer->stop();
      if (httpsServer) httpsServer->stop();
      delay(300);
      ESP.restart();
    }
  } else {
    holdDetected = false;
    holdStart = 0;
  }
}

void pulseRelay() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(300);
  digitalWrite(RELAY_PIN, LOW);
}

void forceShutdown() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(7000);
  digitalWrite(RELAY_PIN, LOW);
}

// Single short ping attempt. Returns as soon as a reply arrives; otherwise waits at most timeoutMs.
bool pingOnce(const ip_addr_t &target_addr, uint32_t timeoutMs) {
  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
  ping_config.count = 1;
  ping_config.timeout_ms = timeoutMs;
  ping_config.data_size = 32;
  ping_config.target_addr = target_addr;

  esp_ping_handle_t ping_handle = nullptr;
  esp_err_t err = esp_ping_new_session(&ping_config, nullptr, &ping_handle);
  if (err != ESP_OK || ping_handle == nullptr) {
    return false;
  }

  err = esp_ping_start(ping_handle);
  if (err != ESP_OK) {
    esp_ping_delete_session(ping_handle);
    return false;
  }

  uint32_t reply_count = 0;
  bool got_reply = false;
  unsigned long start = millis();
  while (millis() - start < timeoutMs + 50) {
    if (esp_ping_get_profile(ping_handle, ESP_PING_PROF_REPLY, &reply_count, sizeof(reply_count)) == ESP_OK && reply_count > 0) {
      got_reply = true;
      break;
    }
    delay(15);
  }

  esp_ping_stop(ping_handle);
  esp_ping_delete_session(ping_handle);
  return got_reply;
}

// Several short pings instead of one slow one: a single dropped packet no longer reports the PC
// as offline, and the common case (PC online, first ping replies) still resolves almost instantly.
bool isPcOnline() {
  if (targetIp.length() < 7 || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  IPAddress ip;
  if (!ip.fromString(targetIp)) {
    return false;
  }

  ip_addr_t target_addr;
  IP_ADDR4(&target_addr, ip[0], ip[1], ip[2], ip[3]);

  const int attempts = 3;
  const uint32_t perPingTimeoutMs = 250;
  for (int i = 0; i < attempts; ++i) {
    if (pingOnce(target_addr, perPingTimeoutMs)) {
      return true;
    }
    if (i < attempts - 1) delay(30);
  }
  return false;
}

bool isPcPowered() {
  // Only read the pin when both the checkbox is on and a valid GPIO number is configured.
  if (!usePowerLedPin || powerLedPin < 0) {
    return false;
  }
  // Pull-up input: idle/no PowerLed signal reads HIGH, PowerLed active pulls it LOW.
  return digitalRead(powerLedPin) == LOW;
}

// off: PowerLed pin configured and reads "no power"; powered: has power but no ping reply yet; connected: ping succeeded.
// Ping only runs when powered, and only from here (only called while handling a status request).
String getPcState() {
  if (!isPcOnline()) {
    return !isPcPowered() ? "off" : "powered";
  } else {
    return "connected";
  }
}

String buildStatusJson() {
  String json = "{\n";
  json += "  \"wifi_connected\": " + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",\n";
  json += "  \"ssid\": \"" + escapeHtml(wifiSsid) + "\",\n";
  json += "  \"target_ip\": \"" + escapeHtml(targetIp) + "\",\n";
  json += "  \"port\": " + String(configPort) + ",\n";
  json += "  \"device_ip\": \"" + WiFi.localIP().toString() + "\",\n";
  json += "  \"pc_state\": \"" + getPcState() + "\"\n";
  json += "}";
  return json;
}

String buildLegacyPcStatusJson() {
  String status = "{";
  status += "\"pc0\":{\"GPIO\":\"5\",\"idx\":\"0\",\"stat\":\"" + getPcState() + "\"}";
  status += "}";
  return status;
}

String buildApSetupPage() {
  String ssidValue = wifiSsid.length() > 0 ? wifiSsid : "";
  String portValue = String(configPort > 0 ? configPort : DEFAULT_CONFIG_PORT);
  String targetValue = targetIp.length() > 0 ? targetIp : DEFAULT_TARGET_IP;
  String powerLedPinValue = String(powerLedPin);
  String usePowerLedChecked = usePowerLedPin ? "checked" : "";
  String powerLedWrapDisplay = usePowerLedPin ? "block" : "none";
  String autoReconnectChecked = autoReconnectEnabled ? "checked" : "";
  String removePasswordDisplay = adminPasswordHash.length() > 0 ? "flex" : "none";

  String html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Wake Setup</title>
  <style>
    * { box-sizing: border-box; }
    body { min-width: 320px; min-height: 100vh; margin: 0; padding: 32px 16px 56px; background: #edf1ec; color: #17211d; font-family: "Segoe UI", sans-serif; line-height: 1.5; }
    .wrap { width: min(620px, 100%); margin: 0 auto; }
    h1 { margin: 0 0 20px; color: #173d2d; font-size: 2rem; letter-spacing: 0; }
    .card { padding: 28px; border: 1px solid #d4dbd5; border-radius: 8px; background: #fff; box-shadow: 0 18px 45px rgba(26,48,37,.11); }
    label { display: block; margin: 18px 0 7px; font-size: .86rem; font-weight: 700; }
    input, select, button { width: 100%; min-height: 46px; padding: 11px 13px; border-radius: 6px; font: inherit; }
    input, select { border: 1px solid #d4dbd5; background: #fafcf9; color: #17211d; }
    input:focus, select:focus, button:focus { outline: 3px solid rgba(23,61,45,.24); outline-offset: 2px; }
    button { margin-top: 22px; border: 0; background: #173d2d; color: #fff; font-weight: 700; cursor: pointer; }
    button:hover { background: #0e2d20; }
    .scan { margin-top: 8px; }
    .scan button { background: #f3f5f2; color: #173d2d; border: 1px solid #d4dbd5; }
    .danger-zone { margin-top: 24px; padding-top: 20px; border-top: 1px solid #d4dbd5; }
    .danger-zone button { margin-top: 8px; border: 1px solid #b73c35; background: #fff7f5; color: #a52f29; }
    .danger-zone button:hover { background: #fbe4e1; }
    .small { margin-top: 7px; color: #637069; font-size: .78rem; line-height: 1.5; }
    .status { margin-top: 16px; padding: 12px 13px; border-radius: 6px; background: #f3f5f2; color: #637069; font-size: .82rem; }
    .check-row { display: flex; align-items: flex-start; gap: 10px; font-weight: 600; }
    .check-row input { width: 18px; min-height: 18px; margin: 2px 0 0; }
    @media (max-width: 520px) { body { padding: 22px 10px 40px; } .card { padding: 22px 18px; } }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>ESP32 Wake Setup</h1>
    <div class="card">
      <form id="configForm" method="POST">
        <label>WiFi network</label>
        <select id="ssidSelect" name="ssid">
          <option value="">-- Select a WiFi network --</option>
        </select>

        <label>Custom SSID</label>
        <input id="ssidCustom" name="ssid_custom" type="text" value=")HTML" + ssidValue + R"HTML(" placeholder="Or enter the SSID manually">

        <label>WiFi password</label>
        <input name="password" type="password" placeholder="Leave blank to keep the current password">

        <label>Web server port</label>
        <input name="port" type="number" min="1" max="65535" value=")HTML" + portValue + R"HTML(">

        <label>Target computer IP</label>
        <input name="target_ip" type="text" value=")HTML" + targetValue + R"HTML(" placeholder=")HTML" + String(DEFAULT_TARGET_IP) + R"HTML(">

        <label class="check-row">
          <input type="checkbox" id="usePowerLed" name="use_power_led_pin" value="1" )HTML" + usePowerLedChecked + R"HTML(>
          Use a Power LED pin to detect whether the computer is powered
        </label>
        <div id="powerLedPinWrap" style="display:)HTML" + powerLedWrapDisplay + R"HTML(;">
          <label>Power LED GPIO (for example 2, 4, 6, 7 or 10; do not use the relay pin)</label>
          <input name="power_led_pin" type="number" min="0" max="21" value=")HTML" + powerLedPinValue + R"HTML(">
        </div>

        <label>Control password</label>
        <input id="adminPassword" name="admin_password" type="password" placeholder="Leave blank to keep the current password">
        <div class="small">Enter a new value only when you want to change the password used for wake, shutdown, reset and configuration access.</div>
        <label class="check-row" id="removePasswordRow" style="display:)HTML" + removePasswordDisplay + R"HTML(;">
          <input type="checkbox" id="removeAdminPassword" name="remove_admin_password" value="1">
          Remove the saved control password and allow unauthenticated GET requests
        </label>

        <label class="check-row">
          <input type="checkbox" id="autoReconnect" name="auto_reconnect" value="1" )HTML" + autoReconnectChecked + R"HTML(>
          Automatically reconnect to the saved WiFi network after an outage
        </label>

        <button type="submit">Save configuration</button>
      </form>

      <div class="scan">
        <button type="button" id="scanButton">Scan WiFi networks</button>
        <div class="small">If your network is not listed, enter its SSID manually above.</div>
      </div>
      <div class="status" id="statusBox">No scan has been run yet.</div>
      <div class="danger-zone">
        <strong>Reset configuration</strong>
        <div class="small">Erase all saved settings and restart the ESP32 in setup mode.</div>
        <button type="button" id="resetConfigButton">Reset all configuration</button>
      </div>
    </div>
  </div>

  <script>
    const routeBase = window.location.pathname.endsWith('/')
      ? window.location.pathname
      : window.location.pathname + '/';
    document.getElementById('configForm').action = routeBase + 'api/config';
    const savedSsid = ")HTML" + ssidValue + R"HTML(";
    const select = document.getElementById('ssidSelect');
    if (savedSsid) {
      const option = document.createElement('option');
      option.value = savedSsid;
      option.textContent = savedSsid;
      option.selected = true;
      select.appendChild(option);
      document.getElementById('ssidCustom').value = savedSsid;
    }

    async function scanNetworks() {
      const statusBox = document.getElementById('statusBox');
      statusBox.textContent = 'Scanning WiFi networks...';
      try {
        const response = await fetch(routeBase + 'api/scan');
        const data = await response.json();
        if (!response.ok || data.error) {
          statusBox.textContent = 'WiFi scan failed. Please try again.';
          return;
        }
        select.innerHTML = '<option value="">-- Select a WiFi network --</option>';
        if (savedSsid) {
          const opt = document.createElement('option');
          opt.value = savedSsid;
          opt.textContent = savedSsid;
          opt.selected = true;
          select.appendChild(opt);
        }
        if (!data || !data.networks || !data.networks.length) {
          statusBox.textContent = 'No nearby WiFi networks were found.';
          return;
        }
        data.networks.forEach((net) => {
          const option = document.createElement('option');
          option.value = net.ssid;
          option.textContent = net.ssid + ' (' + net.rssi + ' dBm)';
          select.appendChild(option);
        });
        if (savedSsid) {
          document.getElementById('ssidCustom').value = savedSsid;
        }
        statusBox.textContent = 'Found ' + data.networks.length + ' WiFi network(s).';
      } catch (err) {
        statusBox.textContent = 'WiFi scan error: ' + err;
      }
    }

    select.addEventListener('change', function() {
      const selected = this.value;
      if (selected) {
        document.getElementById('ssidCustom').value = selected;
      }
    });

    const powerLedCheckbox = document.getElementById('usePowerLed');
    const powerLedPinWrap = document.getElementById('powerLedPinWrap');
    powerLedCheckbox.addEventListener('change', function() {
      powerLedPinWrap.style.display = this.checked ? 'block' : 'none';
    });

    const configForm = document.getElementById('configForm');
    const removeAdminPassword = document.getElementById('removeAdminPassword');
    const adminPassword = document.getElementById('adminPassword');
    removeAdminPassword.addEventListener('change', function() {
      adminPassword.disabled = this.checked;
      if (this.checked) adminPassword.value = '';
    });
    configForm.addEventListener('submit', async function(event) {
      event.preventDefault();
      if (removeAdminPassword.checked && !window.confirm('Remove the control password? Wake, shutdown and reset will accept requests without a password.')) {
        return;
      }

      const submitButton = configForm.querySelector('button[type="submit"]');
      const statusBox = document.getElementById('statusBox');
      submitButton.disabled = true;
      statusBox.textContent = 'Saving configuration...';

      try {
        const response = await fetch(configForm.action, {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams(new FormData(configForm)).toString()
        });
        const responseText = await response.text();
        if (!response.ok) {
          throw new Error(responseText || 'HTTP ' + response.status);
        }

        const result = JSON.parse(responseText);
        configForm.querySelector('input[name="password"]').value = '';
        adminPassword.value = '';
        if (removeAdminPassword.checked) {
          removeAdminPassword.checked = false;
          adminPassword.disabled = false;
          document.getElementById('removePasswordRow').style.display = 'none';
        }

        statusBox.textContent = result.status === 'saved_and_connected'
          ? 'Configuration saved. This page will stay open.'
          : 'Configuration saved, but WiFi is not connected. The ESP32 will enter setup mode.';
      } catch (err) {
        statusBox.textContent = 'The connection closed while saving. The configuration may have been saved; reconnect to the ESP32 and verify it.';
      } finally {
        submitButton.disabled = false;
      }
    });

    document.getElementById('scanButton').addEventListener('click', scanNetworks);
    document.getElementById('resetConfigButton').addEventListener('click', async function() {
      if (!window.confirm('Reset all saved configuration? The ESP32 will restart in setup mode and you will need to reconnect to its WiFi access point.')) {
        return;
      }

      const button = this;
      const statusBox = document.getElementById('statusBox');
      button.disabled = true;
      statusBox.textContent = 'Resetting configuration and restarting the ESP32...';

      try {
        const response = await fetch(routeBase + 'api/config/reset', {
          method: 'POST',
          credentials: 'same-origin'
        });
        if (!response.ok) {
          throw new Error('Reset failed with HTTP ' + response.status);
        }
        statusBox.textContent = 'Configuration erased. Reconnect to the ESP32 setup WiFi after it restarts.';
      } catch (err) {
        button.disabled = false;
        statusBox.textContent = 'Reset failed: ' + err;
      }
    });
  </script>
</body>
</html>
)HTML";
  return html;
}

String buildLoginPage(const String &errorMessage) {
  String errorHtml = errorMessage.length() > 0
    ? "<div class=\"status\" style=\"background:#3a1414;\">" + escapeHtml(errorMessage) + "</div>"
    : "";
  String html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Wake - Sign in</title>
  <style>
    * { box-sizing: border-box; }
    body { min-height: 100vh; margin: 0; padding: 32px 16px; background: #edf1ec; color: #17211d; font-family: "Segoe UI", sans-serif; line-height: 1.5; }
    .wrap { width: min(420px, 100%); margin: 60px auto 0; }
    h1 { margin: 0 0 20px; color: #173d2d; font-size: 2rem; }
    .card { padding: 28px; border: 1px solid #d4dbd5; border-radius: 8px; background: #fff; box-shadow: 0 18px 45px rgba(26,48,37,.11); }
    label { display: block; margin-bottom: 7px; font-size: .86rem; font-weight: 700; }
    input, button { width: 100%; min-height: 46px; padding: 11px 13px; border-radius: 6px; font: inherit; }
    input { border: 1px solid #d4dbd5; background: #fafcf9; color: #17211d; }
    button { margin-top: 20px; border: 0; background: #173d2d; color: #fff; font-weight: 700; cursor: pointer; }
    .status { margin-bottom: 16px; padding: 12px; border-radius: 6px; background: #fff0ee; color: #a63832; }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>Sign in</h1>
    <div class="card">
      )HTML" + errorHtml + R"HTML(
      <form id="loginForm" method="POST">
        <label>Control password</label>
        <input name="password" type="password" autofocus placeholder="Enter your password">
        <button type="submit">Sign in</button>
      </form>
      <script>
        const routeBase = window.location.pathname.endsWith('/')
          ? window.location.pathname
          : window.location.pathname + '/';
        const routeBaseLogin = routeBase.indexOf('/login/') === 0 ? routeBase.substring(0, routeBase.length - 1) : routeBase + 'login';
        document.getElementById('loginForm').action = routeBaseLogin;
      </script>
    </div>
  </div>
</body>
</html>
)HTML";
  return html;
}

// Reads the "esp32auth" cookie and compares it against the in-memory session token created by
// the last successful /login. No password is required at all when adminPasswordHash is empty.
bool isConfigPageAuthorized(HTTPRequest * req) {
  if (adminPasswordHash.length() == 0) {
    return true;
  }
  if (sessionToken.length() == 0) {
    return false;
  }

  std::string cookieHeader = req->getHeader("Cookie");
  if (cookieHeader.empty()) {
    return false;
  }

  String cookies(cookieHeader.c_str());
  int idx = cookies.indexOf("esp32auth=");
  if (idx < 0) {
    return false;
  }
  String token = cookies.substring(idx + 10);
  int semi = token.indexOf(';');
  if (semi >= 0) {
    token = token.substring(0, semi);
  }
  token.trim();
  return token.length() > 0 && token == sessionToken;
}

// Config page is reachable through the normal STA port too, not just while in AP setup mode,
// so the device never has to be re-flashed or reset just to change its settings later.
void handleRoot(HTTPRequest * req, HTTPResponse * res) {
  if (!isConfigPageAuthorized(req)) {
    res->setHeader("Content-Type", "text/html");
    res->print(buildLoginPage(""));
    return;
  }

  res->setHeader("Content-Type", "text/html");
  res->print(buildApSetupPage());
}

void handleScan(HTTPRequest * req, HTTPResponse * res) {
  bool apWasActive = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);

  if (apWasActive) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
  }

  WiFi.scanDelete();
  const uint32_t maxMsPerChannel = 500;
  int n = WiFi.scanNetworks(false, true, false, maxMsPerChannel);
  Serial.printf("[SCAN] Synchronous scan result: %d\n", n);

  bool scanFailed = n < 0;
  if (scanFailed) n = 0;

  String json = scanFailed ? "{\"error\":\"scan_failed\",\"networks\":[" : "{\"networks\":[";
  int count = 0;
  for (int i = 0; i < n; ++i) {
    if (count > 0) json += ",";
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    json += "{\"ssid\":\"" + escapeHtml(ssid) + "\",\"rssi\":" + String(rssi) + "}";
    count++;
  }
  json += "]}";
  WiFi.scanDelete();

  if (apWasActive) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }
  if (scanFailed) {
    res->setStatusCode(503);
  }
  res->setHeader("Content-Type", "application/json");
  res->print(json);
}

// Password field is always left blank in the config form (see buildApSetupPage), so an empty
// submission must not be treated as "set the WiFi password to blank". Tries the previously saved
// password first, then falls back to an open (no-password) connection, retrying both a few times
// before giving up. wifiPassword ends up holding whichever one actually worked.
bool connectPreservingPassword(const String &previousPassword, uint32_t perAttemptTimeoutMs, int rounds) {
  for (int round = 0; round < rounds; ++round) {
    wifiPassword = previousPassword;
    if (connectToSavedWifi(perAttemptTimeoutMs)) {
      return true;
    }
    wifiPassword = "";
    if (connectToSavedWifi(perAttemptTimeoutMs)) {
      return true;
    }
  }
  wifiPassword = previousPassword;
  return false;
}

void handleSaveConfig(HTTPRequest * req, HTTPResponse * res) {
  if (!isConfigPageAuthorized(req)) {
    res->setStatusCode(401);
    res->setHeader("Content-Type", "text/plain");
    res->print("Unauthorized: please log in at / first");
    return;
  }

  String previousSsid = wifiSsid;
  String previousPassword = wifiPassword;
  uint16_t previousConfigPort = configPort;

  std::string ssidCustomField, ssidSelectField, passwordField, targetIpFieldValue, portFieldValue;
  std::string usePowerLedField, powerLedPinField, adminPasswordField, removeAdminPasswordField, autoReconnectField;

  HTTPURLEncodedBodyParser parser(req);
  while (parser.nextField()) {
    std::string name = parser.getFieldName();
    std::string value;
    char buf[257];
    while (!parser.endOfField()) {
      size_t readLength = parser.read((byte *)buf, sizeof(buf) - 1);
      buf[readLength] = '\0';
      value += buf;
    }
    if (name == "ssid_custom") ssidCustomField = value;
    else if (name == "ssid") ssidSelectField = value;
    else if (name == "password") passwordField = value;
    else if (name == "target_ip") targetIpFieldValue = value;
    else if (name == "port") portFieldValue = value;
    else if (name == "use_power_led_pin") usePowerLedField = value;
    else if (name == "power_led_pin") powerLedPinField = value;
    else if (name == "admin_password") adminPasswordField = value;
    else if (name == "remove_admin_password") removeAdminPasswordField = value;
    else if (name == "auto_reconnect") autoReconnectField = value;
  }

  String ssidValue = ssidCustomField.length() > 0 ? String(ssidCustomField.c_str()) : String(ssidSelectField.c_str());
  String passwordValue = String(passwordField.c_str());
  String targetValue = String(targetIpFieldValue.c_str());
  String portValue = String(portFieldValue.c_str());
  bool useLedValue = (usePowerLedField == "1" || usePowerLedField == "on" || usePowerLedField == "true");
  int ledPinValue = powerLedPinField.length() > 0 ? String(powerLedPinField.c_str()).toInt() : DEFAULT_POWER_LED_PIN;
  String adminPasswordValue = String(adminPasswordField.c_str());
  bool removeAdminPassword = (removeAdminPasswordField == "1" || removeAdminPasswordField == "on" || removeAdminPasswordField == "true");
  bool autoReconnectValue = (autoReconnectField == "1" || autoReconnectField == "on" || autoReconnectField == "true");

  if (ssidValue.length() == 0) {
    res->setStatusCode(400);
    res->setHeader("Content-Type", "text/plain");
    res->print("SSID is required");
    return;
  }

  if (useLedValue && ledPinValue == RELAY_PIN) {
    res->setStatusCode(400);
    res->setHeader("Content-Type", "text/plain");
    res->print("power_led_pin must be different from the relay pin (" + String(RELAY_PIN) + ")");
    return;
  }

  wifiSsid = ssidValue;
  targetIp = targetValue.length() > 0 ? targetValue : targetIp;
  configPort = portValue.toInt();
  if (configPort < 1 || configPort > 65535) {
    configPort = DEFAULT_CONFIG_PORT;
  }
  usePowerLedPin = useLedValue;
  powerLedPin = ledPinValue;
  if (removeAdminPassword) {
    adminPasswordHash = "";
    sessionToken = "";
  } else if (adminPasswordValue.length() > 0) {
    adminPasswordHash = sha256Hex(adminPasswordValue);
  }
  autoReconnectEnabled = autoReconnectValue;
  if (usePowerLedPin) {
    pinMode(powerLedPin, INPUT_PULLUP);
  }

  bool passwordProvided = passwordValue.length() > 0;
  bool wifiCredentialsChanged = wifiSsid != previousSsid || passwordProvided;
  bool connected = WiFi.status() == WL_CONNECTED;
  bool connectionAttempted = false;

  if (!passwordProvided) {
    wifiPassword = previousPassword;
  }

  if (wifiCredentialsChanged || !connected) {
    connectionAttempted = true;
    if (!passwordProvided && previousPassword.length() > 0) {
      connected = connectPreservingPassword(previousPassword, 7000, 2);
    } else {
      wifiPassword = passwordValue;
      connected = connectToSavedWifi(20000);
    }
  } else {
    Serial.println("[CFG] WiFi credentials unchanged; keeping the current STA connection.");
  }

  if (passwordProvided) {
    wifiPassword = passwordValue;
  }

  saveConfig();
  setStatusLed(false);

  res->setHeader("Content-Type", "application/json");
  if (connected) {
    res->print("{\"status\":\"saved_and_connected\",\"ssid\":\"" + escapeHtml(wifiSsid) + "\",\"target_ip\":\"" + escapeHtml(targetIp) + "\",\"port\":" + String(configPort) + "}");
  } else {
    res->print("{\"status\":\"saved_but_wifi_not_connected\",\"ssid\":\"" + escapeHtml(wifiSsid) + "\",\"target_ip\":\"" + escapeHtml(targetIp) + "\",\"port\":" + String(configPort) + "}");
  }

  // Restart networking only when needed; unchanged settings must not interrupt this request.
  if (!connected) {
    pendingServerAction = PendingServerAction::START_AP;
    pendingServerActionAt = millis() + 300;
  } else if (connectionAttempted || configPort != previousConfigPort) {
    pendingServerAction = PendingServerAction::START_STA;
    pendingServerActionAt = millis() + 300;
  }
}

// Checks the submitted password against adminPasswordHash and, on success, issues a session
// cookie so the browser doesn't have to resend the password on every subsequent page load.
void handleLogin(HTTPRequest * req, HTTPResponse * res) {
  std::string passwordField;
  HTTPURLEncodedBodyParser parser(req);
  while (parser.nextField()) {
    std::string name = parser.getFieldName();
    std::string value;
    char buf[257];
    while (!parser.endOfField()) {
      size_t readLength = parser.read((byte *)buf, sizeof(buf) - 1);
      buf[readLength] = '\0';
      value += buf;
    }
    if (name == "password") {
      passwordField = value;
    }
  }

  if (adminPasswordHash.length() == 0 || sha256Hex(String(passwordField.c_str())) == adminPasswordHash) {
    uint8_t rnd[16];
    for (int i = 0; i < 16; ++i) {
      rnd[i] = (uint8_t)esp_random();
    }
    String token;
    char hex[3];
    for (int i = 0; i < 16; ++i) {
      snprintf(hex, sizeof(hex), "%02x", rnd[i]);
      token += hex;
    }
    sessionToken = token;

    res->setStatusCode(302);
    res->setStatusText("Found");
    res->setHeader("Location", "./");
    res->setHeader("Set-Cookie", ("esp32auth=" + sessionToken + "; Path=/").c_str());
    res->setHeader("Content-Type", "text/plain");
    res->print("Redirecting...");
    return;
  }

  res->setStatusCode(401);
  res->setHeader("Content-Type", "text/html");
  res->print(buildLoginPage("Incorrect password. Please try again."));
}

// The request method must match the device's authentication mode: GET for an unprotected device,
// or POST with the matching password when protection is enabled.
bool checkActionAuthorized(HTTPRequest * req, HTTPResponse * res) {
  res->setHeader("Access-Control-Allow-Origin", "*");
  res->setHeader("Access-Control-Allow-Private-Network", "true");

  if (adminPasswordHash.length() == 0) {
    if (req->getMethod() == "GET") {
      return true;
    }
    res->setStatusCode(403);
    res->setHeader("Content-Type", "text/plain");
    res->print("This device has no control password; send an unauthenticated GET request");
    return false;
  }

  if (req->getMethod() != "POST") {
    res->setStatusCode(401);
    res->setHeader("Content-Type", "text/plain");
    res->print("Password required: send a POST request with a 'password' field");
    return false;
  }

  std::string providedPassword;
  HTTPURLEncodedBodyParser parser(req);
  while (parser.nextField()) {
    std::string name = parser.getFieldName();
    std::string value;
    char buf[257];
    while (!parser.endOfField()) {
      size_t readLength = parser.read((byte *)buf, sizeof(buf) - 1);
      buf[readLength] = '\0';
      value += buf;
    }
    if (name == "password") {
      providedPassword = value;
    }
  }

  if (sha256Hex(String(providedPassword.c_str())) != adminPasswordHash) {
    res->setStatusCode(403);
    res->setHeader("Content-Type", "text/plain");
    res->print("Invalid password");
    return false;
  }

  return true;
}

void handleWake(HTTPRequest * req, HTTPResponse * res) {
  if (!checkActionAuthorized(req, res)) return;
  pulseRelay();
  res->setHeader("Content-Type", "text/plain");
  res->print("OK");
}

void handleShutdown(HTTPRequest * req, HTTPResponse * res) {
  if (!checkActionAuthorized(req, res)) return;
  forceShutdown();
  res->setHeader("Content-Type", "text/plain");
  res->print("OK");
}

void resetConfigurationAndRestart(HTTPResponse * res) {
  clearSavedConfig();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  res->setHeader("Content-Type", "text/plain");
  res->print("OK");
  delay(500);
  if (httpServer) httpServer->stop();
  if (httpsServer) httpsServer->stop();
  delay(500);
  ESP.restart();
}

void handleReset(HTTPRequest * req, HTTPResponse * res) {
  if (!checkActionAuthorized(req, res)) return;
  resetConfigurationAndRestart(res);
}

void handleConfigReset(HTTPRequest * req, HTTPResponse * res) {
  req->discardRequestBody();
  if (!isConfigPageAuthorized(req)) {
    res->setStatusCode(401);
    res->setHeader("Content-Type", "text/plain");
    res->print("Unauthorized: please log in at / first");
    return;
  }

  resetConfigurationAndRestart(res);
}

void handleStatus(HTTPRequest * req, HTTPResponse * res) {
  if (!checkActionAuthorized(req, res)) return;
  res->setHeader("Content-Type", "text/plain");
  res->print(buildLegacyPcStatusJson());
}

void handleApiPreflight(HTTPRequest * req, HTTPResponse * res) {
  req->discardRequestBody();
  res->setStatusCode(200);
  res->setHeader("Access-Control-Allow-Origin", "*");
  res->setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res->setHeader("Access-Control-Allow-Headers", "Content-Type");
  res->setHeader("Access-Control-Allow-Private-Network", "true");
  res->setHeader("Access-Control-Max-Age", "600");
  res->setHeader("Content-Type", "text/plain");
  res->print("OK");
}

// Catch-all for unregistered paths. In AP mode this is what makes phones/laptops pop up a
// "Sign in to network" notification: OS captive-portal probes (generate_204, hotspot-detect.html,
// connecttest.txt, ...) all land here via the DNS hijack and get redirected to the setup page.
void handleCaptivePortalRedirect(HTTPRequest * req, HTTPResponse * res) {
  req->discardRequestBody();
  res->setHeader("Cache-Control", "no-store");

  if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
    res->setStatusCode(404);
    res->setStatusText("Not Found");
    res->setHeader("Content-Type", "text/plain");
    res->print("Not Found");
    return;
  }

  String location = String(req->isSecure() ? "https://" : "http://") + WiFi.softAPIP().toString() + "/";
  res->setStatusCode(302);
  res->setStatusText("Found");
  res->setHeader("Location", location.c_str());
  res->setHeader("Content-Type", "text/plain");
  res->print("Redirecting to setup page...");
}

// Handles the specific URLs Android/iOS/Windows use to detect a captive portal. Answering these
// directly with a 200 response that isn't the OS's expected "no portal here" body (a bare 204,
// the literal "Success" page, or "Microsoft NCSI") makes the sign-in notification appear
// immediately instead of relying only on the generic redirect below, which some OS versions
// don't treat as a captive-portal signal.
void handleCaptiveProbe(HTTPRequest * req, HTTPResponse * res) {
  req->discardRequestBody();
  res->setHeader("Cache-Control", "no-store");

  if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
    res->setStatusCode(404);
    res->setHeader("Content-Type", "text/plain");
    res->print("Not Found");
    return;
  }

  res->setStatusCode(200);
  res->setHeader("Content-Type", "text/html");
  res->print("<html><head><meta http-equiv=\"refresh\" content=\"0;url=/\"></head><body>Redirecting...</body></html>");
}

void buildRoutes() {
  routeNodes.push_back(new ResourceNode("/", "GET", &handleRoot));
  routeNodes.push_back(new ResourceNode("/login", "POST", &handleLogin));
  routeNodes.push_back(new ResourceNode("/api/config", "POST", &handleSaveConfig));
  routeNodes.push_back(new ResourceNode("/api/config/reset", "POST", &handleConfigReset));
  routeNodes.push_back(new ResourceNode("/api/scan", "GET", &handleScan));
  routeNodes.push_back(new ResourceNode("/status", "GET", &handleStatus));
  routeNodes.push_back(new ResourceNode("/status", "POST", &handleStatus));
  routeNodes.push_back(new ResourceNode("/stt", "GET", &handleStatus));
  routeNodes.push_back(new ResourceNode("/stt", "POST", &handleStatus));
  routeNodes.push_back(new ResourceNode("/status", "OPTIONS", &handleApiPreflight));
  routeNodes.push_back(new ResourceNode("/stt", "OPTIONS", &handleApiPreflight));

  // Known OS captive-portal probe URLs (Android, iOS/macOS, Windows) get an immediate, explicit
  // response instead of falling through to the generic catch-all redirect.
  const char *captiveProbePaths[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
    "/ncsi.txt", "/connecttest.txt", "/canonical.html", "/success.txt",
  };
  for (const char *path : captiveProbePaths) {
    routeNodes.push_back(new ResourceNode(path, "GET", &handleCaptiveProbe));
  }

  // API routes accept GET or POST; checkActionAuthorized() requires the method to match whether
  // this device currently has a control password.
  struct ActionRoute { const char *path; HTTPSCallbackFunction *handler; };
  const ActionRoute actionRoutes[] = {
    {"/wake", &handleWake}, {"/pw", &handleWake},
    {"/shutdown", &handleShutdown}, {"/sd", &handleShutdown}, {"/fsd", &handleShutdown},
    {"/reset", &handleReset}, {"/api/reset", &handleReset}, {"/rs", &handleReset},
  };
  for (const ActionRoute &route : actionRoutes) {
    routeNodes.push_back(new ResourceNode(route.path, "GET", route.handler));
    routeNodes.push_back(new ResourceNode(route.path, "POST", route.handler));
    routeNodes.push_back(new ResourceNode(route.path, "OPTIONS", &handleApiPreflight));
  }

  captivePortalNode = new ResourceNode("", "GET", &handleCaptivePortalRedirect);
}

void beginServerOnPorts(uint16_t httpPortToUse, uint16_t httpsPortToUse, bool enableHttps) {
  if (httpServer) {
    httpServer->stop();
    delete httpServer;
  }
  if (httpsServer) {
    httpsServer->stop();
    delete httpsServer;
    httpsServer = nullptr;
  }

  httpServer = new HTTPServer(httpPortToUse);
  for (ResourceNode *node : routeNodes) {
    httpServer->registerNode(node);
  }
  httpServer->setDefaultNode(captivePortalNode);
  httpServer->start();
  Serial.printf("[HTTP] Server listening on port %u\n", httpPortToUse);

  // HTTPS is skipped in AP setup mode: our self-signed cert makes OS captive-portal probes
  // over HTTPS fail TLS validation, which stops the "Sign in to network" prompt from showing.
  if (enableHttps) {
    httpsServer = new HTTPSServer(&tlsCert, httpsPortToUse);
    for (ResourceNode *node : routeNodes) {
      httpsServer->registerNode(node);
    }
    httpsServer->setDefaultNode(captivePortalNode);
    httpsServer->start();
    Serial.printf("[HTTPS] Server listening on port %u\n", httpsPortToUse);
  } else {
    Serial.println("[HTTPS] Disabled while in AP setup mode");
  }
}

void beginFauxmo() {
  if (fauxmoStarted) return;
  fauxmoStarted = true;

  fauxmo.createServer(true);
  fauxmo.setPort(80);
  fauxmo.addDevice(ALEXA_DEVICE_NAME);
  fauxmo.onSetState([](unsigned char deviceId, const char *deviceName, bool state, unsigned char value) {
    Serial.printf("[ALEXA] %s -> %s\n", deviceName, state ? "ON" : "OFF");
    if (state) {
      pulseRelay();
    } else {
      forceShutdown();
    }
  });
  fauxmo.enable(true);
  Serial.println("[ALEXA] fauxmoESP enabled. Say \"Alexa, discover devices\", then \"Alexa, turn on/off " ALEXA_DEVICE_NAME "\".");
}

void beginConfiguredServer() {
  if (configPort < 1 || configPort > 65535) {
    configPort = DEFAULT_CONFIG_PORT;
  }
  httpsPort = (configPort >= 65535) ? configPort - 1 : configPort + 1;
  beginServerOnPorts(configPort, httpsPort, true);
  beginFauxmo();
}

void startAccessPoint() {
  if (fauxmoStarted) {
    preferences.begin(CONFIG_NAMESPACE, false);
    preferences.putBool("force_ap_once", true);
    preferences.end();
    Serial.println("[ALEXA] Restarting to release port 80 before switching to AP mode.");
    delay(200);
    ESP.restart();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  // Modem sleep otherwise delays the AP's replies to a freshly-joined client's DHCP/DNS/HTTP
  // requests, which is the main reason the OS "sign in to network" prompt is slow or missing.
  WiFi.setSleep(false);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("[AP] IP: ");
  Serial.println(ip);
  setStatusLed(true);

  dnsServer.setTTL(0);
  dnsServer.start(DNS_PORT, "*", ip);
  dnsServerActive = true;

  // Use the standard 80 port in AP setup mode so it's reachable without typing a custom port.
  beginServerOnPorts(80, 443, false);
}

bool connectToSavedWifi(uint32_t timeoutMs) {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  bool ipv6Enabled = WiFi.enableIpV6();
  WiFi.setAutoReconnect(autoReconnectEnabled);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  setStatusLed(false);

  Serial.printf("[STA] Connecting to %s\n", wifiSsid.c_str());
  Serial.printf("[STA] IPv6 %s\n", ipv6Enabled ? "enabled" : "could not be enabled");

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[STA] Connected, IP: ");
      Serial.println(WiFi.localIP());
      if (ipv6Enabled) {
        Serial.print("[STA] IPv6 link-local: ");
        Serial.println(WiFi.localIPv6());
      }
      setStatusLed(false);
      return true;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[STA] Failed to connect.");
  return false;
}

void startStationMode() {
  setStatusLed(false);
  if (!connectToSavedWifi(20000)) {
    Serial.println("[STA] Failed to connect, entering AP mode");
    startAccessPoint();
    return;
  }

  if (WiFi.getMode() == WIFI_STA) {
    if (dnsServerActive) {
      dnsServer.stop();
      dnsServerActive = false;
    }
    beginConfiguredServer();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  WiFi.onEvent(handleWifiGotIPv6, ARDUINO_EVENT_WIFI_STA_GOT_IP6);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  setStatusLed(false);
  pinMode(RESET_HOLD_PIN, INPUT_PULLUP);

  preferences.begin(CONFIG_NAMESPACE, false);
  bool forceAccessPoint = preferences.getBool("force_ap_once", false);
  if (forceAccessPoint) {
    preferences.remove("force_ap_once");
  }
  bool pendingFactoryReset = preferences.getBool("factory_reset_pending", false);
  if (pendingFactoryReset) {
    Serial.println("[CFG] Pending factory reset detected. Forcing AP mode.");
    preferences.remove("factory_reset_pending");
    preferences.clear();
    wifiSsid = "";
    wifiPassword = "";
    targetIp = DEFAULT_TARGET_IP;
    configPort = DEFAULT_CONFIG_PORT;
    usePowerLedPin = DEFAULT_USE_POWER_LED_PIN;
    powerLedPin = DEFAULT_POWER_LED_PIN;
    adminPasswordHash = DEFAULT_ADMIN_PASSWORD_HASH;
    autoReconnectEnabled = DEFAULT_AUTO_RECONNECT_ENABLED;
  } else if (!isFactoryResetRequested()) {
    if (preferences.isKey("wifi_ssid")) {
      wifiSsid = preferences.getString("wifi_ssid");
      wifiPassword = preferences.getString("wifi_password");
      targetIp = preferences.getString("target_ip");
      configPort = preferences.getUShort("server_port");
      if (configPort == 0) configPort = DEFAULT_CONFIG_PORT;
    }
    usePowerLedPin = preferences.getBool("use_pwrled", DEFAULT_USE_POWER_LED_PIN);
    powerLedPin = preferences.getInt("pwrled_pin", DEFAULT_POWER_LED_PIN);
    adminPasswordHash = preferences.getString("admin_pwd_hash", DEFAULT_ADMIN_PASSWORD_HASH);
    autoReconnectEnabled = preferences.getBool("auto_reconnect", DEFAULT_AUTO_RECONNECT_ENABLED);
  }
  preferences.end();

  if (usePowerLedPin) {
    pinMode(powerLedPin, INPUT_PULLUP);
  }

  buildRoutes();

  if (forceAccessPoint) {
    Serial.println("[AP] One-time AP fallback requested.");
    startAccessPoint();
  } else if (wifiSsid.length() > 0) {
    startStationMode();
  } else {
    startAccessPoint();
  }
}

void loop() {
  monitorFactoryResetHold();
  if (httpServer) httpServer->loop();
  if (httpsServer) httpsServer->loop();
  if (dnsServerActive) dnsServer.processNextRequest();
  if (fauxmoStarted) fauxmo.handle();

  if (pendingServerAction != PendingServerAction::NONE && millis() >= pendingServerActionAt) {
    PendingServerAction action = pendingServerAction;
    pendingServerAction = PendingServerAction::NONE;
    if (dnsServerActive) {
      dnsServer.stop();
      dnsServerActive = false;
    }
    if (action == PendingServerAction::START_STA) {
      beginConfiguredServer();
    } else {
      startAccessPoint();
    }
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED && wifiSsid.length() > 0) {
    static unsigned long lastRetryAt = 0;
    if (millis() - lastRetryAt > 30000) {
      lastRetryAt = millis();
      Serial.println("[STA] Connection lost, retrying saved WiFi.");
      connectToSavedWifi(15000);
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[STA] Still not connected, keeping AP available.");
      startAccessPoint();
    }
  }

  // While sitting in AP setup mode with saved WiFi credentials, periodically try to reconnect on
  // our own so the device recovers automatically after a router reboot/outage without needing to
  // be set up on-site again. Only attempted while nobody is actively using the setup AP.
  if (WiFi.getMode() == WIFI_AP && autoReconnectEnabled && wifiSsid.length() > 0) {
    static unsigned long lastApRetryAt = 0;
    const unsigned long apRetryIntervalMs = 60000;
    if (millis() - lastApRetryAt > apRetryIntervalMs) {
      lastApRetryAt = millis();
      if (WiFi.softAPgetStationNum() == 0) {
        Serial.println("[AP] No clients connected; attempting to reconnect to saved WiFi.");
        if (connectToSavedWifi(10000)) {
          if (dnsServerActive) {
            dnsServer.stop();
            dnsServerActive = false;
          }
          beginConfiguredServer();
        } else {
          Serial.println("[AP] Reconnect attempt failed, staying in AP mode.");
          startAccessPoint();
        }
      }
    }
  }
}
