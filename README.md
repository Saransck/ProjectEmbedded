# 🛡️ Kids Guardian Smartwatch (นาฬิกาป้องกันเด็กพลัดหลง)

ระบบนาฬิกาอัจฉริยะสำหรับเด็กและผู้ปกครอง พัฒนาด้วยบอร์ด **M5StickC PLUS 1.1 (ESP32)** เชื่อมต่อผ่าน **Bluetooth Low Energy (BLE)** เข้ากับ **Web Companion App** ทำงานร่วมกับสมาร์ตโฟน (Android / iOS)

---

## ✨ ฟังก์ชันหลัก (Key Features)

1. **🚨 Out-of-Range Anti-Lost Alarm (ระบบตรวจจับออกนอกระยะ):**
   - เมื่อเด็กเดินห่างออกไปจนสัญญาณ BLE ขาดการเชื่อมต่อ
   - **ฝั่งนาฬิกา:** Buzzer จะส่งเสียงร้องเตือนความถี่สูงทันที และหน้าจอเปลี่ยนเป็นสีแดงเตือนภัย แสดง **ชื่อเด็ก, ชื่อผู้ปกครอง, เบอร์โทรศัพท์ฉุกเฉิน**
   - **ฝั่งมือถือผู้ปกครอง:** ส่งเสียงไซเรนฉุกเฉิน (Web Audio API) แจ้งเตือนผู้ปกครองทันทีแบบสองทาง
2. **🆘 SOS Emergency Button (ปุ่มขอความช่วยเหลือฉุกเฉิน):**
   - เด็กสามารถกดปุ่ม **M5 (Button A)** ด้านหน้าค้างไว้ 2 วินาที เพื่อส่งสัญญาณ SOS ไปสั่น/แจ้งเตือนมือถือผู้ปกครอง
3. **💾 NVS Flash Memory Persistence:**
   - บันทึกเบอร์โทรและชื่อผู้ปกครองลง Flash Memory ของชิป ESP32 โดยตรง แม้ปิดเครื่องหรือแบตเตอรี่หมด ข้อมูลก็ไม่สูญหาย
4. **⏰ Real-time Clock Sync:**
   - ซิงค์เวลาบนชิป RTC (BM8563) ให้ตรงกับเวลามือถืออัตโนมัติเมื่อกดเชื่อมต่อ
5. **🔋 Battery & Power Management:**
   - อ่านระดับแบตเตอรี่ผ่าน AXP192 และรายงาน % ไปยังแอปมือถือ
   - ปุ่มกดด้านข้าง (Button B) ใช้สำหรับ Snooze (พักเสียงเตือนชั่วคราวขณะหลงทาง) หรือปรับหรี่แสงหน้าจอเพื่อประหยัดแบตเตอรี่

---

## 📁 โครงสร้างโปรเจกต์ (Project Structure)

```
ProjectEmbedded/
├── firmware/
│   ├── KidsWatch_M5StickCPlus.ino   # โค้ด Arduino สำหรับ M5StickC PLUS 1.1
│   └── platformio.ini               # ตั้งค่าสำหรับ PlatformIO (VS Code)
├── web/
│   ├── index.html                   # หน้าเว็บ Companion App
│   ├── style.css                    # Modern Glassmorphism CSS Design
│   └── app.js                       # Logic Web Bluetooth API + Web Audio Siren
└── README.md
```

---

## 🛠️ การติดตั้งและอัปโหลด Firmware (M5StickC PLUS)

### ผ่าน Arduino IDE:
1. ติดตั้งบอร์ด **ESP32** ใน Boards Manager
2. ติดตั้ง Library: `M5StickCPlus` (by M5Stack)
3. เลือกบอร์ด: **M5Stick-C-Plus** หรือ **ESP32 Dev Module**
4. เชื่อมต่อสาย USB-C และกด **Upload**

### ผ่าน PlatformIO (VS Code):
```bash
cd firmware
pio run -t upload
```

---

## 📱 การใช้งาน Web Companion App

### การเปิดใช้งาน:
- **บนคอมพิวเตอร์ / Android:** เปิดผ่าน **Google Chrome** หรือ **Microsoft Edge**
- **บน iPhone / iPad:** ติดตั้งแอปฟรี **[Bluefy – Web BLE Browser](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)** บน App Store เพื่อเปิดเว็บ
- *หมายเหตุ: Web Bluetooth ต้องการการเชื่อมต่อแบบ HTTPS://*

### ขั้นตอนการใช้งาน:
1. เปิดเครื่อง M5StickC PLUS
2. เปิดเว็บแอปและกดปุ่ม **"เชื่อมต่อ BLE กับนาฬิกา"**
3. เลือกอุปกรณ์ชื่อ `KidsWatch-...`
4. กรอกชื่อเด็ก, ชื่อผู้ปกครอง และเบอร์โทรศัพท์ แล้วกด **"บันทึกและส่งเข้าสู่นาฬิกา"**
5. ระบบจะเริ่มทำงานปกป้องเด็กทันที!

---

## 📡 BLE Protocol Specification

- **Service UUID:** `19b10000-e8f2-537e-4f6c-d104768a1214`
  - **Info Characteristic (`19b10001-...`):** `READ` / `WRITE` / `NOTIFY` — ข้อมูลผู้ปกครอง (`ParentName|Phone|ChildName`)
  - **Time Characteristic (`19b10002-...`):** `WRITE` — ซิงค์เวลา (`YYYY,MM,DD,hh,mm,ss`)
  - **Command Characteristic (`19b10003-...`):** `READ` / `WRITE` / `NOTIFY` — `0x01`: ค้นหานาฬิกา, `0xFF`: SOS จากเด็ก
  - **Battery Characteristic (`19b10004-...`):** `READ` / `NOTIFY` — ระดับแบตเตอรี่ (0–100%)