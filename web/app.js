// ========================================================
// 🛡️ Kids Guardian BLE Web Companion App
// ========================================================

const SERVICE_UUID        = "19b10000-e8f2-537e-4f6c-d104768a1214";
const CHAR_INFO_UUID      = "19b10001-e8f2-537e-4f6c-d104768a1214";
const CHAR_TIME_UUID      = "19b10002-e8f2-537e-4f6c-d104768a1214";
const CHAR_COMMAND_UUID   = "19b10003-e8f2-537e-4f6c-d104768a1214";
const CHAR_BATTERY_UUID   = "19b10004-e8f2-537e-4f6c-d104768a1214";
const CHAR_RANGE_UUID     = "19b10005-e8f2-537e-4f6c-d104768a1214";

// State
let bleDevice = null;
let gattServer = null;
let infoChar = null;
let timeChar = null;
let commandChar = null;
let batteryChar = null;
let rangeChar = null;

let isConnected = false;
let currentRangeLevel = 1; // 1 = Safe, 2 = Warning (เริ่มห่าง), 3 = Far (ไกลมาก)
let audioCtx = null;
let sirenInterval = null;
let isSirenPlaying = false;
let qrCodeObj = null;

// DOM Elements
const connPill = document.getElementById('connPill');
const connDot = document.getElementById('connDot');
const connText = document.getElementById('connText');
const btnConnect = document.getElementById('btnConnect');
const btnDisconnect = document.getElementById('btnDisconnect');
const btnSyncTime = document.getElementById('btnSyncTime');
const btnRingWatch = document.getElementById('btnRingWatch');
const btnSimulateLost = document.getElementById('btnSimulateLost');
const btnSaveInfo = document.getElementById('btnSaveInfo');
const btnSilenceWebSiren = document.getElementById('btnSilenceWebSiren');
const btnClearLog = document.getElementById('btnClearLog');

const alertBanner = document.getElementById('alertBanner');
const alertTitle = document.getElementById('alertTitle');
const alertDesc = document.getElementById('alertDesc');

const warnBanner = document.getElementById('warnBanner');
const warnTitle = document.getElementById('warnTitle');
const warnDesc = document.getElementById('warnDesc');

const batText = document.getElementById('batText');
const batBar = document.getElementById('batBar');
const watchTimeText = document.getElementById('watchTimeText');
const rangeStatusText = document.getElementById('rangeStatusText');
const rssiText = document.getElementById('rssiText');
const radarAnim = document.getElementById('radarAnim');
const watchNameBadge = document.getElementById('watchNameBadge');
const logContainer = document.getElementById('logContainer');

const inputChildName = document.getElementById('inputChildName');
const inputParentName = document.getElementById('inputParentName');
const inputParentPhone = document.getElementById('inputParentPhone');

const prevChild = document.getElementById('prevChild');
const prevParent = document.getElementById('prevParent');
const prevPhone = document.getElementById('prevPhone');

// ==========================================
// 🚀 Initialization
// ==========================================
window.addEventListener('DOMContentLoaded', () => {
    loadSavedFormData();
    initQRCode();
    updateMockupPreview();

    // Check Web Bluetooth support
    if (!navigator.bluetooth) {
        log('คำเตือน: เบราว์เซอร์นี้ไม่รองรับ Web Bluetooth API แนะนำให้เปิดผ่าน Google Chrome บน Android/PC หรือแอป Bluefy บน iOS', 'warn');
    }

    // Attach Event Listeners
    btnConnect.addEventListener('click', toggleConnect);
    if (btnDisconnect) {
        btnDisconnect.addEventListener('click', handleManualDisconnect);
    }
    btnSyncTime.addEventListener('click', syncCurrentTimeToWatch);
    btnRingWatch.addEventListener('click', ringWatchBuzzer);
    btnSimulateLost.addEventListener('click', toggleSirenSimulation);
    btnSaveInfo.addEventListener('click', handleSaveInfo);
    btnSilenceWebSiren.addEventListener('click', stopAudioSiren);
    btnClearLog.addEventListener('click', () => { logContainer.innerHTML = ''; });

    [inputChildName, inputParentName, inputParentPhone].forEach(input => {
        input.addEventListener('input', () => {
            updateMockupPreview();
            saveFormData();
        });
    });
});

// ==========================================
// 📱 QR Code & Mockup Generator
// ==========================================
function initQRCode() {
    const qrElem = document.getElementById('qrcode');
    if (!qrElem) return;
    qrElem.innerHTML = '';
    const phone = inputParentPhone.value.trim() || "0812345678";
    qrCodeObj = new QRCode(qrElem, {
        text: `tel:${phone}`,
        width: 88,
        height: 88,
        colorDark: "#000000",
        colorLight: "#ffffff",
        correctLevel: QRCode.CorrectLevel.M
    });
}

// Convert Thai characters to clean English romanization for Watch LCD
function convertThaiToRoman(text) {
    if (!text) return "Kiddo";
    const hasThai = /[\u0E00-\u0E7F]/.test(text);
    if (!hasThai) return text;

    // Common names & titles dictionary
    const commonMap = {
        'น้องภูมิ': 'Poom', 'ภูมิ': 'Poom',
        'คุณแม่': 'Mom', 'แม่': 'Mom', 'มามี้': 'Mommy', 'หม่าม้า': 'Mama',
        'คุณพ่อ': 'Dad', 'พ่อ': 'Dad', 'ปะป๊า': 'Papa',
        'คุณยาย': 'Grandma', 'ยาย': 'Grandma', 'คุณย่า': 'Grandma', 'ย่า': 'Grandma',
        'คุณตา': 'Grandpa', 'ตา': 'Grandpa', 'คุณปู่': 'Grandpa', 'ปู่': 'Grandpa',
        'น้องกานต์': 'Karn', 'กานต์': 'Karn',
        'น้องนนท์': 'Nont', 'นนท์': 'Nont',
        'น้องฟ้า': 'Fah', 'ฟ้า': 'Fah',
        'น้องน้ำ': 'Nam', 'น้ำ': 'Nam',
        'น้องกาย': 'Guy', 'กาย': 'Guy',
        'น้องวิน': 'Win', 'วิน': 'Win',
        'น้องเมฆ': 'Mek', 'เมฆ': 'Mek',
        'น้องบอส': 'Boss', 'บอส': 'Boss',
        'น้องพลอย': 'Ploy', 'พลอย': 'Ploy',
        'น้องมิน': 'Min', 'มิน': 'Min',
        'น้องเบล': 'Belle', 'เบล': 'Belle',
        'น้องต้น': 'Ton', 'ต้น': 'Ton',
        'น้องปัน': 'Pun', 'ปัน': 'Pun',
        'น้องพี': 'Pee', 'พี': 'Pee',
        'น้องเจ': 'Jay', 'เจ': 'Jay',
        'น้องมาร์ค': 'Mark', 'มาร์ค': 'Mark',
        'น้องเอวา': 'Ava', 'เอวา': 'Ava',
        'น้องไบรท์': 'Bright', 'ไบรท์': 'Bright',
        'น้องข้าว': 'Khao', 'ข้าว': 'Khao',
        'น้องต้นข้าว': 'Tonkhao', 'ต้นข้าว': 'Tonkhao',
        'น้องพรีม': 'Preme', 'พรีม': 'Preme',
        'น้องคุณ': 'Khun', 'คุณ': 'Khun',
        'น้องริว': 'Ryu', 'ริว': 'Ryu',
        'น้องลูกพีช': 'Peach', 'ลูกพีช': 'Peach',
        'น้องชิน': 'Chin', 'ชิน': 'Chin'
    };

    let trimmed = text.trim();
    if (commonMap[trimmed]) return commonMap[trimmed];

    // Strip "น้อง" or "คุณ" prefixes if present
    let cleaned = trimmed.replace(/^(น้อง|คุณ)/, '');
    if (commonMap[cleaned]) return commonMap[cleaned];

    // Consonant & Vowel transliteration fallback
    const thaiConsonants = {
        'ก': 'K', 'ข': 'Kh', 'ฃ': 'Kh', 'ค': 'Kh', 'ฅ': 'Kh', 'ฆ': 'Kh',
        'ง': 'Ng', 'จ': 'Ch', 'ฉ': 'Ch', 'ช': 'Ch', 'ซ': 'S', 'ฌ': 'Ch',
        'ญ': 'Y', 'ด': 'D', 'ต': 'T', 'ถ': 'Th', 'ท': 'Th', 'ธ': 'Th',
        'น': 'N', 'บ': 'B', 'ป': 'P', 'ผ': 'Ph', 'ฝ': 'F', 'พ': 'Ph',
        'ฟ': 'F', 'ภ': 'Ph', 'ม': 'M', 'ย': 'Y', 'ร': 'R', 'ฤ': 'Rue',
        'ล': 'L', 'ว': 'W', 'ศ': 'S', 'ษ': 'S', 'ส': 'S', 'ห': 'H',
        'ฬ': 'L', 'อ': 'O', 'ฮ': 'H'
    };

    const thaiVowels = {
        'ะ': 'a', 'า': 'a', 'ำ': 'am', 'ิ': 'i', 'ี': 'ee', 'ึ': 'ue', 'ื': 'ue',
        'ุ': 'u', 'ู': 'oo', 'เ': 'e', 'แ': 'ae', 'โ': 'o', 'ใ': 'ai', 'ไ': 'ai'
    };

    let result = '';
    for (let char of cleaned) {
        if (thaiConsonants[char]) {
            result += thaiConsonants[char];
        } else if (thaiVowels[char]) {
            result += thaiVowels[char];
        } else if (/^[a-zA-Z0-9\s_-]$/.test(char)) {
            result += char;
        }
    }
    result = result.trim();
    if (result.length > 0) {
        return result.charAt(0).toUpperCase() + result.slice(1).toLowerCase();
    }
    return 'Kiddo';
}

function updateMockupPreview() {
    const cName = inputChildName.value.trim() || "Poom";
    const pName = inputParentName.value.trim() || "Mom";
    const pPhone = inputParentPhone.value.trim() || "0812345678";

    const cRoman = convertThaiToRoman(cName);
    const pRoman = convertThaiToRoman(pName);

    prevChild.textContent = cRoman;
    prevParent.textContent = pRoman;
    prevPhone.textContent = pPhone;
    watchNameBadge.textContent = `KidsWatch-${cRoman}`;

    const childHint = document.getElementById('childHint');
    const parentHint = document.getElementById('parentHint');
    if (childHint) childHint.textContent = `แสดงบนนาฬิกา: ${cRoman}`;
    if (parentHint) parentHint.textContent = `แสดงบนนาฬิกา: ${pRoman}`;

    if (qrCodeObj) {
        qrCodeObj.clear();
        qrCodeObj.makeCode(`tel:${pPhone}`);
    }
}

function saveFormData() {
    localStorage.setItem('guardian_child', inputChildName.value);
    localStorage.setItem('guardian_parent', inputParentName.value);
    localStorage.setItem('guardian_phone', inputParentPhone.value);
}

function loadSavedFormData() {
    const savedChild = localStorage.getItem('guardian_child');
    const savedParent = localStorage.getItem('guardian_parent');
    const savedPhone = localStorage.getItem('guardian_phone');

    inputChildName.value = savedChild || "Poom";
    inputParentName.value = savedParent || "Mom";
    inputParentPhone.value = savedPhone || "0812345678";
}

const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

async function safeWrite(characteristic, data) {
    if (!characteristic) return;
    if (typeof characteristic.writeValueWithResponse === 'function') {
        await characteristic.writeValueWithResponse(data);
    } else {
        await characteristic.writeValue(data);
    }
}

// ==========================================
// 📡 Web Bluetooth Handler
// ==========================================
async function toggleConnect() {
    if (isConnected) {
        disconnectBLE();
        return;
    }

    if (!navigator.bluetooth) {
        alert("⚠️ เบราว์เซอร์ปัจจุบันไม่รองรับ Web Bluetooth API!\n\n" +
              "• หากใช้ iPhone หรือ iPad: บราวเซอร์มาตรฐาน (Safari, Chrome บน iOS) ไม่รองรับบลูทูธบนเว็บ โปรดดาวน์โหลดแอป 'Bluefy - Web BLE Browser' จาก App Store แล้วเปิดเว็บนี้\n\n" +
              "• หากใช้ Mac หรือ Windows: ต้องเปิดผ่าน 'Google Chrome' หรือ 'Microsoft Edge' เท่านั้น (Safari / Firefox ไม่รองรับ)\n\n" +
              "• หากใช้ Android: โปรดเปิดใช้งานทั้ง Bluetooth และ ตำแหน่ง (Location/GPS)");
        log('เบราว์เซอร์ไม่รองรับ Web Bluetooth API (โปรดใช้ Chrome/Edge หรือ Bluefy บน iOS)', 'error');
        return;
    }

    try {
        const chkAllDevices = document.getElementById('chkAllDevices');
        let requestOptions;

        if (chkAllDevices && chkAllDevices.checked) {
            log('🔍 กำลังค้นหาอุปกรณ์บลูทูธทั้งหมดรอบตัว (โหมด All Devices)...', 'info');
            requestOptions = {
                acceptAllDevices: true,
                optionalServices: [SERVICE_UUID]
            };
        } else {
            log('🔍 กำลังสแกนหา KidsWatch (หรือติ๊ก "ค้นหาอุปกรณ์ทั้งหมด" หากหาไม่เจอ)...', 'info');
            requestOptions = {
                filters: [
                    { namePrefix: 'KidsWatch' },
                    { namePrefix: 'M5' },
                    { services: [SERVICE_UUID] }
                ],
                optionalServices: [SERVICE_UUID]
            };
        }

        bleDevice = await navigator.bluetooth.requestDevice(requestOptions);

        bleDevice.addEventListener('gattserverdisconnected', onDisconnected);
        log(`พบอุปกรณ์: ${bleDevice.name || 'อุปกรณ์ไม่ระบุชื่อ'} กำลังเชื่อมต่อ...`, 'info');

        gattServer = await bleDevice.gatt.connect();
        log('เชื่อมต่อสำเร็จ! กำลังค้นหา Guardian GATT Services...', 'success');
        await sleep(150); // ให้เวลา BLE controller จัดการ Connection Parameters

        const service = await gattServer.getPrimaryService(SERVICE_UUID);
        await sleep(60);

        // Get Characteristics
        infoChar = await service.getCharacteristic(CHAR_INFO_UUID);
        timeChar = await service.getCharacteristic(CHAR_TIME_UUID);
        commandChar = await service.getCharacteristic(CHAR_COMMAND_UUID);
        batteryChar = await service.getCharacteristic(CHAR_BATTERY_UUID);
        await sleep(60);

        // Listen for Battery changes
        await batteryChar.startNotifications();
        batteryChar.addEventListener('characteristicvaluechanged', onBatteryChanged);
        await sleep(60);

        // Listen for SOS commands from Watch
        await commandChar.startNotifications();
        commandChar.addEventListener('characteristicvaluechanged', onCommandReceived);
        await sleep(60);

        // Get and listen for Proximity / RSSI Range updates
        try {
            rangeChar = await service.getCharacteristic(CHAR_RANGE_UUID);
            await sleep(60);
            await rangeChar.startNotifications();
            rangeChar.addEventListener('characteristicvaluechanged', onRangeChanged);
        } catch (e) {
            console.warn('Range characteristic not available', e);
        }
        await sleep(60);

        // Read initial battery
        try {
            const batVal = await batteryChar.readValue();
            updateBatteryDisplay(batVal.getUint8(0));
        } catch (e) {
            console.warn('Initial bat read error', e);
        }
        await sleep(60);

        // Set state to Connected
        setConnectedState(true);
        log('พร้อมทำงาน! เชื่อมต่อและป้องกันเด็กอยู่ในระยะแล้ว', 'success');

        // Automatically sync current time to watch
        await syncCurrentTimeToWatch();
        await sleep(80);

        // Automatically sync registered parent info to watch
        await sendInfoToWatch();

    } catch (error) {
        if (error.name === 'NotFoundError') {
            log('ยกเลิกการเลือก หรือไม่พบอุปกรณ์ในระยะ (ลองติ๊ก "ค้นหาอุปกรณ์ทั้งหมดรอบตัว" แล้วกดใหม่อีกครั้ง)', 'warn');
        } else {
            log(`เชื่อมต่อไม่สำเร็จ: ${error.message || error}`, 'error');
        }
        setConnectedState(false);
    }
}

let isManualDisconnect = false;

async function handleManualDisconnect() {
    log('กำลังตัดการเชื่อมต่อกับนาฬิกาอย่างปลอดภัย (Safe Disconnect)...', 'info');
    isManualDisconnect = true;
    try {
        if (commandChar && isConnected) {
            const disarmCmd = new Uint8Array([0x04]); // 0x04 = Disarm & Safe Disconnect
            await safeWrite(commandChar, disarmCmd);
            await sleep(150);
        }
    } catch (e) {
        console.warn('Disarm command error', e);
    }

    disconnectBLE();
    log('ตัดการเชื่อมต่อเรียบร้อยแล้ว (ไม่ส่งเสียงไซเรน)', 'success');
}

function onDisconnected() {
    const wasConnected = isConnected;
    const manual = isManualDisconnect;
    isManualDisconnect = false;

    if (manual) {
        log('ตัดการเชื่อมต่อเรียบร้อยแล้ว (Safe Disconnect)', 'info');
        setConnectedState(false, false);
    } else {
        log('🚨 ขาดการเชื่อมต่อกับนาฬิกา! (Out of Range / หลุดระยะ)', 'error');
        setConnectedState(false, wasConnected); // Only trigger emergency siren if it was genuinely connected
    }
}

function disconnectBLE() {
    if (bleDevice && bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
    }
    setConnectedState(false);
}

// ==========================================
// 📡 Proximity & RSSI Range Handler
// ==========================================
function onRangeChanged(event) {
    const value = event.target.value;
    if (value.byteLength >= 2) {
        const level = value.getUint8(0);
        const rssiAbs = value.getUint8(1);
        updateProximityStatus(level, -rssiAbs);
    }
}

function updateProximityStatus(level, rssi) {
    if (rssiText) {
        let quality = 'ใกล้ตัว';
        if (rssi <= -85) quality = 'ไกลมาก';
        else if (rssi <= -72) quality = 'เริ่มห่าง';
        rssiText.textContent = `${rssi} dBm (${quality})`;
        if (level === 1) rssiText.style.color = 'var(--accent-emerald)';
        else if (level === 2) rssiText.style.color = 'var(--accent-amber)';
        else rssiText.style.color = '#f97316';
    }

    if (!isConnected) return;

    if (level === 1) { // 🟢 ปลอดภัย (Safe)
        rangeStatusText.textContent = 'ปลอดภัย (~0-5m)';
        rangeStatusText.className = 'stat-value safe-tag';
        if (radarAnim) radarAnim.className = 'radar-circle';
        if (warnBanner) warnBanner.style.display = 'none';
    } else if (level === 2) { // 🟡 เตือน: เด็กเริ่มออกห่าง (~5-10m)
        rangeStatusText.textContent = 'เริ่มออกห่าง (~5-10m)';
        rangeStatusText.className = 'stat-value warn-tag';
        if (radarAnim) radarAnim.className = 'radar-circle warning';
        if (warnBanner) {
            warnTitle.textContent = '⚠️ เด็กเริ่มเดินออกห่างแล้ว (~5–10 เมตร)';
            warnDesc.textContent = `สัญญาณบลูทูธเริ่มอ่อนลง (${rssi} dBm) กรุณาสอดส่องมองหาน้อง`;
            warnBanner.style.display = 'block';
        }
        if (currentRangeLevel !== 2) {
            playSoftWarningChime();
            log(`⚠️ สัญญาณเตือน: เด็กเริ่มเดินออกห่างแล้ว (${rssi} dBm)`, 'warn');
        }
    } else if (level === 3) { // 🟠 ไกลมาก เสี่ยงหลุดระยะ (~10-15m)
        rangeStatusText.textContent = 'ไกลมาก! เสี่ยงหลุด (~10-15m)';
        rangeStatusText.className = 'stat-value danger-tag';
        if (radarAnim) radarAnim.className = 'radar-circle warning';
        if (warnBanner) {
            warnTitle.textContent = '🚨 เด็กอยู่ห่างมาก (~10–15 เมตร) ใกล้หลุดระยะ!';
            warnDesc.textContent = `สัญญาณอ่อนมาก (${rssi} dBm) เสี่ยงหลุดการเชื่อมต่อ กรุณาเดินเข้าไปใกล้น้อง`;
            warnBanner.style.display = 'block';
        }
        if (currentRangeLevel !== 3) {
            playSoftWarningChime();
            log(`🚨 สัญญาณวิกฤต: เด็กอยู่ห่างมาก เสี่ยงหลุดระยะ (${rssi} dBm)`, 'warn');
        }
    }

    currentRangeLevel = level;
}

function playSoftWarningChime() {
    try {
        initAudio();
        if (audioCtx.state === 'suspended') audioCtx.resume();
        const now = audioCtx.currentTime;
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'sine';
        osc.frequency.setValueAtTime(880, now);
        osc.frequency.setValueAtTime(660, now + 0.12);
        gain.gain.setValueAtTime(0.2, now);
        gain.gain.exponentialRampToValueAtTime(0.01, now + 0.35);
        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start(now);
        osc.stop(now + 0.35);

        if (navigator.vibrate) {
            try { navigator.vibrate([150, 80, 150]); } catch (e) {}
        }
    } catch (e) {}
}

// ==========================================
// 🔔 Out-of-Range Alarm & SOS Handlers
// ==========================================
function onCommandReceived(event) {
    const value = event.target.value;
    if (value.byteLength > 0) {
        const cmd = value.getUint8(0);
        if (cmd === 0xFF) { // SOS Flag from Watch
            triggerEmergencyAlarm("🚨 เด็กกดปุ่มขอความช่วยเหลือฉุกเฉิน (SOS)!", "เด็กกดปุ่ม M5 บนนาฬิกาค้างไว้เพื่อส่งสัญญาณเตือนคุณแม่/คุณพ่อ");
        }
    }
}

function triggerEmergencyAlarm(title, desc) {
    alertTitle.textContent = title;
    alertDesc.textContent = desc;
    alertBanner.style.display = 'block';

    connPill.className = 'connection-status-pill alarm';
    connText.textContent = 'EMERGENCY ALERT';
    rangeStatusText.textContent = 'หลุดระยะ!';
    rangeStatusText.className = 'stat-value';
    rangeStatusText.style.color = '#ef4444';

    startAudioSiren();
}

function dismissEmergencyAlarm() {
    alertBanner.style.display = 'none';
    stopAudioSiren();
}

// ==========================================
// 🔊 Web Audio Siren Synthesizer
// ==========================================
function initAudio() {
    if (!audioCtx) {
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }
}

function startAudioSiren() {
    if (isSirenPlaying) return;
    initAudio();
    if (audioCtx.state === 'suspended') {
        audioCtx.resume();
    }
    isSirenPlaying = true;

    if (navigator.vibrate) {
        try {
            navigator.vibrate([500, 200, 500, 200, 500]);
        } catch (e) {}
    }

    let toneHigh = false;
    sirenInterval = setInterval(() => {
        if (!isSirenPlaying) return;
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'sawtooth';
        osc.frequency.setValueAtTime(toneHigh ? 960 : 700, audioCtx.currentTime);
        gain.gain.setValueAtTime(0.3, audioCtx.currentTime);
        gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.28);

        osc.connect(gain);
        gain.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + 0.3);

        toneHigh = !toneHigh;
    }, 300);
}

function stopAudioSiren() {
    if (sirenInterval) {
        clearInterval(sirenInterval);
        sirenInterval = null;
    }
    isSirenPlaying = false;
    alertBanner.style.display = 'none';
    if (navigator.vibrate) {
        try {
            navigator.vibrate(0);
        } catch (e) {}
    }
}

function toggleSirenSimulation() {
    if (isSirenPlaying) {
        stopAudioSiren();
        log('ปิดการจำลองเสียงไซเรน', 'info');
    } else {
        triggerEmergencyAlarm("🚨 ทดสอบระบบไซเรนแจ้งเตือน", "จำลองสถานการณ์เมื่อเด็กออกนอกระยะหรือกด SOS");
        log('เปิดเสียงไซเรนจำลอง (กรุณากด ปิดเสียงไซเรน เพื่อหยุด)', 'warn');
    }
}

// ==========================================
// ⌚ Watch BLE Actions (Sync Time, Info, Beep)
// ==========================================
async function syncCurrentTimeToWatch() {
    if (!timeChar) {
        log('ไม่สามารถซิงค์เวลาได้: ยังไม่ได้เชื่อมต่อ BLE', 'warn');
        return;
    }
    try {
        const now = new Date();
        const timeStr = `${now.getFullYear()},${now.getMonth() + 1},${now.getDate()},${now.getHours()},${now.getMinutes()},${now.getSeconds()}`;
        const encoder = new TextEncoder();
        await safeWrite(timeChar, encoder.encode(timeStr));

        const timeDisplay = now.toLocaleTimeString('th-TH');
        watchTimeText.textContent = timeDisplay;
        log(`ซิงค์เวลากับนาฬิกาสำเร็จ: ${timeDisplay}`, 'success');
    } catch (e) {
        log(`เกิดข้อผิดพลาดในการซิงค์เวลา: ${e.message}`, 'error');
    }
}

async function handleSaveInfo() {
    saveFormData();
    updateMockupPreview();
    if (isConnected && infoChar) {
        await sendInfoToWatch();
    } else {
        log('บันทึกข้อมูลในเว็บแล้ว (จะถูกส่งเข้าสู่นาฬิกาอัตโนมัติเมื่อกดเชื่อมต่อ)', 'info');
    }
}

async function sendInfoToWatch() {
    if (!infoChar) return;
    try {
        const cName = inputChildName.value.trim() || "Poom";
        const pName = inputParentName.value.trim() || "Mom";
        const pPhone = inputParentPhone.value.trim() || "0812345678";

        // Convert Thai to clean English Romanization for watch LCD
        const cNameWatch = convertThaiToRoman(cName);
        const pNameWatch = convertThaiToRoman(pName);

        const payload = `${pNameWatch}|${pPhone}|${cNameWatch}`;

        const encoder = new TextEncoder();
        await safeWrite(infoChar, encoder.encode(payload));
        log(`บันทึกข้อมูลเข้าสู่นาฬิกาสำเร็จ: ${cNameWatch} | ${pNameWatch} (${pPhone})`, 'success');
    } catch (e) {
        log(`เกิดข้อผิดพลาดในการส่งข้อมูล: ${e.message}`, 'error');
    }
}

async function ringWatchBuzzer() {
    if (!commandChar) return;
    try {
        const cmd = new Uint8Array([0x01]); // 0x01 = Ring buzzer
        await safeWrite(commandChar, cmd);
        log('ส่งคำสั่งให้นาฬิกาส่งเสียง Beep ค้นหาสำเร็จ 🔔', 'success');
    } catch (e) {
        log(`ส่งคำสั่งไม่สำเร็จ: ${e.message}`, 'error');
    }
}

function onBatteryChanged(event) {
    const pct = event.target.value.getUint8(0);
    updateBatteryDisplay(pct);
}

function updateBatteryDisplay(pct) {
    batText.textContent = `${pct}%`;
    batBar.style.width = `${pct}%`;
    if (pct <= 20) {
        batBar.style.background = 'var(--accent-crimson)';
    } else if (pct <= 50) {
        batBar.style.background = 'var(--accent-amber)';
    } else {
        batBar.style.background = 'linear-gradient(90deg, var(--accent-emerald), var(--accent-cyan))';
    }
}

// ==========================================
// 🎨 UI State Manager
// ==========================================
function setConnectedState(connected, isAlarmTriggered = false) {
    isConnected = connected;

    if (connected) {
        connPill.className = 'connection-status-pill connected';
        connText.textContent = 'เชื่อมต่อแล้ว (Connected)';
        
        btnConnect.style.display = 'none';
        if (btnDisconnect) btnDisconnect.style.display = 'inline-flex';

        btnSyncTime.disabled = false;
        btnRingWatch.disabled = false;

        rangeStatusText.textContent = 'ปลอดภัย (~0-5m)';
        rangeStatusText.className = 'stat-value safe-tag';
        rangeStatusText.style.color = 'var(--accent-emerald)';

        if (warnBanner) warnBanner.style.display = 'none';
        if (radarAnim) radarAnim.className = 'radar-circle';
        currentRangeLevel = 1;

        dismissEmergencyAlarm();
    } else {
        connPill.className = 'connection-status-pill';
        connText.textContent = 'ยังไม่เชื่อมต่อ (Disconnected)';

        btnConnect.style.display = 'inline-flex';
        if (btnDisconnect) btnDisconnect.style.display = 'none';

        btnSyncTime.disabled = true;
        btnRingWatch.disabled = true;

        if (warnBanner) warnBanner.style.display = 'none';
        if (rssiText) {
            rssiText.textContent = '-- dBm';
            rssiText.style.color = 'var(--text-muted)';
        }
        if (radarAnim) radarAnim.className = 'radar-circle';
        currentRangeLevel = 0;

        if (isAlarmTriggered) {
            triggerEmergencyAlarm("⚠️ ออกนอกระยะเชื่อมต่อ! (Out of Range)", "นาฬิกาส่งเสียงร้องเตือนและแสดงเบอร์โทรฉุกเฉินบนหน้าจอแล้ว");
        } else {
            rangeStatusText.textContent = 'ไม่ได้เชื่อมต่อ';
            rangeStatusText.className = 'stat-value';
            rangeStatusText.style.color = 'var(--text-muted)';
        }
    }
}

// ==========================================
// 📜 Logger
// ==========================================
function log(msg, type = 'system') {
    const now = new Date();
    const timeStr = now.toTimeString().split(' ')[0];
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.innerHTML = `<span class="log-time">${timeStr}</span><span class="log-msg">${msg}</span>`;
    logContainer.prepend(entry);
}
