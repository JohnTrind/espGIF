#include "wifi_manager.h"
#include "led_status.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>

static bool apMode = false;
static const int MAX_NETWORKS = 8;

// ---- AP SSID ----

String wifiGetAPSSID() {
    Preferences p;
    String ssid = "espGIF-Setup";
    if (p.begin("wifi", true)) {
        ssid = p.getString("ap_ssid", "espGIF-Setup");
        p.end();
    }
    return ssid;
}

void wifiSetAPSSID(const String& ssid) {
    Preferences p;
    p.begin("wifi", false);
    p.putString("ap_ssid", ssid);
    p.end();
}

// ---- Multi-network storage ----

static int getNetworkCount() {
    Preferences p;
    int count = 0;
    if (p.begin("wifi", true)) {
        count = p.getInt("count", 0);
        // Migrate from old single-network format
        if (count == 0) {
            String oldSSID = p.getString("ssid", "");
            if (oldSSID.length() > 0) {
                String oldPass = p.getString("pass", "");
                p.end();
                // Save as first network in new format
                Preferences pw;
                pw.begin("wifi", false);
                pw.putString("ssid0", oldSSID);
                pw.putString("pass0", oldPass);
                pw.putInt("count", 1);
                pw.remove("ssid");
                pw.remove("pass");
                pw.end();
                return 1;
            }
        }
        p.end();
    }
    return count;
}

String wifiGetNetworksJSON() {
    Preferences p;
    String json = "[";
    if (p.begin("wifi", true)) {
        int count = p.getInt("count", 0);
        for (int i = 0; i < count; i++) {
            if (i > 0) json += ",";
            String ssid = p.getString(("ssid" + String(i)).c_str(), "");
            json += "{\"ssid\":\"";
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");
            json += ssid;
            json += "\",\"index\":";
            json += String(i);
            json += "}";
        }
        p.end();
    }
    json += "]";
    return json;
}

bool wifiAddNetwork(const String& ssid, const String& pass) {
    Preferences p;
    if (!p.begin("wifi", false)) return false;
    int count = p.getInt("count", 0);
    if (count >= MAX_NETWORKS) { p.end(); return false; }

    p.putString(("ssid" + String(count)).c_str(), ssid);
    p.putString(("pass" + String(count)).c_str(), pass);
    p.putInt("count", count + 1);
    p.end();
    Serial.printf("WiFi network added: %s (index %d)\n", ssid.c_str(), count);
    return true;
}

bool wifiRemoveNetwork(int index) {
    Preferences p;
    if (!p.begin("wifi", false)) return false;
    int count = p.getInt("count", 0);
    if (index < 0 || index >= count) { p.end(); return false; }

    // Shift remaining networks down
    for (int i = index; i < count - 1; i++) {
        String nextSSID = p.getString(("ssid" + String(i + 1)).c_str(), "");
        String nextPass = p.getString(("pass" + String(i + 1)).c_str(), "");
        p.putString(("ssid" + String(i)).c_str(), nextSSID);
        p.putString(("pass" + String(i)).c_str(), nextPass);
    }
    // Remove last entries
    p.remove(("ssid" + String(count - 1)).c_str());
    p.remove(("pass" + String(count - 1)).c_str());
    p.putInt("count", count - 1);
    p.end();
    Serial.printf("WiFi network removed: index %d\n", index);
    return true;
}

// ---- Async WiFi scan ----

void wifiStartScan() {
    if (apMode) {
        WiFi.mode(WIFI_AP_STA);
    }
    WiFi.scanNetworks(true);
    Serial.println("WiFi scan started (async)");
}

String wifiGetScanResults() {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
        return "{\"scanning\":true}";
    }
    if (n == WIFI_SCAN_FAILED || n < 0) {
        return "{\"scanning\":false,\"networks\":[]}";
    }

    String json = "{\"scanning\":false,\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        json += ssid;
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",\"open\":";
        json += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "true" : "false";
        json += "}";
    }
    json += "]}";
    WiFi.scanDelete();

    if (apMode) {
        WiFi.mode(WIFI_AP);
    }
    return json;
}

// ---- Init / Connect ----

void wifiInit() {
    Preferences p;
    int count = getNetworkCount();

    Serial.printf("WiFi: %d saved networks\n", count);

    if (count > 0 && p.begin("wifi", true)) {
        ledSet(LED_CONNECTING);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        delay(500);

        for (int i = 0; i < count; i++) {
            String ssid = p.getString(("ssid" + String(i)).c_str(), "");
            String pass = p.getString(("pass" + String(i)).c_str(), "");
            if (ssid.length() == 0) continue;

            Serial.printf("Trying WiFi %d/%d: %s\n", i + 1, count, ssid.c_str());
            WiFi.begin(ssid.c_str(), pass.c_str());

            // 10s timeout per network
            unsigned long start = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
                ledUpdate();
                delay(200);
            }

            if (WiFi.status() == WL_CONNECTED) {
                p.end();
                apMode = false;
                ledSet(LED_CONNECTED);
                Serial.printf("Connected to %s! IP: %s (RSSI: %d)\n",
                              ssid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
                MDNS.begin("espgif");
                return;
            }

            Serial.printf("  Failed (status=%d)\n", WiFi.status());
            WiFi.disconnect();
            delay(500);
        }
        p.end();
        Serial.println("All WiFi networks failed, falling back to AP");
    }

    // Start AP mode
    apMode = true;
    String apSSID = wifiGetAPSSID();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str());
    ledSet(LED_AP_MODE);
    Serial.printf("AP Mode - Connect to '%s', IP: %s\n",
                  apSSID.c_str(), WiFi.softAPIP().toString().c_str());
    MDNS.begin("espgif");
}

bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifiGetIP() {
    if (apMode) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

bool wifiIsAPMode() {
    return apMode;
}
