# 🛡️ Kids Guardian Smartwatch (นาฬิกาป้องกันเด็กพลัดหลง)

ระบบนาฬิกาอัจฉริยะสำหรับเด็กและผู้ปกครอง พัฒนาด้วยบอร์ด **M5StickC PLUS 1.1 (ESP32)** เชื่อมต่อผ่าน **Bluetooth Low Energy (BLE)** เข้ากับ **Web Companion App** บนสมาร์ตโฟน (Android / iOS)

---

## ✨ ฟังก์ชันหลัก (Key Features)

1. **🚨 Out-of-Range Anti-Lost Alarm (ระบบตรวจจับออกนอกระยะ):**
   - เมื่อเด็กเดินห่างออกจากผู้ปกครองจนสัญญาณ BLE ขาดหายไป:
     - **ฝั่งนาฬิกาเด็ก (M5StickC PLUS):** ส่งเสียงไซเรนเตือนภัยความถี่สูงสลับโทนทันที พร้อมหน้าจอสีแดงกระพริบ แสดง **ชื่อเด็ก, ชื่อผู้ปกครอง, เบอร์โทรศัพท์ฉุกเฉินขนาดใหญ่** และ **QR Code บนหน้าปัดนาฬิกา** ให้ผู้พบเห็นสามารถยกกล้องมือถือสแกนแล้วกดโทรหาผู้ปกครองได้ทันที
     - **ฝั่งมือถือผู้ปกครอง:** ส่งเสียงไซเรนฉุกเฉิน (Web Audio API) พร้อมระบบสั่น (Haptic Vibration) แจ้งเตือนผู้ปกครองทันที
2. **🆘 SOS Emergency Button (ปุ่มขอความช่วยเหลือฉุกเฉิน):**
   - เด็กสามารถกดปุ่ม **M5 (Button A)** ด้านหน้าค้างไว้ 2 วินาที เมื่อตกใจหรือต้องการความช่วยเหลือ นาฬิกาจะส่งสัญญาณ SOS ไปสั่นและดังเตือนที่มือถือผู้ปกครองทันที
3. **🤫 Snooze Function (พักเสียงชั่วคราว):**
   - ขณะเสียงเตือนดัง สามารถกดปุ่มด้านข้าง **(Button B)** เพื่อพักเสียงไซเรน 45 วินาที เพื่อไม่ให้เด็กรู้สึกตระหนกและสะดวกต่อการพูดคุยโทรศัพท์ โดยหน้าจอข้อมูลฉุกเฉินยังคงแสดงอยู่ตลอดเวลา
4. **💾 NVS Flash Memory Persistence (บันทึกข้อมูลถาวร):**
   - บันทึกเบอร์โทรศัพท์และชื่อผู้ปกครองลงหน่วยความจำ Flash (NVS Preferences) ของชิป ESP32 โดยตรง ข้อมูลไม่สูญหายแม้ปิดเครื่องหรือแบตเตอรี่หมด
5. **⏰ Real-time Clock Sync (ซิงค์เวลาอัตโนมัติ):**
   - ซิงค์เวลาบนชิป RTC (BM8563) ให้ตรงกับเวลามือถืออัตโนมัติเมื่อกดเชื่อมต่อ BLE
6. **🔋 Battery & Power Management:**
   - อ่านระดับแบตเตอรี่ผ่านชิป AXP192 และรายงาน % แบบเรียลไทม์ไปยังแอปมือถือ
   - กดปุ่มด้านข้าง (Button B) ในโหมดปกติเพื่อปรับระดับความสว่างหน้าจอ ประหยัดพลังงาน
7. **🔔 Find My Watch (ค้นหานาฬิกา):**
   - ผู้ปกครองสามารถกดปุ่มในแอปมือถือ เพื่อสั่งให้นาฬิกาส่งเสียง Beep ช่วยค้นหาตำแหน่งได้

---

## 📁 โครงสร้างโปรเจกต์ (Project Structure)

```
ProjectEmbedded/
├── platformio.ini         # ตั้งค่า PlatformIO (บอร์ด M5Stick-C, Partition min_spiffs, Library M5StickCPlus)
├── src/
│   └── main.cpp           # เฟิร์มแวร์ C++ หลักสำหรับ M5StickC PLUS 1.1
├── web/
│   ├── index.html         # หน้าเว็บ Guardian Companion App (Mobile-first Glassmorphism UI)
│   ├── style.css          # สไตล์ตกแต่ง ปรับแสงเงา Radar Animation และ Dark Mode
│   └── app.js             # ควบคุม Web Bluetooth API, Web Audio Siren, Vibration, QR Code
├── index.html             # ทางเข้าหลัก Redirect เข้าสู่ web/ (รองรับ GitHub Pages ทันที)
└── README.md
```

---

## 🛠️ การคอมไพล์และอัปโหลด Firmware (PlatformIO)

### ผ่าน PlatformIO บน VS Code:
1. เปิดโฟลเดอร์โปรเจกต์ใน VS Code ที่มีส่วนขยาย **PlatformIO IDE**
2. เสียบสาย USB-C บอร์ด M5StickC PLUS 1.1 เข้ากับคอมพิวเตอร์
3. กดปุ่ม **Build** (เครื่องหมายถูก) หรือ **Upload** (ลูกศรขวา) ที่แถบด้านล่างของ VS Code

### ผ่าน Command Line (Terminal):
```bash
# คอมไพล์โปรเจกต์
pio run

# อัปโหลดลงบอร์ด M5StickC PLUS
pio run -t upload

# เปิด Serial Monitor ดู Log (115200 baud)
pio run -t monitor
```

---

## 📱 วิธีใช้งาน Web Companion App บนมือถือ

### อุปกรณ์ที่รองรับ:
- **Android / PC / Mac:** เปิดผ่าน **Google Chrome** หรือ **Microsoft Edge**
- **iPhone / iPad:** ติดตั้งแอปฟรี **[Bluefy – Web BLE Browser](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)** บน App Store แล้วเปิด URL ของเว็บ
- *(หมายเหตุ: Web Bluetooth ต้องการการเชื่อมต่อแบบ HTTPS หรือรันบน localhost)*

### ขั้นตอนการเริ่มใช้งาน:
1. เปิดเครื่อง M5StickC PLUS (หน้าจอจะแสดงเวลานาฬิกา และสถานะ `[WAITING BLE]`)
2. เปิดเว็บแอป Companion บนมือถือ
3. กรอก **ชื่อเด็ก, ชื่อผู้ปกครอง และเบอร์โทรศัพท์ฉุกเฉิน**
4. กดปุ่ม **"เชื่อมต่อ BLE กับนาฬิกา"** แล้วเลือกอุปกรณ์ `KidsWatch-...`
5. เมื่อเชื่อมต่อแล้ว ข้อมูลผู้ปกครองจะถูกบันทึกลง Flash และเวลาจะซิงค์กับมือถือทันที
6. หากเด็กเดินออกห่างเกินระยะสัญญาณ ทั้งนาฬิกาและมือถือจะส่งเสียงเตือนภัยพร้อมกันทันที!

---

## 📡 BLE Protocol Specification

- **Service UUID:** `19b10000-e8f2-537e-4f6c-d104768a1214`
  - **Info Characteristic (`19b10001-...`):** `READ` / `WRITE` / `NOTIFY` — ข้อมูลผู้ปกครอง (`ParentName|Phone|ChildName`)
  - **Time Characteristic (`19b10002-...`):** `WRITE` — ซิงค์เวลา (`YYYY,MM,DD,hh,mm,ss`)
  - **Command Characteristic (`19b10003-...`):** `READ` / `WRITE` / `NOTIFY` — `0x01`: Find Watch, `0x02`: Mute, `0xFF`: SOS Alert
  - **Battery Characteristic (`19b10004-...`):** `READ` / `NOTIFY` — ระดับแบตเตอรี่ (0–100%)