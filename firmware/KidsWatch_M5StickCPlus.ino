#include <M5StickCPlus.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ==========================================
// 🛡️ Kids Guardian Watch - M5StickC PLUS 1.1
// ==========================================

// BLE UUID Definitions
#define SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_INFO_UUID      "19b10001-e8f2-537e-4f6c-d104768a1214" // Parent Info (Read/Write)
#define CHAR_TIME_UUID      "19b10002-e8f2-537e-4f6c-d104768a1214" // Time Sync (Write)
#define CHAR_COMMAND_UUID   "19b10003-e8f2-537e-4f6c-d104768a1214" // Command / SOS (Read/Write/Notify)
#define CHAR_BATTERY_UUID   "19b10004-e8f2-537e-4f6c-d104768a1214" // Battery % (Read/Notify)

// State Machine
enum WatchState {
    STATE_NORMAL,
    STATE_ALARM_OUT_OF_RANGE,
    STATE_SOS
};

WatchState currentState = STATE_NORMAL;

// Preferences (NVS storage)
Preferences prefs;
String parentName = "Mom/Dad";
String parentPhone = "0812345678";
String childName = "Kiddo";
bool alarmArmEnabled = true; // Alarm triggers only after initial connection or if armed

// BLE Globals
BLEServer* pServer = nullptr;
BLECharacteristic* pInfoChar = nullptr;
BLECharacteristic* pTimeChar = nullptr;
BLECharacteristic* pCommandChar = nullptr;
BLECharacteristic* pBatteryChar = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool hasBeenConnectedEver = false; // Arm alarm once connected

// Hardware Timers & Flags
unsigned long lastDisplayUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastBeepToggle = 0;
unsigned long alarmTriggerTime = 0;
unsigned long snoozeUntil = 0;
unsigned long sosDisplayUntil = 0;
unsigned long buttonAPressedTime = 0;
bool isButtonAPressed = false;
bool buzzerState = false;

// Display orientation: 1 or 3 = Landscape (240x135), 0 or 2 = Portrait (135x240)
// Landscape gives great width for big clock and phone numbers.
const uint8_t SCREEN_ROTATION = 1;

// Forward Declarations
void updateDisplay();
void drawNormalWatchFace();
void drawEmergencyScreen();
void drawSOSScreen();
void startBuzzerAlarm();
void stopBuzzer();
void loadSettings();
void saveSettings();

// ==========================================
// 📡 BLE Server Callbacks
// ==========================================
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        hasBeenConnectedEver = true;
        // Turn off alarm if it was sounding
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            currentState = STATE_NORMAL;
            stopBuzzer();
            M5.Lcd.fillScreen(BLACK);
        }
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        // Trigger Out-of-Range Alarm if watch was previously paired & armed
        if (alarmArmEnabled && hasBeenConnectedEver) {
            currentState = STATE_ALARM_OUT_OF_RANGE;
            alarmTriggerTime = millis();
            M5.Lcd.fillScreen(RED);
        }
        // Restart Advertising
        pServer->getAdvertising()->start();
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
                updateDisplay();
            }
        }
    }
};

class TimeCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() >= 6) {
            // Binary format: Year(2 bytes), Month(1), Date(1), Hour(1), Min(1), Sec(1)
            // Or String format: "YYYY,MM,DD,hh,mm,ss"
            String timeStr = String(value.c_str());
            int y, m, d, hh, mm, ss;
            if (sscanf(timeStr.c_str(), "%d,%d,%d,%d,%d,%d", &y, &m, &d, &hh, &mm, &ss) == 6) {
                RTC_TimeTypeDef TimeStruct;
                TimeStruct.Hours   = hh;
                TimeStruct.Minutes = mm;
                TimeStruct.Seconds = ss;
                M5.Rtc.SetTime(&TimeStruct);

                RTC_DateTypeDef DateStruct;
                DateStruct.Year    = y;
                DateStruct.Month   = m;
                DateStruct.Date    = d;
                M5.Rtc.SetData(&DateStruct);

                // Quick feedback beep
                M5.Beep.tone(2000);
                delay(80);
                M5.Beep.mute();
            }
        }
    }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            uint8_t cmd = (uint8_t)value[0];
            if (cmd == 0x01) { // Ping / Find Watch (Ring for 3 seconds)
                for (int i = 0; i < 3; i++) {
                    M5.Beep.tone(3500);
                    delay(150);
                    M5.Beep.mute();
                    delay(100);
                }
            } else if (cmd == 0x02) { // Arm / Disarm Alarm
                if (value.length() > 1) {
                    alarmArmEnabled = (value[1] == 1);
                }
            }
        }
    }
};

// ==========================================
// 💾 NVS Storage Helpers
// ==========================================
void loadSettings() {
    prefs.begin("guardian", true);
    parentName  = prefs.getString("p_name", "Mom/Dad");
    parentPhone = prefs.getString("p_phone", "0812345678");
    childName   = prefs.getString("c_name", "Kiddo");
    alarmArmEnabled = prefs.getBool("alarm_en", true);
    prefs.end();
}

void saveSettings() {
    prefs.begin("guardian", false);
    prefs.putString("p_name", parentName);
    prefs.putString("p_phone", parentPhone);
    prefs.putString("c_name", childName);
    prefs.putBool("alarm_en", alarmArmEnabled);
    prefs.end();
}

// ==========================================
// 🔋 Battery Calculation
// ==========================================
int getBatteryPercentage() {
    float vbat = M5.Axp.GetBatVoltage();
    // LiPo curve approximation for AXP192
    if (vbat <= 3.20) return 0;
    if (vbat >= 4.15) return 100;
    int pct = (int)((vbat - 3.20) / (4.15 - 3.20) * 100.0);
    return constrain(pct, 0, 100);
}

// ==========================================
// 🚀 Setup
// ==========================================
void setup() {
    M5.begin();
    M5.Lcd.setRotation(SCREEN_ROTATION);
    M5.Lcd.fillScreen(BLACK);
    M5.Axp.ScreenBreath(11); // High brightness

    loadSettings();

    // Init BLE
    String devName = "KidsWatch-" + childName;
    BLEDevice::init(devName.c_str());

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    // Parent Info Characteristic
    pInfoChar = pService->createCharacteristic(
        CHAR_INFO_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pInfoChar->setCallbacks(new InfoCallbacks());
    pInfoChar->addDescriptor(new BLE2902());
    String initialInfo = parentName + "|" + parentPhone + "|" + childName;
    pInfoChar->setValue(initialInfo.c_str());

    // Time Sync Characteristic
    pTimeChar = pService->createCharacteristic(
        CHAR_TIME_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pTimeChar->setCallbacks(new TimeCallbacks());

    // Command Characteristic (Two-way: Ring watch & SOS trigger)
    pCommandChar = pService->createCharacteristic(
        CHAR_COMMAND_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCommandChar->setCallbacks(new CommandCallbacks());
    pCommandChar->addDescriptor(new BLE2902());

    // Battery Level Characteristic
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
    pAdvertising->setMinPreferred(0x06); // iPhone connection optimization
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    drawNormalWatchFace();
}

// ==========================================
// 🔄 Main Loop
// ==========================================
void loop() {
    M5.update();
    unsigned long currentMillis = millis();

    // 1. Handle Button Inputs
    // Button A (Front M5): Long Press = SOS Alert
    if (M5.BtnA.isPressed()) {
        if (!isButtonAPressed) {
            isButtonAPressed = true;
            buttonAPressedTime = currentMillis;
        } else if (currentMillis - buttonAPressedTime > 2000) { // 2 seconds hold
            // Trigger SOS
            currentState = STATE_SOS;
            sosDisplayUntil = currentMillis + 4000;
            M5.Lcd.fillScreen(ORANGE);

            if (deviceConnected && pCommandChar) {
                uint8_t sosCmd[] = { 0xFF }; // SOS Flag to Phone App
                pCommandChar->setValue(sosCmd, 1);
                pCommandChar->notify();
            }

            // High pitch short alert
            M5.Beep.tone(4200);
            delay(200);
            M5.Beep.mute();
            buttonAPressedTime = currentMillis + 10000; // Prevent re-trigger until released
        }
    } else {
        isButtonAPressed = false;
    }

    // Button B (Side Button): Snooze Buzzer / Mute during Out-of-Range Alarm
    if (M5.BtnB.wasPressed()) {
        if (currentState == STATE_ALARM_OUT_OF_RANGE) {
            // Snooze beep for 45 seconds, but keep emergency screen visible!
            snoozeUntil = currentMillis + 45000;
            stopBuzzer();
        } else {
            // Cycle brightness to save battery
            static uint8_t brightnessLevel = 11;
            brightnessLevel = (brightnessLevel == 11) ? 8 : 11;
            M5.Axp.ScreenBreath(brightnessLevel);
        }
    }

    // 2. State Machine Handling
    if (currentState == STATE_ALARM_OUT_OF_RANGE) {
        // Run alarm buzzer if not snoozed
        if (currentMillis > snoozeUntil) {
            startBuzzerAlarm();
        } else {
            stopBuzzer();
        }

        // Redraw emergency screen every 1 second
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

    // 3. Periodic Battery Notification
    if (deviceConnected && (currentMillis - lastBatteryUpdate >= 10000)) {
        lastBatteryUpdate = currentMillis;
        uint8_t bat = (uint8_t)getBatteryPercentage();
        pBatteryChar->setValue(&bat, 1);
        pBatteryChar->notify();
    }
}

// ==========================================
// 🎨 Screen Rendering
// ==========================================

void drawNormalWatchFace() {
    RTC_TimeTypeDef TimeStruct;
    RTC_DateTypeDef DateStruct;
    M5.Rtc.GetTime(&TimeStruct);
    M5.Rtc.GetData(&DateStruct);

    int bat = getBatteryPercentage();

    // Top Status Bar: Child Name + BLE Icon + Battery
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.setCursor(8, 6);
    M5.Lcd.printf("ID: %s", childName.c_str());

    // BLE Status
    if (deviceConnected) {
        M5.Lcd.setTextColor(GREEN, BLACK);
        M5.Lcd.setCursor(140, 6);
        M5.Lcd.print("[BLE OK]");
    } else {
        M5.Lcd.setTextColor(TFT_DARKGREY, BLACK);
        M5.Lcd.setCursor(140, 6);
        M5.Lcd.print("[WAIT BLE]");
    }

    // Battery Percentage
    uint16_t batColor = (bat < 20) ? RED : ((bat < 50) ? YELLOW : GREEN);
    M5.Lcd.setTextColor(batColor, BLACK);
    M5.Lcd.setCursor(200, 6);
    M5.Lcd.printf("%d%%", bat);

    // Decorative line
    M5.Lcd.drawFastHLine(0, 18, 240, TFT_DARKGREY);

    // Large Digital Clock (HH:MM:SS)
    char timeBuffer[10];
    sprintf(timeBuffer, "%02d:%02d:%02d", TimeStruct.Hours, TimeStruct.Minutes, TimeStruct.Seconds);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(4);
    M5.Lcd.drawString(timeBuffer, 18, 36);

    // Date String
    char dateBuffer[20];
    sprintf(dateBuffer, "%04d-%02d-%02d", DateStruct.Year, DateStruct.Month, DateStruct.Date);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString(dateBuffer, 55, 82);

    // Bottom Guardian Status
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
    static bool flashState = false;
    flashState = !flashState;

    uint16_t bg = flashState ? RED : TFT_MAROON;
    M5.Lcd.fillScreen(bg);

    // Flashing Header
    M5.Lcd.setTextColor(WHITE, bg);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("! LOST CHILD !", 35, 6);

    // Emergency Details Card (Black background for readability)
    M5.Lcd.fillRect(4, 30, 232, 98, BLACK);
    M5.Lcd.drawRect(4, 30, 232, 98, WHITE);

    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(YELLOW, BLACK);
    M5.Lcd.setCursor(12, 38);
    M5.Lcd.printf("Child Name: %s", childName.c_str());

    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setCursor(12, 54);
    M5.Lcd.printf("Parent: %s", parentName.c_str());

    // Highlighted Phone Number
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREENYELLOW, BLACK);
    M5.Lcd.setCursor(12, 72);
    M5.Lcd.printf("TEL: %s", parentPhone.c_str());

    // Action instructions
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(CYAN, BLACK);
    M5.Lcd.drawString("Please call parents immediately", 20, 96);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
    M5.Lcd.drawString("[Press Side Btn to Snooze Alarm]", 15, 112);
}

void drawSOSScreen() {
    M5.Lcd.setTextColor(BLACK, ORANGE);
    M5.Lcd.setTextSize(3);
    M5.Lcd.drawString("SOS SENT!", 40, 25);

    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Alerting Parent...", 20, 75);
}

// ==========================================
// 🔊 Buzzer Controller
// ==========================================
void startBuzzerAlarm() {
    unsigned long now = millis();
    // Beep pattern: 250ms ON, 250ms OFF
    if (now - lastBeepToggle >= 250) {
        lastBeepToggle = now;
        buzzerState = !buzzerState;
        if (buzzerState) {
            M5.Beep.tone(3200); // 3.2 kHz loud siren pitch
        } else {
            M5.Beep.mute();
        }
    }
}

void stopBuzzer() {
    M5.Beep.mute();
    buzzerState = false;
}
