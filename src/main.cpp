#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ========================================================
// 🛡️ Kids Guardian Smartwatch - M5StickC PLUS 1.1
// ========================================================

// BLE UUID Definitions (Standard 128-bit UUIDs)
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_INFO_UUID      "19b10001-e8f2-537e-4f6c-d104768a1214" // Parent Info (Read/Write)
#define CHAR_TIME_UUID      "19b10002-e8f2-537e-4f6c-d104768a1214" // Time Sync (Write)
#define CHAR_COMMAND_UUID   "19b10003-e8f2-537e-4f6c-d104768a1214" // Command & SOS (Read/Write/Notify)
#define CHAR_BATTERY_UUID   "19b10004-e8f2-537e-4f6c-d104768a1214" // Battery % (Read/Notify)

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

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool hasBeenConnectedEver = false;

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
// 📡 BLE Server Callbacks
// ==========================================
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        hasBeenConnectedEver = true;

        // Auto-recover from alarm when reconnected
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            currentState = STATE_NORMAL;
            stopBuzzer();
            M5.Lcd.fillScreen(BLACK);
        }
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;

        // If previously connected and armed, trigger Out-of-Range Alarm
        if (alarmArmEnabled && hasBeenConnectedEver) {
            currentState = STATE_ALARM_OUT_OF_RANGE;
            alarmTriggerTime = millis();
            M5.Axp.ScreenBreath(100); // 100% maximum brightness during alarm
            M5.Lcd.fillScreen(RED);
        }

        // Restart Advertising so phone can reconnect
        BLEDevice::startAdvertising();
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
                parentName = payload.substring(0, firstSep);
                if (secondSep > firstSep) {
                    parentPhone = payload.substring(firstSep + 1, secondSep);
                    childName = payload.substring(secondSep + 1);
                } else {
                    parentPhone = payload.substring(firstSep + 1);
                }
                saveSettings();
                M5.Lcd.fillScreen(BLACK);
                drawNormalWatchFace();
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
                RTC_TimeTypeDef TimeStruct;
                TimeStruct.Hours   = hour;
                TimeStruct.Minutes = minute;
                TimeStruct.Seconds = second;
                M5.Rtc.SetTime(&TimeStruct);

                RTC_DateTypeDef DateStruct;
                DateStruct.Year    = year;
                DateStruct.Month   = month;
                DateStruct.Date    = day;
                DateStruct.WeekDay = 1;
                M5.Rtc.SetDate(&DateStruct);

                playChime();
                M5.Lcd.fillScreen(BLACK);
                drawNormalWatchFace();
            }
        }
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            uint8_t cmd = (uint8_t)value[0];
            if (cmd == 0x01) {
                // Find My Watch command -> Sound Loud Buzzer at 4200 Hz
                for (int i = 0; i < 4; i++) {
                    M5.Beep.tone(4200);
                    delay(160);
                    M5.Beep.mute();
                    delay(80);
                }
            } else if (cmd == 0x02) {
                // Stop buzzer
                stopBuzzer();
            } else if (cmd == 0x03) {
                // Simulate lost mode
                currentState = STATE_ALARM_OUT_OF_RANGE;
                alarmTriggerTime = millis();
                M5.Axp.ScreenBreath(100);
                M5.Lcd.fillScreen(RED);
            }
        }
    }
};

// ==========================================
// ⚙️ Persistent Storage (NVS)
// ==========================================
void loadSettings() {
    prefs.begin("kidswatch", true);
    parentName  = prefs.getString("p_name", "Mom/Dad");
    parentPhone = prefs.getString("p_phone", "0812345678");
    childName   = prefs.getString("c_name", "Kiddo");
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

    pService->start();

    // Start Advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // iOS optimization
    pAdvertising->setMinPreferred(0x12);

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
    // 1. Button Inputs
    // ----------------------------------------------------
    // Button A (Front M5): Long press (2 sec) -> Send SOS
    if (M5.BtnA.isPressed()) {
        if (!isButtonAPressed) {
            isButtonAPressed = true;
            buttonAPressedTime = currentMillis;
        } else if (currentMillis - buttonAPressedTime > 2000) {
            // SOS Triggered!
            currentState = STATE_SOS;
            sosDisplayUntil = currentMillis + 4000;
            M5.Lcd.fillScreen(ORANGE);

            if (deviceConnected && pCommandChar) {
                uint8_t sosCmd[] = { 0xFF }; // SOS signal
                pCommandChar->setValue(sosCmd, 1);
                pCommandChar->notify();
            }

            // High-pitched alert sound
            M5.Axp.ScreenBreath(100);
            M5.Beep.tone(4500);
            delay(350);
            M5.Beep.mute();
            buttonAPressedTime = currentMillis + 10000; // prevent repeated triggers
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
        } else {
            drawSOSScreen();
        }
    } else { // STATE_NORMAL
        stopBuzzer();
        if (currentMillis - lastDisplayUpdate >= 500) {
            lastDisplayUpdate = currentMillis;
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
    M5.Lcd.printf("ID: %s", childName.c_str());

    // BLE Status
    if (deviceConnected) {
        M5.Lcd.setTextColor(GREEN, BLACK);
        M5.Lcd.setCursor(130, 6);
        M5.Lcd.print("[BLE LINKED]");
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

    // --- Date String ---
    char dateBuffer[20];
    sprintf(dateBuffer, "%04d-%02d-%02d", DateStruct.Year, DateStruct.Month, DateStruct.Date);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString(dateBuffer, 55, 82);

    // --- Bottom Status Bar ---
    M5.Lcd.drawFastHLine(0, 108, 240, TFT_NAVY);
    M5.Lcd.setTextSize(1);
    if (deviceConnected) {
        M5.Lcd.setTextColor(TFT_GREENYELLOW, BLACK);
        M5.Lcd.drawString("* Parent Link Protected *", 45, 118);
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

    // Full-Width Emergency Info Card (232x107)
    M5.Lcd.fillRect(4, 24, 232, 107, BLACK);
    M5.Lcd.drawRect(4, 24, 232, 107, WHITE);

    // Child Name
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.setCursor(12, 30);
    M5.Lcd.printf("CHILD: %s", childName.c_str());

    // Parent Name
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(12, 48);
    M5.Lcd.printf("PARENT: %s", parentName.c_str());

    // Big Bold Emergency Phone Number
    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(TFT_GREENYELLOW, BLACK);
    M5.Lcd.setCursor(12, 68);
    M5.Lcd.print(parentPhone.c_str());

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
    M5.Lcd.drawString("SOS SENT!", 40, 30);

    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Alerting Parent...", 25, 75);
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