#pragma once

#include <Arduino.h>

void wifiInit();
bool wifiIsConnected();
String wifiGetIP();
bool wifiIsAPMode();

// Multi-network management
String wifiGetNetworksJSON();
bool wifiAddNetwork(const String& ssid, const String& pass);
bool wifiRemoveNetwork(int index);

// Async WiFi scan
void wifiStartScan();
String wifiGetScanResults();

// AP mode settings
String wifiGetAPSSID();
void wifiSetAPSSID(const String& ssid);
