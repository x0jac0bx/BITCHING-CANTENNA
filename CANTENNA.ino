#include <WiFi.h>
#include <NimBLEDevice.h>

// ================================================
//   CANTENNA — ESP32-S3 WiFi/BLE Scanner
//   Board: ESP32S3 Dev Module
//   USB CDC On Boot: Enabled (native USB serial)
//   Library: NimBLE-Arduino by h2zero
// ================================================

const unsigned long WIFI_SCAN_INTERVAL_MS = 5000;
const unsigned long BLE_SCAN_INTERVAL_MS  = 5000;
const uint32_t      BLE_SCAN_DURATION_MS  = 3000;

enum ScanMode { SCAN_IDLE, SCAN_WIFI, SCAN_BLE };

ScanMode      scanMode          = SCAN_IDLE;
unsigned long lastWifiScan      = 0;
unsigned long lastBleScan       = 0;
bool          bleInitialized    = false;
bool          bleScanInProgress = false;
NimBLEScan*   bleScan           = nullptr;

void waitForSerial() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && millis() - start < 5000) delay(10);
  delay(300);
}

void printBanner() {
  Serial.println();
  Serial.println("================================================");
  Serial.println("            CANTENNA RADIO SCANNER");
  Serial.println("================================================");
  Serial.println("           . _ _ _ _ _ _ _ .");
  Serial.println("          |  P R I N G L E S |");
  Serial.println("          |   :::::::::::    | =====> )))))))");
  Serial.println("          |   :::::::::::    |");
  Serial.println("          |_________________|");
  Serial.println("                  ||");
  Serial.println("              ____||____");
  Serial.println("             /  ESP32   \\");
  Serial.println("            /_S3_BOARD___\\");
  Serial.println("================================================");
  Serial.println("          WiFi scan OR BLE scan, not both");
  Serial.println("================================================");
  Serial.println();
}

void printCommands() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  start wifi   - begin WiFi scanning");
  Serial.println("  stop wifi    - stop WiFi scanning");
  Serial.println("  start ble    - begin BLE scanning");
  Serial.println("  stop ble     - stop BLE scanning");
  Serial.println("  help         - show this list");
  Serial.println();
}

void radioOff() {
  WiFi.scanDelete();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  if (bleScan != nullptr && bleScan->isScanning()) {
    bleScan->stop();
  }

  bleScanInProgress = false;
}

String encryptionToString(wifi_auth_mode_t auth) {
  switch (auth) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
#ifdef WIFI_AUTH_WAPI_PSK
    case WIFI_AUTH_WAPI_PSK:        return "WAPI";
#endif
    default:                        return "?";
  }
}

void runWifiScan() {
  if (scanMode != SCAN_WIFI) return;

  Serial.println("\n[WiFi] Scanning...");

  int n = WiFi.scanNetworks(false, true);

  if (n < 0) {
    Serial.println("[WiFi] Scan failed.");
    WiFi.scanDelete();
    return;
  }

  if (n == 0) {
    Serial.println("[WiFi] No networks found.");
  } else {
    Serial.println("[WiFi] Networks found: " + String(n));
    Serial.println();
    Serial.println("  #    RSSI   CH   ENC        SSID");
    Serial.println("  ----------------------------------------------");

    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) ssid = "<hidden>";
      if (ssid.length() > 36) ssid = ssid.substring(0, 36);

      char line[128];
      snprintf(
        line,
        sizeof(line),
        "  %-4d %-6d %-4d %-10s %s",
        i + 1,
        WiFi.RSSI(i),
        WiFi.channel(i),
        encryptionToString(WiFi.encryptionType(i)).c_str(),
        ssid.c_str()
      );

      Serial.println(line);
    }
  }

  Serial.println("  ----------------------------------------------");
  WiFi.scanDelete();
}

void startWifi() {
  if (scanMode == SCAN_WIFI) {
    Serial.println("[WiFi] Already running.");
    return;
  }

  if (scanMode == SCAN_BLE) {
    Serial.println("[WiFi] Stop BLE first.");
    return;
  }

  radioOff();

  scanMode = SCAN_WIFI;

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setSleep(false);
  WiFi.disconnect(true, true);

  delay(300);

  Serial.println("[WiFi] SCAN STARTED — refreshes every 5 s. Type 'stop wifi' to stop.");

  runWifiScan();
  lastWifiScan = millis();
}

void stopWifi() {
  if (scanMode != SCAN_WIFI) {
    Serial.println("[WiFi] Not running.");
    return;
  }

  WiFi.scanDelete();
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  scanMode = SCAN_IDLE;

  Serial.println("[WiFi] SCAN STOPPED");
}

void initBleIfNeeded() {
  if (bleInitialized) return;

  if (!NimBLEDevice::init("Cantenna_Scanner")) {
    Serial.println("[BLE] NimBLE init failed.");
    return;
  }

  bleScan = NimBLEDevice::getScan();

  bleScan->setActiveScan(true);
  bleScan->setInterval(100);
  bleScan->setWindow(100);
  bleScan->setMaxResults(50);
  bleScan->setDuplicateFilter(true);

  bleInitialized = true;
}

void printBleResults() {
  if (bleScan == nullptr) return;

  NimBLEScanResults results = bleScan->getResults();
  int count = results.getCount();

  if (count == 0) {
    Serial.println("[BLE] No devices found.");
  } else {
    Serial.println("[BLE] Devices found: " + String(count));
    Serial.println();
    Serial.println("  #    RSSI   ADDRESS              NAME");
    Serial.println("  ------------------------------------------------------------");

    for (int i = 0; i < count; i++) {
      const NimBLEAdvertisedDevice* device = results.getDevice(i);
      if (device == nullptr) continue;

      std::string nameStd = device->getName();
      String name = nameStd.length() > 0 ? String(nameStd.c_str()) : "(no name)";

      if (name.length() > 28) {
        name = name.substring(0, 28);
      }

      char line[128];
      snprintf(
        line,
        sizeof(line),
        "  %-4d %-6d %-20s %s",
        i + 1,
        device->getRSSI(),
        device->getAddress().toString().c_str(),
        name.c_str()
      );

      Serial.println(line);
    }

    Serial.println("  ------------------------------------------------------------");
  }

  bleScan->clearResults();
}

void startBleScanCycle() {
  if (scanMode != SCAN_BLE || bleScan == nullptr || bleScan->isScanning()) return;

  Serial.println("\n[BLE] Scanning...");

  bleScan->clearResults();

  if (!bleScan->start(BLE_SCAN_DURATION_MS, false, true)) {
    Serial.println("[BLE] Failed to start scan.");
    return;
  }

  bleScanInProgress = true;
}

void handleBleScanner() {
  if (scanMode != SCAN_BLE || bleScan == nullptr) return;

  if (bleScanInProgress && !bleScan->isScanning()) {
    bleScanInProgress = false;
    printBleResults();
    lastBleScan = millis();
  }

  if (!bleScanInProgress && millis() - lastBleScan >= BLE_SCAN_INTERVAL_MS) {
    startBleScanCycle();
  }
}

void startBle() {
  if (scanMode == SCAN_BLE) {
    Serial.println("[BLE] Already running.");
    return;
  }

  if (scanMode == SCAN_WIFI) {
    Serial.println("[BLE] Stop WiFi first.");
    return;
  }

  radioOff();
  initBleIfNeeded();

  if (!bleInitialized || bleScan == nullptr) {
    Serial.println("[BLE] Could not initialize scanner.");
    scanMode = SCAN_IDLE;
    return;
  }

  scanMode = SCAN_BLE;
  lastBleScan = 0;
  bleScanInProgress = false;

  Serial.println("[BLE] SCAN STARTED — scans every 5 s. Type 'stop ble' to stop.");

  startBleScanCycle();
}

void stopBle() {
  if (scanMode != SCAN_BLE) {
    Serial.println("[BLE] Not running.");
    return;
  }

  if (bleScan != nullptr) {
    if (bleScan->isScanning()) {
      bleScan->stop();
    }

    bleScan->clearResults();
  }

  bleScanInProgress = false;
  scanMode = SCAN_IDLE;

  Serial.println("[BLE] SCAN STOPPED");
}

void checkSerialCommand() {
  static String cmd = "";

  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmd.length() == 0) return;

      cmd.trim();
      cmd.toLowerCase();

      if      (cmd == "start wifi") startWifi();
      else if (cmd == "stop wifi")  stopWifi();
      else if (cmd == "start ble")  startBle();
      else if (cmd == "stop ble")   stopBle();
      else if (cmd == "help")       printCommands();
      else                          Serial.println("Unknown command. Type 'help'.");

      cmd = "";
      return;
    }

    if (c == 8 || c == 127) {
      if (cmd.length() > 0) {
        cmd.remove(cmd.length() - 1);
      }
      continue;
    }

    if (c >= 32 && c <= 126) {
      cmd += c;
    }
  }
}

void setup() {
  waitForSerial();
  printBanner();
  radioOff();

  Serial.println("Cantenna Scanner Ready");

  printCommands();
}

void loop() {
  checkSerialCommand();

  if (scanMode == SCAN_WIFI && millis() - lastWifiScan >= WIFI_SCAN_INTERVAL_MS) {
    runWifiScan();
    lastWifiScan = millis();
  }

  handleBleScanner();
}