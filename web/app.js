// ========================================================
// 🛡️ Kids Guardian BLE Web Companion App
// ========================================================

const SERVICE_UUID        = "19b10000-e8f2-537e-4f6c-d104768a1214";
const CHAR_INFO_UUID      = "19b10001-e8f2-537e-4f6c-d104768a1214";
const CHAR_TIME_UUID      = "19b10002-e8f2-537e-4f6c-d104768a1214";
const CHAR_COMMAND_UUID   = "19b10003-e8f2-537e-4f6c-d104768a1214";
const CHAR_BATTERY_UUID   = "19b10004-e8f2-537e-4f6c-d104768a1214";

// State
let bleDevice = null;
let gattServer = null;
let infoChar = null;
let timeChar = null;
let commandChar = null;
let batteryChar = null;

let isConnected = false;
let audioCtx = null;
let sirenInterval = null;
let isSirenPlaying = false;
let qrCodeObj = null;

// DOM Elements
const connPill = document.getElementById('connPill');
const connDot = document.getElementById('connDot');
const connText = document.getElementById('connText');
const btnConnect = document.getElementById('btnConnect');
const btnSyncTime = document.getElementById('btnSyncTime');
const btnRingWatch = document.getElementById('btnRingWatch');
const btnSimulateLost = document.getElementById('btnSimulateLost');
const btnSaveInfo = document.getElementById('btnSaveInfo');
const btnSilenceWebSiren = document.getElementById('btnSilenceWebSiren');
const btnClearLog = document.getElementById('btnClearLog');

const alertBanner = document.getElementById('alertBanner');
const alertTitle = document.getElementById('alertTitle');
const alertDesc = document.getElementById('alertDesc');

const batText = document.getElementById('batText');
const batBar = document.getElementById('batBar');
const watchTimeText = document.getElementById('watchTimeText');
const rangeStatusText = document.getElementById('rangeStatusText');
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

function updateMockupPreview() {
    const cName = inputChildName.value.trim() || "น้องภูมิ";
    const pName = inputParentName.value.trim() || "คุณแม่";
    const pPhone = inputParentPhone.value.trim() || "0812345678";

    prevChild.textContent = cName;
    prevParent.textContent = pName;
    prevPhone.textContent = pPhone;
    watchNameBadge.textContent = `KidsWatch-${cName}`;

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

    if (savedChild) inputChildName.value = savedChild;
    if (savedParent) inputParentName.value = savedParent;
    if (savedPhone) inputParentPhone.value = savedPhone;
}

// ==========================================
// 📡 Web Bluetooth Handler
// ==========================================
async function toggleConnect() {
    if (isConnected) {
        disconnectBLE();
        return;
    }

    try {
        log('กำลังเริ่มสแกนหานาฬิกา M5StickC PLUS...', 'info');

        bleDevice = await navigator.bluetooth.requestDevice({
            filters: [
                { namePrefix: 'KidsWatch' }
            ],
            optionalServices: [SERVICE_UUID]
        });

        bleDevice.addEventListener('gattserverdisconnected', onDisconnected);
        log(`พบอุปกรณ์: ${bleDevice.name} กำลังเชื่อมต่อ...`, 'info');

        gattServer = await bleDevice.gatt.connect();
        log('เชื่อมต่อสำเร็จ! กำลังค้นหา Guardian GATT Services...', 'success');

        const service = await gattServer.getPrimaryService(SERVICE_UUID);

        // Get Characteristics
        infoChar = await service.getCharacteristic(CHAR_INFO_UUID);
        timeChar = await service.getCharacteristic(CHAR_TIME_UUID);
        commandChar = await service.getCharacteristic(CHAR_COMMAND_UUID);
        batteryChar = await service.getCharacteristic(CHAR_BATTERY_UUID);

        // Listen for Battery changes
        await batteryChar.startNotifications();
        batteryChar.addEventListener('characteristicvaluechanged', onBatteryChanged);

        // Listen for SOS commands from Watch
        await commandChar.startNotifications();
        commandChar.addEventListener('characteristicvaluechanged', onCommandReceived);

        // Read initial battery
        try {
            const batVal = await batteryChar.readValue();
            updateBatteryDisplay(batVal.getUint8(0));
        } catch (e) {
            console.warn('Initial bat read error', e);
        }

        // Set state to Connected
        setConnectedState(true);
        log('พร้อมทำงาน! เชื่อมต่อและป้องกันเด็กอยู่ในระยะแล้ว', 'success');

        // Automatically sync current time to watch
        await syncCurrentTimeToWatch();

        // Automatically sync registered parent info to watch
        await sendInfoToWatch();

    } catch (error) {
        log(`เชื่อมต่อไม่สำเร็จ: ${error.message || error}`, 'error');
        setConnectedState(false);
    }
}

function onDisconnected() {
    log('🚨 ขาดการเชื่อมต่อกับนาฬิกา! (Out of Range / หลุดระยะ)', 'error');
    setConnectedState(false, true); // Trigger out-of-range alarm!
}

function disconnectBLE() {
    if (bleDevice && bleDevice.gatt.connected) {
        bleDevice.gatt.disconnect();
    }
    setConnectedState(false);
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
        await timeChar.writeValue(encoder.encode(timeStr));

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
        const cName = inputChildName.value.trim() || "Kiddo";
        const pName = inputParentName.value.trim() || "Mom/Dad";
        const pPhone = inputParentPhone.value.trim() || "0812345678";
        const payload = `${pName}|${pPhone}|${cName}`;

        const encoder = new TextEncoder();
        await infoChar.writeValue(encoder.encode(payload));
        log(`บันทึกข้อมูลผู้ปกครองลง Flash บนนาฬิกาสำเร็จ: ${payload}`, 'success');
    } catch (e) {
        log(`เกิดข้อผิดพลาดในการส่งข้อมูล: ${e.message}`, 'error');
    }
}

async function ringWatchBuzzer() {
    if (!commandChar) return;
    try {
        const cmd = new Uint8Array([0x01]); // 0x01 = Ring buzzer
        await commandChar.writeValue(cmd);
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
        btnConnect.querySelector('span').textContent = 'ตัดการเชื่อมต่อ (Disconnect)';
        btnConnect.classList.remove('btn-primary');
        btnConnect.classList.add('btn-outline');

        btnSyncTime.disabled = false;
        btnRingWatch.disabled = false;

        rangeStatusText.textContent = 'ปลอดภัย (Safe)';
        rangeStatusText.className = 'stat-value safe-tag';
        rangeStatusText.style.color = 'var(--accent-emerald)';

        dismissEmergencyAlarm();
    } else {
        connPill.className = 'connection-status-pill';
        connText.textContent = 'ยังไม่เชื่อมต่อ (Disconnected)';
        btnConnect.querySelector('span').textContent = 'เชื่อมต่อ BLE กับนาฬิกา';
        btnConnect.classList.add('btn-primary');
        btnConnect.classList.remove('btn-outline');

        btnSyncTime.disabled = true;
        btnRingWatch.disabled = true;

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
