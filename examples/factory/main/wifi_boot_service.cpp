#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "nvs_param.h"
#include "ui_port.h"

/*
 * Boot WiFi service.
 *
 * Owns the always-on AP + web server lifecycle and opportunistically attempts
 * STA internet connectivity from saved NVS credentials. The existing UI loop
 * already calls ui_wifi_service_loop(); this file intercepts that symbol via
 * a linker wrapper so the service is active from boot without changing ui.cpp.
 */

static WebServer boot_web_server(80);
static DNSServer boot_dns_server;

static String boot_wifi_sta_ssid;
static String boot_wifi_sta_pwd;
static String boot_wifi_ap_ssid = "T5S3-AP";
static String boot_wifi_ap_pwd = "12345678";

static bool boot_wifi_loaded = false;
static bool boot_wifi_ap_started = false;
static bool boot_wifi_web_started = false;
static bool boot_wifi_sta_begin_called = false;
static wl_status_t boot_wifi_last_status = WL_IDLE_STATUS;
static uint32_t boot_wifi_last_sta_attempt_ms = 0;
static uint32_t boot_wifi_last_settings_check_ms = 0;
static uint32_t boot_wifi_last_log_ms = 0;

static constexpr uint32_t WIFI_STA_RETRY_MS = 30000;
static constexpr uint32_t WIFI_SETTINGS_CHECK_MS = 3000;
static constexpr uint32_t WIFI_STATUS_LOG_MS = 15000;

static void boot_wifi_load_settings()
{
    boot_wifi_sta_ssid = nvs_param_get_str(NVS_ID_WIFI_STA_SSID);
    boot_wifi_sta_pwd = nvs_param_get_str(NVS_ID_WIFI_STA_PWD);
    boot_wifi_ap_ssid = nvs_param_get_str(NVS_ID_WIFI_AP_SSID);
    boot_wifi_ap_pwd = nvs_param_get_str(NVS_ID_WIFI_AP_PWD);

    if (boot_wifi_ap_ssid.length() == 0) boot_wifi_ap_ssid = "T5S3-AP";
    if (boot_wifi_ap_pwd.length() < 8) boot_wifi_ap_pwd = "12345678";
    boot_wifi_loaded = true;
}

static bool boot_wifi_settings_changed()
{
    String sta_ssid = nvs_param_get_str(NVS_ID_WIFI_STA_SSID);
    String sta_pwd = nvs_param_get_str(NVS_ID_WIFI_STA_PWD);
    String ap_ssid = nvs_param_get_str(NVS_ID_WIFI_AP_SSID);
    String ap_pwd = nvs_param_get_str(NVS_ID_WIFI_AP_PWD);
    if (ap_ssid.length() == 0) ap_ssid = "T5S3-AP";
    if (ap_pwd.length() < 8) ap_pwd = "12345678";

    return sta_ssid != boot_wifi_sta_ssid ||
           sta_pwd != boot_wifi_sta_pwd ||
           ap_ssid != boot_wifi_ap_ssid ||
           ap_pwd != boot_wifi_ap_pwd;
}

static void boot_wifi_save_settings()
{
    nvs_param_set_str(NVS_ID_WIFI_STA_SSID, boot_wifi_sta_ssid.c_str());
    nvs_param_set_str(NVS_ID_WIFI_STA_PWD, boot_wifi_sta_pwd.c_str());
    nvs_param_set_str(NVS_ID_WIFI_AP_SSID, boot_wifi_ap_ssid.c_str());
    nvs_param_set_str(NVS_ID_WIFI_AP_PWD, boot_wifi_ap_pwd.c_str());
}

static void boot_wifi_send_cors_headers()
{
    boot_web_server.sendHeader("Access-Control-Allow-Origin", "*");
    boot_web_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    boot_web_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static String boot_wifi_json_escape(const String &value)
{
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

static void boot_wifi_send_settings_json()
{
    boot_wifi_send_cors_headers();
    String json = "{";
    json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"wifi_ssid\":\"" + boot_wifi_json_escape(boot_wifi_sta_ssid) + "\",";
    json += "\"wifi_password\":\"" + boot_wifi_json_escape(boot_wifi_sta_pwd) + "\",";
    json += "\"ap_ssid\":\"" + boot_wifi_json_escape(boot_wifi_ap_ssid) + "\",";
    json += "\"ap_password\":\"" + boot_wifi_json_escape(boot_wifi_ap_pwd) + "\",";
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";
    boot_web_server.send(200, "application/json", json);
}

static const char *BOOT_WIFI_HTML = R"HTML(<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>T5S3 WiFi</title>
  <style>
    body { font-family: sans-serif; max-width: 560px; margin: 24px auto; padding: 0 16px; }
    label { display:block; margin: 10px 0 4px; font-weight: 600; }
    input { width: 100%; box-sizing: border-box; padding: 8px; }
    button { margin-top: 14px; padding: 10px 14px; }
    #status { margin-top: 12px; white-space: pre-wrap; }
  </style>
</head>
<body>
  <h1>T5S3 WiFi Settings</h1>
  <p>The access point stays on for local access. Saved station credentials are retried for internet access.</p>
  <label for="wifi_ssid">WiFi SSID</label>
  <input id="wifi_ssid" type="text" />
  <label for="wifi_password">WiFi Password</label>
  <input id="wifi_password" type="password" />
  <label for="ap_ssid">AP SSID</label>
  <input id="ap_ssid" type="text" />
  <label for="ap_password">AP Password (min 8 chars)</label>
  <input id="ap_password" type="password" />
  <button id="saveBtn">Save and Retry</button>
  <button id="refreshBtn">Refresh Status</button>
  <div id="status"></div>
<script>
const $ = id => document.getElementById(id);
function setStatus(s){ $('status').textContent = s; }
async function load(){
  const r = await fetch('/settings');
  const d = await r.json();
  $('wifi_ssid').value = d.wifi_ssid || '';
  $('wifi_password').value = d.wifi_password || '';
  $('ap_ssid').value = d.ap_ssid || '';
  $('ap_password').value = d.ap_password || '';
  setStatus(`Connected: ${d.wifi_connected ? 'yes' : 'no'}\nAP IP: ${d.ap_ip}\nSTA IP: ${d.sta_ip}`);
}
async function save(){
  const body = new URLSearchParams();
  body.set('wifi_ssid', $('wifi_ssid').value);
  body.set('wifi_password', $('wifi_password').value);
  body.set('ap_ssid', $('ap_ssid').value);
  body.set('ap_password', $('ap_password').value);
  const r = await fetch('/settings', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body});
  const d = await r.json();
  setStatus(`Saved. Retrying STA.\nConnected: ${d.wifi_connected ? 'yes' : 'no'}\nAP IP: ${d.ap_ip}\nSTA IP: ${d.sta_ip}`);
}
$('saveBtn').addEventListener('click', () => save().catch(e => setStatus('Error: ' + e.message)));
$('refreshBtn').addEventListener('click', () => load().catch(e => setStatus('Error: ' + e.message)));
load().catch(e => setStatus('Error: ' + e.message));
</script>
</body>
</html>)HTML";

static void boot_wifi_handle_root()
{
    boot_web_server.send(200, "text/html", BOOT_WIFI_HTML);
}

static void boot_wifi_handle_settings_options()
{
    boot_wifi_send_cors_headers();
    boot_web_server.send(204, "text/plain", "");
}

static void boot_wifi_handle_settings_post()
{
    if (boot_web_server.hasArg("wifi_ssid")) boot_wifi_sta_ssid = boot_web_server.arg("wifi_ssid");
    if (boot_web_server.hasArg("wifi_password")) boot_wifi_sta_pwd = boot_web_server.arg("wifi_password");
    if (boot_web_server.hasArg("ap_ssid")) boot_wifi_ap_ssid = boot_web_server.arg("ap_ssid");
    if (boot_web_server.hasArg("ap_password")) {
        String ap_pwd = boot_web_server.arg("ap_password");
        if (ap_pwd.length() >= 8) boot_wifi_ap_pwd = ap_pwd;
    }
    if (boot_wifi_ap_ssid.length() == 0) boot_wifi_ap_ssid = "T5S3-AP";
    if (boot_wifi_ap_pwd.length() < 8) boot_wifi_ap_pwd = "12345678";

    boot_wifi_save_settings();
    boot_wifi_ap_started = false;
    boot_wifi_sta_begin_called = false;
    boot_wifi_last_sta_attempt_ms = 0;
    boot_wifi_send_settings_json();
}

static void boot_wifi_start_web_server()
{
    if (boot_wifi_web_started) return;

    boot_dns_server.start(53, "*", WiFi.softAPIP());
    boot_web_server.on("/", HTTP_GET, boot_wifi_handle_root);
    boot_web_server.on("/settings", HTTP_OPTIONS, boot_wifi_handle_settings_options);
    boot_web_server.on("/settings", HTTP_GET, boot_wifi_send_settings_json);
    boot_web_server.on("/settings", HTTP_POST, boot_wifi_handle_settings_post);
    boot_web_server.onNotFound([]() {
        boot_web_server.sendHeader("Location", "http://paper.go/");
        boot_web_server.send(302, "text/plain", "Redirecting to http://paper.go/");
    });
    boot_web_server.begin();
    boot_wifi_web_started = true;
    Serial.printf("[wifi boot] web server started AP_IP=%s\n", WiFi.softAPIP().toString().c_str());
}

static void boot_wifi_ensure_ap()
{
    if (!boot_wifi_loaded) boot_wifi_load_settings();
    if (boot_wifi_ap_started) return;

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(boot_wifi_ap_ssid.c_str(), boot_wifi_ap_pwd.c_str());
    if (!ok) {
        Serial.printf("[wifi boot] AP start failed ssid='%s' pwd_len=%u\n", boot_wifi_ap_ssid.c_str(), (unsigned)boot_wifi_ap_pwd.length());
        return;
    }
    boot_wifi_ap_started = true;
    Serial.printf("[wifi boot] AP started ssid='%s' ip=%s\n", boot_wifi_ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
    boot_wifi_start_web_server();
}

static void boot_wifi_try_sta(const char *reason)
{
    if (!boot_wifi_loaded) boot_wifi_load_settings();
    if (boot_wifi_sta_ssid.length() == 0) return;

    uint32_t now = millis();
    if (boot_wifi_sta_begin_called && (now - boot_wifi_last_sta_attempt_ms) < WIFI_STA_RETRY_MS) return;

    boot_wifi_ensure_ap();
    WiFi.setAutoReconnect(true);
    Serial.printf("[wifi boot] STA begin reason=%s ssid='%s'\n", reason ? reason : "", boot_wifi_sta_ssid.c_str());
    WiFi.begin(boot_wifi_sta_ssid.c_str(), boot_wifi_sta_pwd.c_str());
    boot_wifi_sta_begin_called = true;
    boot_wifi_last_sta_attempt_ms = now;
}

static void boot_wifi_status_poll()
{
    wl_status_t status = WiFi.status();
    bool connected = (status == WL_CONNECTED);
    ui_wifi_set_status(connected);

    if (status != boot_wifi_last_status) {
        Serial.printf("[wifi boot] STA status=%d connected=%d ip=%s\n", (int)status, connected ? 1 : 0, WiFi.localIP().toString().c_str());
        boot_wifi_last_status = status;
    }

    if (!connected) {
        boot_wifi_try_sta("retry");
    }

    uint32_t now = millis();
    if (now - boot_wifi_last_log_ms >= WIFI_STATUS_LOG_MS) {
        boot_wifi_last_log_ms = now;
        Serial.printf("[wifi boot] AP=%d web=%d STA=%d AP_IP=%s STA_IP=%s\n",
                      boot_wifi_ap_started ? 1 : 0,
                      boot_wifi_web_started ? 1 : 0,
                      connected ? 1 : 0,
                      WiFi.softAPIP().toString().c_str(),
                      WiFi.localIP().toString().c_str());
    }
}

static void boot_wifi_check_settings()
{
    uint32_t now = millis();
    if (now - boot_wifi_last_settings_check_ms < WIFI_SETTINGS_CHECK_MS) return;
    boot_wifi_last_settings_check_ms = now;

    if (!boot_wifi_loaded) {
        boot_wifi_load_settings();
        return;
    }
    if (!boot_wifi_settings_changed()) return;

    Serial.println("[wifi boot] settings changed; reloading AP/STA config");
    boot_wifi_load_settings();
    boot_wifi_ap_started = false;
    boot_wifi_sta_begin_called = false;
    boot_wifi_last_sta_attempt_ms = 0;
    boot_wifi_ensure_ap();
    boot_wifi_try_sta("settings_changed");
}

extern "C" void __wrap__Z20ui_wifi_service_loopv(void)
{
    boot_wifi_ensure_ap();
    boot_wifi_check_settings();
    boot_wifi_status_poll();

    if (boot_wifi_web_started) {
        boot_dns_server.processNextRequest();
        boot_web_server.handleClient();
    }
}
