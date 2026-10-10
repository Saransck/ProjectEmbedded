#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>

// ========================================================
// 🛡️ Kids Guardian Smartwatch - M5StickC PLUS 1.1
// ========================================================

// BLE UUID Definitions (Standard 128-bit UUIDs)
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_INFO_UUID      "19b10001-e8f2-537e-4f6c-d104768a1214" // Parent Info (Read/Write)
#define CHAR_TIME_UUID      "19b10002-e8f2-537e-4f6c-d104768a1214" // Time Sync (Write)
#define CHAR_COMMAND_UUID   "19b10003-e8f2-537e-4f6c-d104768a1214" // Command & SOS (Read/Write/Notify)
#define CHAR_BATTERY_UUID   "19b10004-e8f2-537e-4f6c-d104768a1214" // Battery % (Read/Notify)
#define CHAR_RANGE_UUID     "19b10005-e8f2-537e-4f6c-d104768a1214" // Range & RSSI (Read/Notify)

// Operating States
enum WatchState {
    STATE_NORMAL,
    STATE_ALARM_OUT_OF_RANGE,
    STATE_SOS
};

WatchState currentState = STATE_NORMAL;

// Preferences (Non-Volatile Storage in ESP32 Flash)
Preferences prefs;
String parentName = "Mom/Dad";
String parentPhone = "0812345678";
String childName = "Kiddo";
bool alarmArmEnabled = true;

// BLE Objects
BLEServer* pServer = nullptr;
BLECharacteristic* pInfoChar = nullptr;
BLECharacteristic* pTimeChar = nullptr;
BLECharacteristic* pCommandChar = nullptr;
BLECharacteristic* pBatteryChar = nullptr;
BLECharacteristic* pRangeChar = nullptr;

volatile bool deviceConnected = false;
bool oldDeviceConnected = false;
bool hasBeenConnectedEver = false;

// RSSI & Range Tracking (Multi-level Distance Warning)
esp_bd_addr_t peerAddress;
volatile bool hasPeerAddress = false;
volatile int8_t latestRssi = 0;
volatile bool newRssiAvailable = false;
int8_t filteredRssi = -60;
uint8_t currentRangeLevel = 1; // 1 = Safe (ปกติ), 2 = Warning (เริ่มห่าง), 3 = Far (ไกลมาก)
unsigned long lastRssiReadTime = 0;

// Thread-safe flags between BLE tasks (Core 0) and main loop (Core 1)
volatile bool pendingTimeSync = false;
RTC_TimeTypeDef pendingTime;
RTC_DateTypeDef pendingDate;

volatile bool pendingInfoSave = false;
String pendingParentName = "";
String pendingParentPhone = "";
String pendingChildName = "";

volatile uint8_t pendingCommand = 0;
volatile bool pendingAlarmRecover = false;
volatile bool needScreenRedraw = true;

// Timers & Control Variables
unsigned long lastDisplayUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastBeepToggle = 0;
unsigned long alarmTriggerTime = 0;
unsigned long snoozeUntil = 0;
unsigned long sosDisplayUntil = 0;
unsigned long buttonAPressedTime = 0;
bool isButtonAPressed = false;
bool buzzerActive = false;
bool buzzerHighTone = false;

// Brightness control (0-100 scale, default to 100 for maximum brightness)
uint8_t brightnessLevel = 100;

// Forward Declarations
void updateDisplay();
void drawNormalWatchFace();
void drawEmergencyScreen();
void drawSOSScreen();
void startBuzzerAlarm();
void stopBuzzer();
void playChime();
void loadSettings();
void saveSettings();
int getBatteryPercentage();

// ==========================================
// 📡 Custom GAP Event Handler (Read RSSI)
// ==========================================
void customGapHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (event == ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT) {
        if (param != nullptr && param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            latestRssi = param->read_rssi_cmpl.rssi;
            newRssiAvailable = true;
        }
    }
}

// ==========================================
// 📡 BLE Server Callbacks
// ==========================================
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
        hasBeenConnectedEver = true;
        pendingAlarmRecover = true;
    }

    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        deviceConnected = true;
        hasBeenConnectedEver = true;
        pendingAlarmRecover = true;
        if (param != nullptr) {
            memcpy(peerAddress, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            hasPeerAddress = true;
        }
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        hasPeerAddress = false;
        currentRangeLevel = 0;
        // Do NOT execute display/hardware I/O or startAdvertising here!
        // The main loop() will handle safe restart and alarm triggering.
    }
};

// ==========================================
// 📩 BLE Characteristic Callbacks
// ==========================================
class InfoCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            String payload = String(value.c_str());
            // Payload format: "ParentName|ParentPhone|ChildName"
            int firstSep = payload.indexOf('|');
            int secondSep = payload.indexOf('|', firstSep + 1);

            if (firstSep > 0) {
                pendingParentName = payload.substring(0, firstSep);
                if (secondSep > firstSep) {
                    pendingParentPhone = payload.substring(firstSep + 1, secondSep);
                    pendingChildName = payload.substring(secondSep + 1);
                } else {
                    pendingParentPhone = payload.substring(firstSep + 1);
                    pendingChildName = childName;
                }
                pendingInfoSave = true;
            }
        }
    }
};

class TimeCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            // Format: "YYYY,MM,DD,hh,mm,ss"
            int year, month, day, hour, minute, second;
            if (sscanf(value.c_str(), "%d,%d,%d,%d,%d,%d", &year, &month, &day, &hour, &minute, &second) == 6) {
                pendingTime.Hours   = hour;
                pendingTime.Minutes = minute;
                pendingTime.Seconds = second;

                pendingDate.Year    = year;
                pendingDate.Month   = month;
                pendingDate.Date    = day;
                pendingDate.WeekDay = 1;

                pendingTimeSync = true;
            }
        }
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            pendingCommand = (uint8_t)value[0];
        }
    }
};

// Clean and filter printable ASCII characters so TFT_eSPI screen never renders junk characters
String sanitizeAscii(const String& str, const String& fallback) {
    String out = "";
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = (unsigned char)str[i];
        if (c >= 32 && c <= 126) {
            out += (char)c;
        }
    }
    out.trim();
    if (out.length() == 0) return fallback;
    return out;
}

// ==========================================
// ⚙️ Persistent Storage (NVS)
// ==========================================
void loadSettings() {
    prefs.begin("kidswatch", true);
    parentName  = sanitizeAscii(prefs.getString("p_name", "Mom/Dad"), "Mom/Dad");
    parentPhone = sanitizeAscii(prefs.getString("p_phone", "0812345678"), "0812345678");
    childName   = sanitizeAscii(prefs.getString("c_name", "Kiddo"), "Kiddo");
    prefs.end();
}

void saveSettings() {
    prefs.begin("kidswatch", false);
    prefs.putString("p_name", parentName);
    prefs.putString("p_phone", parentPhone);
    prefs.putString("c_name", childName);
    prefs.end();
}

int getBatteryPercentage() {
    float vbat = M5.Axp.GetBatVoltage();
    int pct = (int)((vbat - 3.2f) / (4.15f - 3.2f) * 100.0f);
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
}

void playChime() {
    M5.Beep.tone(3800);
    delay(80);
    M5.Beep.tone(4500);
    delay(120);
    M5.Beep.mute();
}

// ==========================================
// 🚀 Setup
// ==========================================
void setup() {
    M5.begin();
    M5.Axp.ScreenBreath(100); // 100% Maximum Brightness
    M5.Lcd.setRotation(1);    // Landscape mode (240x135)
    M5.Lcd.fillScreen(BLACK);

    loadSettings();

    // Splash screen
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("KIDS GUARDIAN", 35, 30);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.drawString("Smart BLE Anti-Lost Watch", 45, 60);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.drawString("Starting BLE...", 80, 85);
    delay(1000);

    // Initialize BLE
    String devName = "KidsWatch-" + childName;
    if (devName.length() > 20) {
        devName = devName.substring(0, 20);
    }
    BLEDevice::init(devName.c_str());
    BLEDevice::setMTU(517);

    // Boost BLE transmit power to Maximum (+9dBm) for rock-solid stability & range
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_CONN_HDL0, ESP_PWR_LVL_P9);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    // 1. Info Characteristic
    pInfoChar = pService->createCharacteristic(
        CHAR_INFO_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pInfoChar->setCallbacks(new InfoCallbacks());
    pInfoChar->addDescriptor(new BLE2902());
    String initialPayload = parentName + "|" + parentPhone + "|" + childName;
    pInfoChar->setValue(initialPayload.c_str());

    // 2. Time Sync Characteristic
    pTimeChar = pService->createCharacteristic(
        CHAR_TIME_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pTimeChar->setCallbacks(new TimeCallbacks());

    // 3. Command & SOS Characteristic
    pCommandChar = pService->createCharacteristic(
        CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCommandChar->setCallbacks(new CommandCallbacks());
    pCommandChar->addDescriptor(new BLE2902());

    // 4. Battery Characteristic
    pBatteryChar = pService->createCharacteristic(
        CHAR_BATTERY_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pBatteryChar->addDescriptor(new BLE2902());

    // 5. Range & RSSI Characteristic (Proximity Tracking)
    pRangeChar = pService->createCharacteristic(
        CHAR_RANGE_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pRangeChar->addDescriptor(new BLE2902());
    uint8_t initRange[2] = { 1, 60 }; // Level 1 (Safe), 60dBm
    pRangeChar->setValue(initRange, 2);

    BLEDevice::setCustomGapHandler(customGapHandler);

    pService->start();

    // Start Advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);

    // Apple BLE accessory guidelines: min connection interval >= 15ms (0x10 = 20ms, 0x30 = 60ms)
    pAdvertising->setMinPreferred(0x10);
    pAdvertising->setMaxPreferred(0x30);

    BLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setCompleteServices(BLEUUID(SERVICE_UUID));
    pAdvertising->setAdvertisementData(advData);

    BLEAdvertisementData scanData;
    scanData.setName(devName.c_str());
    pAdvertising->setScanResponseData(scanData);

    BLEDevice::startAdvertising();

    M5.Lcd.fillScreen(BLACK);
    drawNormalWatchFace();
}

// ==========================================
// 🔄 Main Loop
// ==========================================
void loop() {
    M5.update();
    unsigned long currentMillis = millis();

    // ----------------------------------------------------
    // 0. Thread-Safe BLE Event Handling & Advertising Restart
    // ----------------------------------------------------
    // Connecting Transition
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
        needScreenRedraw = true;
        currentRangeLevel = 1;
        alarmArmEnabled = true; // Re-arm alarm on reconnection
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            currentState = STATE_NORMAL;
            stopBuzzer();
            M5.Lcd.fillScreen(BLACK);
        }
    }

    // Disconnecting Transition
    if (!deviceConnected && oldDeviceConnected) {
        oldDeviceConnected = false;
        needScreenRedraw = true;
        currentRangeLevel = 0;

        if (alarmArmEnabled && hasBeenConnectedEver) {
            currentState = STATE_ALARM_OUT_OF_RANGE;
            alarmTriggerTime = currentMillis;
            M5.Axp.ScreenBreath(100);
            M5.Lcd.fillScreen(RED);
        }

        // Restart Advertising gracefully from loop task
        delay(150); // Give the BT controller time to cleanly complete teardown
        if (pServer) {
            pServer->startAdvertising();
        }
    }

    // Pending Alarm Recovery
    if (pendingAlarmRecover) {
        pendingAlarmRecover = false;
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            currentState = STATE_NORMAL;
            stopBuzzer();
            M5.Lcd.fillScreen(BLACK);
            needScreenRedraw = true;
        }
    }

    // Pending Time Sync from BLE
    if (pendingTimeSync) {
        pendingTimeSync = false;
        M5.Rtc.SetTime(&pendingTime);
        M5.Rtc.SetDate(&pendingDate);
        playChime();
        M5.Lcd.fillScreen(BLACK);
        drawNormalWatchFace();
    }

    // Pending Info Save from BLE
    if (pendingInfoSave) {
        pendingInfoSave = false;
        parentName = sanitizeAscii(pendingParentName, "Mom/Dad");
        parentPhone = sanitizeAscii(pendingParentPhone, "0812345678");
        if (pendingChildName.length() > 0) {
            childName = sanitizeAscii(pendingChildName, "Kiddo");
        }
        saveSettings();
        M5.Lcd.fillScreen(BLACK);
        drawNormalWatchFace();
    }

    // Pending Command from BLE
    if (pendingCommand != 0) {
        uint8_t cmd = pendingCommand;
        pendingCommand = 0;
        if (cmd == 0x01) {
            // Find My Watch command -> Sound Buzzer
            for (int i = 0; i < 4; i++) {
                M5.Beep.tone(4200);
                delay(140);
                M5.Beep.mute();
                delay(60);
            }
        } else if (cmd == 0x02) {
            stopBuzzer();
        } else if (cmd == 0x03) {
            currentState = STATE_ALARM_OUT_OF_RANGE;
            alarmTriggerTime = currentMillis;
            M5.Axp.ScreenBreath(100);
            M5.Lcd.fillScreen(RED);
        } else if (cmd == 0x04) {
            // Safe Disconnect / Disarm from web app
            alarmArmEnabled = false;
            hasBeenConnectedEver = false;
            currentState = STATE_NORMAL;
            stopBuzzer();
            M5.Lcd.fillScreen(BLACK);
            drawNormalWatchFace();
        }
    }

    // ----------------------------------------------------
    // 0.1 Periodic Proximity & RSSI Distance Tracking
    // ----------------------------------------------------
    if (deviceConnected && hasPeerAddress && (currentMillis - lastRssiReadTime >= 1200)) {
        lastRssiReadTime = currentMillis;
        esp_ble_gap_read_rssi(peerAddress);
    }

    if (newRssiAvailable) {
        newRssiAvailable = false;
        if (latestRssi != 0 && latestRssi != 127) {
            // Exponential moving average filter for smooth distance readings
            filteredRssi = (int8_t)((filteredRssi * 3 + latestRssi) / 4);

            // Proximity thresholds:
            // RSSI > -72 dBm: Level 1 (Safe / ใกล้ตัว 0 - 5m)
            // -72 dBm to -84 dBm: Level 2 (Warning / เริ่มออกห่าง 5 - 10m)
            // <= -85 dBm: Level 3 (Far / เสี่ยงหลุดระยะ 10 - 15m+)
            uint8_t newLevel = 1;
            if (filteredRssi <= -85) {
                newLevel = 3;
            } else if (filteredRssi <= -72) {
                newLevel = 2;
            } else {
                newLevel = 1;
            }

            if (newLevel != currentRangeLevel) {
                // If moving from Safe to Warning (เด็กเริ่มเดินออกห่าง), soft reminder beep on watch
                if (newLevel == 2 && currentRangeLevel == 1) {
                    M5.Beep.tone(3200);
                    delay(50);
                    M5.Beep.mute();
                }
                currentRangeLevel = newLevel;
                needScreenRedraw = true;
            }

            // Send real-time notification to Web Companion App
            if (pRangeChar && deviceConnected) {
                uint8_t rangeData[2] = { currentRangeLevel, (uint8_t)abs(filteredRssi) };
                pRangeChar->setValue(rangeData, 2);
                pRangeChar->notify();
            }
        }
    }

    // ----------------------------------------------------
    // 1. Button Inputs
    // ----------------------------------------------------
    // Button A (Front M5 Button):
    // Press -> Send Alert (0xFE) to Web Companion
    // Long press (>2.5 sec) -> Escalate to Emergency SOS (0xFF)
    if (M5.BtnA.wasPressed()) {
        isButtonAPressed = true;
        buttonAPressedTime = currentMillis;

        // Immediate Alert on Button A press
        currentState = STATE_SOS;
        sosDisplayUntil = currentMillis + 4000;
        M5.Lcd.fillScreen(ORANGE);
        drawSOSScreen();

        if (deviceConnected && pCommandChar) {
            uint8_t alertCmd[] = { 0xFE }; // 0xFE = Button A Alert to Web
            pCommandChar->setValue(alertCmd, 1);
            pCommandChar->notify();
        }

        // Distinct alert chirp on watch
        M5.Axp.ScreenBreath(100);
        M5.Beep.tone(4200);
        delay(100);
        M5.Beep.tone(4800);
        delay(140);
        M5.Beep.mute();
    }

    if (M5.BtnA.isPressed()) {
        if (isButtonAPressed && (currentMillis - buttonAPressedTime > 2500)) {
            isButtonAPressed = false; // Trigger once
            currentState = STATE_SOS;
            sosDisplayUntil = currentMillis + 5000;
            M5.Lcd.fillScreen(RED);
            drawSOSScreen();

            if (deviceConnected && pCommandChar) {
                uint8_t sosCmd[] = { 0xFF }; // 0xFF = Emergency SOS
                pCommandChar->setValue(sosCmd, 1);
                pCommandChar->notify();
            }

            M5.Beep.tone(4500);
            delay(300);
            M5.Beep.mute();
        }
    } else {
        isButtonAPressed = false;
    }

    // Button B (Side Button):
    // In Alarm mode -> Snooze buzzer for 45s while keeping screen visible
    // In Normal mode -> Cycle brightness between 100% (High) and 60% (Mid)
    if (M5.BtnB.wasPressed()) {
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            snoozeUntil = currentMillis + 45000;
            stopBuzzer();
        } else {
            brightnessLevel = (brightnessLevel == 100) ? 60 : 100;
            M5.Axp.ScreenBreath(brightnessLevel);
        }
    }

    // ----------------------------------------------------
    // 2. State Machine Handling
    // ----------------------------------------------------
    if (currentState == STATE_ALARM_OUT_OF_RANGE) {
        // Run Siren if not snoozed
        if (currentMillis > snoozeUntil) {
            startBuzzerAlarm();
        } else {
            stopBuzzer();
        }

        // Refresh emergency alert screen every 1 second
        if (currentMillis - lastDisplayUpdate >= 1000) {
            lastDisplayUpdate = currentMillis;
            drawEmergencyScreen();
        }
    } else if (currentState == STATE_SOS) {
        if (currentMillis > sosDisplayUntil) {
            currentState = STATE_NORMAL;
            M5.Lcd.fillScreen(BLACK);
            needScreenRedraw = true;
        } else {
            drawSOSScreen();
        }
    } else { // STATE_NORMAL
        stopBuzzer();
        if (currentMillis - lastDisplayUpdate >= 1000 || needScreenRedraw) {
            lastDisplayUpdate = currentMillis;
            needScreenRedraw = false;
            drawNormalWatchFace();
        }
    }

    // ----------------------------------------------------
    // 3. Periodic Battery Sync over BLE (every 10s)
    // ----------------------------------------------------
    if (deviceConnected && (currentMillis - lastBatteryUpdate >= 10000)) {
        lastBatteryUpdate = currentMillis;
        uint8_t bat = (uint8_t)getBatteryPercentage();
        pBatteryChar->setValue(&bat, 1);
        pBatteryChar->notify();
    }
}

// ==========================================
// 🎨 Display Functions
// ==========================================

void drawNormalWatchFace() {
    RTC_TimeTypeDef TimeStruct;
    RTC_DateTypeDef DateStruct;
    M5.Rtc.GetTime(&TimeStruct);
    M5.Rtc.GetDate(&DateStruct);

    int bat = getBatteryPercentage();

    // --- Top Bar ---
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.setCursor(8, 6);
    String safeChild = sanitizeAscii(childName, "Child");
    M5.Lcd.printf("ID: %s", safeChild.c_str());

    // BLE Status & Distance Warning
    if (deviceConnected) {
        if (currentRangeLevel == 2) {
            M5.Lcd.setTextColor(YELLOW, BLACK);
            M5.Lcd.setCursor(110, 6);
            M5.Lcd.printf("[FAR %ddB]", filteredRssi);
        } else if (currentRangeLevel == 3) {
            M5.Lcd.setTextColor(ORANGE, BLACK);
            M5.Lcd.setCursor(110, 6);
            M5.Lcd.printf("[LIMIT %ddB]", filteredRssi);
        } else {
            M5.Lcd.setTextColor(GREEN, BLACK);
            M5.Lcd.setCursor(110, 6);
            M5.Lcd.printf("[NEAR %ddB]", filteredRssi);
        }
    } else {
        M5.Lcd.setTextColor(TFT_DARKGREY, BLACK);
        M5.Lcd.setCursor(130, 6);
        M5.Lcd.print("[WAITING BLE]");
    }

    // Battery %
    uint16_t batColor = (bat < 20) ? RED : ((bat < 50) ? YELLOW : GREEN);
    M5.Lcd.setTextColor(batColor, BLACK);
    M5.Lcd.setCursor(210, 6);
    M5.Lcd.printf("%d%%", bat);

    M5.Lcd.drawFastHLine(0, 18, 240, TFT_DARKGREY);

    // --- Big Digital Clock (HH:MM:SS) ---
    char timeBuffer[10];
    sprintf(timeBuffer, "%02d:%02d:%02d", TimeStruct.Hours, TimeStruct.Minutes, TimeStruct.Seconds);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(4);
    M5.Lcd.drawString(timeBuffer, 18, 36);

    // --- Date String (Day / Month / Year) ---
    char dateBuffer[20];
    sprintf(dateBuffer, "%02d/%02d/%04d", DateStruct.Date, DateStruct.Month, DateStruct.Year);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString(dateBuffer, 60, 82);

    // --- Bottom Status Bar ---
    M5.Lcd.drawFastHLine(0, 108, 240, TFT_NAVY);
    M5.Lcd.setTextSize(1);
    if (deviceConnected) {
        if (currentRangeLevel == 2) {
            M5.Lcd.setTextColor(YELLOW, BLACK);
            M5.Lcd.drawString("! Warning: Getting Far !", 40, 118);
        } else if (currentRangeLevel == 3) {
            M5.Lcd.setTextColor(ORANGE, BLACK);
            M5.Lcd.drawString("!! Very Far! Please Return !!", 25, 118);
        } else {
            M5.Lcd.setTextColor(TFT_GREENYELLOW, BLACK);
            M5.Lcd.drawString("* Parent Link Protected *", 45, 118);
        }
    } else if (hasBeenConnectedEver) {
        M5.Lcd.setTextColor(RED, BLACK);
        M5.Lcd.drawString("! BLE LINK LOST !", 70, 118);
    } else {
        M5.Lcd.setTextColor(CYAN, BLACK);
        M5.Lcd.drawString("Connect via Guardian Web App", 30, 118);
    }
}

void drawEmergencyScreen() {
    static bool flash = false;
    flash = !flash;
    uint16_t bg = flash ? RED : TFT_MAROON;

    M5.Lcd.fillScreen(bg);

    // Flashing Header Banner
    M5.Lcd.setTextColor(WHITE, bg);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("! LOST CHILD !", 38, 4);

    String safeChild = sanitizeAscii(childName, "Kiddo");
    String safeParent = sanitizeAscii(parentName, "Parent");
    String safePhone = sanitizeAscii(parentPhone, "0812345678");

    // Full-Width Emergency Info Card (232x107)
    M5.Lcd.fillRect(4, 24, 232, 107, BLACK);
    M5.Lcd.drawRect(4, 24, 232, 107, WHITE);

    // Child Name
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.setCursor(12, 30);
    M5.Lcd.printf("CHILD: %s", safeChild.c_str());

    // Parent Name
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(12, 48);
    M5.Lcd.printf("PARENT: %s", safeParent.c_str());

    // Big Bold Emergency Phone Number
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_GREENYELLOW, BLACK);
    M5.Lcd.setCursor(12, 68);
    M5.Lcd.print(safePhone.c_str());

    // Action Hint
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.drawString("Please call parent immediately!", 22, 96);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
    M5.Lcd.drawString("[Press Side Btn to Snooze Alarm]", 18, 112);
}

void drawSOSScreen() {
    M5.Lcd.setTextColor(BLACK, ORANGE);
    M5.Lcd.setTextSize(3);
    M5.Lcd.drawString("ALERT SENT!", 25, 30);

    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Calling Parent...", 25, 75);
}

// ==========================================
// 🔊 Buzzer Controller
// ==========================================
void startBuzzerAlarm() {
    unsigned long now = millis();
    // Fast alternating dual-tone siren at resonant frequency peak (4000Hz <-> 4500Hz)
    // Continuous sound switching every 120ms for maximum acoustic loudness and urgency
    if (now - lastBeepToggle >= 120) {
        lastBeepToggle = now;
        buzzerHighTone = !buzzerHighTone;
        M5.Beep.tone(buzzerHighTone ? 4500 : 4000);
        buzzerActive = true;
    }
}

void stopBuzzer() {
    M5.Beep.mute();
    buzzerActive = false;
}