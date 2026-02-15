#include <Arduino.h>
#include <stdarg.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include <SPI.h>
#include <usbhub.h>
#include <BTD.h>
#include <BTHID.h>
// Resolve macro conflicts between USB Host Shield 2.0 and ESP-IDF USB stack
#undef USB_TRANSFER_TYPE_CONTROL
#undef USB_TRANSFER_TYPE_ISOCHRONOUS
#undef USB_TRANSFER_TYPE_BULK
#undef USB_TRANSFER_TYPE_INTERRUPT
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define WITH_KEYBOARD true
#define USE_MAX_MODULE true

// Set true to show poll-rate / queue / send-rate overlay
#define DEBUG_MODE false

uint8_t ATOM_MAC_1[] = {0x3C, 0xDC, 0x75, 0xED, 0xFB, 0x4C};
uint8_t ATOM_MAC_2[] = {0xD0, 0xCF, 0x13, 0x0F, 0x90, 0x48};

// XOR key for keyboard payload obfuscation (must match AtomS3U)
static const uint8_t KBD_XOR[7] = {0x4B,0x56,0x4D,0x53,0x77,0x31,0x7A};

#define BLE_KBD_MATCH "Keyboard"
#define ESPNOW_CHAN 1
#define SWITCH_DEBOUNCE_MS 300
#define HEARTBEAT_MS 500
#define BLE_SCAN_BUDGET_MS 20000
#define SEEN_DEV_MAX 48
#define SEEN_NAME_LEN 40
#define BLE_PROBE_MAX 8
#define BLE_PROBE_MIN_RSSI -50
#define SCAN_ROWS 10
#define TOP_DISPLAY SCAN_ROWS
#define PASSKEY_DISPLAY_MS 30000
#define APPLE_PAIR_TIMEOUT_MS 20000
#define MAX_CONNECT_RETRIES 5
#define SCAN_REDRAW_MS 1000
#define BLE_SCAN_BURST_MS 3000
#define BLE_SCAN_GAP_MS 200

// MAX3421E pins (CoreS3 SE + Module-LLM MAX3421E)
#define MX_SCK  36
#define MX_MOSI 37
#define MX_MISO 35
#define MX_CS   1
#define MX_INT  10

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_CONSUMER 0x06

#pragma pack(push, 1)
typedef struct {
  uint8_t buttons;
  int16_t x, y;
  int8_t wheel;
} mouse_evt_t;
typedef struct {
  uint8_t modifiers;
  uint8_t keys[6];
} kbd_evt_t;
#pragma pack(pop)

static QueueHandle_t mouseQueue, kbdQueue;
static SemaphoreHandle_t seenMutex;
static int activeTarget = 0;
static volatile bool usbMouseReady = false;
static volatile bool bleKbdConnected = false;
static volatile int bleState = 0;
static char bleFoundName[64] = "";
static char bleStatusMsg[64] = "";
static char bleConnAddr[20] = "";
static esp_now_peer_info_t peer1 = {}, peer2 = {};
static uint32_t lastSwitchMs = 0;
static volatile bool needRedraw = false;
static volatile int bleMatchReason = 0;
static volatile bool passkeyActive = false;
static volatile uint32_t passkeyValue = 0, passkeyShownMs = 0;
static volatile bool pairingSuccess = false, pairingDone = false;
static volatile bool switchInProgress = false;
static volatile bool switchRequest = false;
static NimBLEClient *pc = nullptr;

static NimBLEAddress knownKbdAddr;
static volatile bool knownKbdValid = false;
static volatile bool probesDone = false;

static volatile bool maxPresent = false;
static volatile bool maxDongleReady = false;
static char maxStatusMsg[32] = "";
static volatile bool classicKbdKnown = false;  // remember classic BT was connected
static volatile bool appleFnDown = false;       // Apple fn/globe key state
static volatile bool appleRawFn = false;        // raw fn bit from keyboard byte 8
static volatile bool appleRawLock = false;      // raw lock bit from keyboard byte 8
static volatile int kbdBatteryPct = -1;         // keyboard battery % (-1 = unknown)

RTC_NOINIT_ATTR char rtcDbg[48];
static bool wasReboot = false;
temperature_sensor_handle_t temp_handle = NULL;

static volatile uint32_t lastActivityMs = 0;
static volatile uint32_t lastPCSwitchMs = 0;
static uint32_t lastScreenTouchMs = 0;
static volatile int lastIdleLevel = 0;
static volatile bool screenTurnedOff = false;
static volatile bool wifiNeedsBackToNormal = false;

#define IDLE1_TIMEOUT_MS 10000
#define IDLE2_TIMEOUT_MS 60000
#define IDLE3_TIMEOUT_MS 300000
#define IDLE4_TIMEOUT_MS 900000

#define IDLE1_HEARTBEAT_MS 500
#define IDLE2_HEARTBEAT_MS 600
#define IDLE3_HEARTBEAT_MS 750
#define IDLE4_HEARTBEAT_MS 1000

#define IDLE1_LOOP_MS 1
#define IDLE2_LOOP_MS 20
#define IDLE3_LOOP_MS 50
#define IDLE4_LOOP_MS 100

static int idleLevel() {
  uint32_t dt = millis() - lastActivityMs;
  if (dt > IDLE4_TIMEOUT_MS) return 4;
  if (dt > IDLE3_TIMEOUT_MS) return 3;
  if (dt > IDLE2_TIMEOUT_MS) return 2;
  if (dt > IDLE1_TIMEOUT_MS) return 1;
  return 0;
}

// ── Debug stats (compiled out when DEBUG_MODE=false) ──
#if DEBUG_MODE

static volatile uint32_t dbgUsbCb = 0, dbgUsbHz = 0;
static volatile uint32_t dbgEspTx = 0, dbgEspHz = 0;
static volatile uint32_t dbgMsTx = 0, dbgMsHz = 0;
static volatile int dbgQPeak = 0;
static uint32_t dbgLastTick = 0;
static void dbgTick() {
  uint32_t now = millis();
  if (now - dbgLastTick >= 1000) {
    dbgUsbHz = dbgUsbCb;
    dbgUsbCb = 0;
    dbgEspHz = dbgEspTx;
    dbgEspTx = 0;
    dbgMsHz = dbgMsTx;
    dbgMsTx = 0;
    dbgQPeak = 0;
    dbgLastTick = now;
    needRedraw = true;
  }
}
#endif

struct seen_dev_t {
  NimBLEAddress addr;
  char name[SEEN_NAME_LEN];
  char mac_short[12];
  char status[16];
  int rssi;
  bool used, hasHidUuid, isApple, isClassic, probeTarget;
};
static seen_dev_t seenDevs[SEEN_DEV_MAX];
static volatile int seenDevCount = 0, bleScanTotal = 0;


static void shortMac(const char *f, char *o, size_t s) {
  if (strlen(f) >= 17)
    snprintf(o, s, "%c%c:%c%c..%c%c", f[0], f[1], f[3], f[4], f[15], f[16]);
  else {
    strncpy(o, f, s - 1);
    o[s - 1] = '\0';
  }
}

static void resetSeenDevs() {
  xSemaphoreTake(seenMutex, portMAX_DELAY);
  for (int i = 0; i < SEEN_DEV_MAX; i++) seenDevs[i].used = false;
  seenDevCount = 0;
  bleScanTotal = 0;
  xSemaphoreGive(seenMutex);
}
static int findSeenDevLocked(const NimBLEAddress &a) {
  for (int i = 0; i < seenDevCount; i++)
    if (seenDevs[i].used && seenDevs[i].addr == a) return i;
  return -1;
}
static int addSeenDev(const NimBLEAddress &addr, const char *name, int rssi,
                      bool hasHid, bool apple, bool classic) {
  xSemaphoreTake(seenMutex, portMAX_DELAY);
  int idx = findSeenDevLocked(addr);
  if (idx >= 0) {
    if (name && name[0] && seenDevs[idx].name[0] == '\0') {
      strncpy(seenDevs[idx].name, name, SEEN_NAME_LEN - 1);
      seenDevs[idx].name[SEEN_NAME_LEN - 1] = '\0';
    }
    if (apple) seenDevs[idx].isApple = true;
    if (hasHid) seenDevs[idx].hasHidUuid = true;
    if (rssi != 0 && (seenDevs[idx].rssi == 0 || rssi > seenDevs[idx].rssi))
      seenDevs[idx].rssi = rssi;
    xSemaphoreGive(seenMutex);
    return idx;
  }
  if (seenDevCount >= SEEN_DEV_MAX) {
    xSemaphoreGive(seenMutex);
    return -1;
  }
  int i = seenDevCount;
  seenDevs[i].addr = addr;
  seenDevs[i].rssi = rssi;
  seenDevs[i].used = true;
  seenDevs[i].hasHidUuid = hasHid;
  seenDevs[i].isApple = apple;
  seenDevs[i].isClassic = classic;
  seenDevs[i].probeTarget = false;
  seenDevs[i].status[0] = '\0';
  shortMac(addr.toString().c_str(), seenDevs[i].mac_short,
           sizeof(seenDevs[i].mac_short));
  if (name && name[0]) {
    strncpy(seenDevs[i].name, name, SEEN_NAME_LEN - 1);
    seenDevs[i].name[SEEN_NAME_LEN - 1] = '\0';
  } else
    seenDevs[i].name[0] = '\0';
  seenDevCount = i + 1;
  xSemaphoreGive(seenMutex);
  return i;
}
static int getTopByRssi(int *out, int maxOut) {
  xSemaphoreTake(seenMutex, portMAX_DELAY);
  int all[SEEN_DEV_MAX], n = 0;
  for (int i = 0; i < seenDevCount && i < SEEN_DEV_MAX; i++)
    if (seenDevs[i].used) all[n++] = i;
  for (int a = 0; a < n - 1; a++)
    for (int b = a + 1; b < n; b++)
      if (seenDevs[all[b]].rssi > seenDevs[all[a]].rssi) {
        int t = all[a];
        all[a] = all[b];
        all[b] = t;
      }
  int c = (n < maxOut) ? n : maxOut;
  for (int i = 0; i < c; i++) out[i] = all[i];
  xSemaphoreGive(seenMutex);
  return c;
}

// ===================== ESP-NOW =====================
static void onEspNowSendDone(const uint8_t *, esp_now_send_status_t) {}
static uint8_t *activeMac() {
  return (activeTarget == 0) ? ATOM_MAC_1 : ATOM_MAC_2;
}
static uint8_t *inactiveMac() {
  return (activeTarget == 0) ? ATOM_MAC_2 : ATOM_MAC_1;
}
static void sendCmd(uint8_t *mac, uint8_t cmd) { esp_now_send(mac, &cmd, 1); }
static void switchTarget() {
  switchInProgress = true;
  vTaskDelay(pdMS_TO_TICKS(2));
  xQueueReset(mouseQueue);
  sendCmd(activeMac(), PKT_DEACTIVATE);
  activeTarget = 1 - activeTarget;
  sendCmd(activeMac(), PKT_ACTIVATE);
  delayMicroseconds(800);
  sendCmd(activeMac(), PKT_ACTIVATE);
  xQueueReset(mouseQueue);
  lastSwitchMs = millis();
  switchInProgress = false;
  needRedraw = true;
}
static const uint8_t broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static void txMouse(const mouse_evt_t *m) {
  uint8_t p[7] = {PKT_MOUSE,
                  (uint8_t)(m->buttons & 0x07),
                  (uint8_t)(m->x & 0xFF),
                  (uint8_t)((m->x >> 8) & 0xFF),
                  (uint8_t)(m->y & 0xFF),
                  (uint8_t)((m->y >> 8) & 0xFF),
                  (uint8_t)m->wheel};
  if (esp_now_send(broadcastMac, p, 7) != ESP_OK) {
    taskYIELD();  // let WiFi task drain TX queue
    esp_now_send(broadcastMac, p, 7);  // one retry
  }
#if DEBUG_MODE
  dbgEspTx++;
#endif
}
static void txKbd(const kbd_evt_t *k) {
  uint8_t p[8] = {PKT_KEYBOARD, k->modifiers};
  memcpy(&p[2], k->keys, 6);
  for (int i = 0; i < 7; i++) p[1 + i] ^= KBD_XOR[i];
  const uint8_t *dst = activeMac();
  for (int attempt = 0; attempt < 3; attempt++) {
    if (esp_now_send(dst, p, 8) == ESP_OK) break;
    taskYIELD();
  }
}

// ===================== USB Host Shield 2.0 — BTHID =====================
#if USE_MAX_MODULE

// Apple F-key → consumer usage mapping (when fn NOT pressed)
// 0 = no consumer mapping (pass through as F-key or handle as shortcut)
#define APPLE_FKEY_FIRST 0x3A  // F1
#define APPLE_FKEY_LAST  0x45  // F12
#define FKEY_IS_SHORTCUT 0xFFFF  // sentinel: handle as keyboard shortcut

static const uint16_t appleFkeyMap[12] = {
  0x0070,           // F1  → Brightness Decrement
  0x006F,           // F2  → Brightness Increment
  FKEY_IS_SHORTCUT, // F3  → Mission Control (Ctrl+Up shortcut)
  0x0221,           // F4  → AC Search (Spotlight)
  0x00CF,           // F5  → Voice Command (Dictation)
  0,                // F6  → DND (pass through as F6 — no standard usage)
  0x00B6,           // F7  → Scan Previous Track
  0x00CD,           // F8  → Play/Pause
  0x00B5,           // F9  → Scan Next Track
  0x00E2,           // F10 → Mute
  0x00EA,           // F11 → Volume Decrement
  0x00E9,           // F12 → Volume Increment
};

static uint16_t activeFkeyConsumer = 0;     // currently-held consumer usage from F-key
static volatile bool appleF3Request = false; // flag for Mission Control shortcut

class KvmKbdParser : public HIDReportParser {
public:
  void Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) override {
    if (len < 8) return;

    // Apple: extract fn state from byte 8 FIRST
    bool isApple = (len >= 9);
    bool fnHeld = false;
    if (isApple) {
      appleRawFn = (buf[8] & 0x02) != 0;
      appleRawLock = (buf[8] & 0x08) != 0;
      fnHeld = appleRawFn;
    }

    kbd_evt_t evt;
    evt.modifiers = buf[0];
    memcpy(evt.keys, &buf[2], 6);

    // Apple F-key → consumer remap ONLY when fn (globe) is NOT held.
    // When fn IS held: F1-F12 pass through as standard function keys.
    uint16_t newConsumer = 0;
    if (isApple && !fnHeld) {
      for (int i = 0; i < 6; i++) {
        uint8_t k = evt.keys[i];
        if (k >= APPLE_FKEY_FIRST && k <= APPLE_FKEY_LAST) {
          uint16_t usage = appleFkeyMap[k - APPLE_FKEY_FIRST];
          if (usage == FKEY_IS_SHORTCUT) {
            // F3 → Mission Control: flag for post-processing, remove from report
            appleF3Request = true;
            evt.keys[i] = 0;
          } else if (usage != 0) {
            newConsumer = usage;
            evt.keys[i] = 0;  // remove F-key from keyboard report
          }
          // usage == 0: leave key as-is (F6 passes through as F6)
        }
      }
    }

    // Consumer press/release tracking
    if (newConsumer != activeFkeyConsumer) {
      if (activeFkeyConsumer != 0) {
        uint8_t rel[3] = {PKT_CONSUMER, 0x00, 0x00};
        esp_now_send(activeMac(), rel, 3);
      }
      if (newConsumer != 0) {
        uint8_t p[3] = {PKT_CONSUMER, (uint8_t)(newConsumer & 0xFF), (uint8_t)(newConsumer >> 8)};
        esp_now_send(activeMac(), p, 3);
      }
      activeFkeyConsumer = newConsumer;
    }

    xQueueSend(kbdQueue, &evt, 0);
    lastActivityMs = millis();
  }
};

// Subclass BTHID to handle Apple vendor reports (fn/globe key etc.)
class KvmBTHID : public BTHID {
public:
  KvmBTHID(BTD *p, bool pair, const char *pin) : BTHID(p, pair, pin) {}

  // Expose protected Reset() for forced disconnect recovery
  void forceReset() { Reset(); }

  // Don't send SET_PROTOCOL to Apple keyboard — it always sends native 9-byte reports.
  // Sending SET_PROTOCOL(boot) makes it switch to 8-byte on reconnect, losing fn/lock byte.
  void setProtocol() override {}

  void ParseBTHIDData(uint8_t len, uint8_t *buf) override {
    if (len < 2) return;
    uint8_t rptId = buf[0];
    // Standard keyboard (0x01) and mouse (0x02) handled by library parsers
    if (rptId == 0x01 || rptId == 0x02) return;

    lastActivityMs = millis();

    // Apple vendor report 0x90: fn/globe key — actual fn handled via byte 8, ignore here
    if (rptId == 0x90) return;

    // Apple battery report 0xF0: sent periodically on interrupt channel
    if (rptId == 0xF0 && len >= 4) {
      int pct = buf[3];
      if (pct >= 0 && pct <= 100 && pct != kbdBatteryPct) {
        kbdBatteryPct = pct;
        needRedraw = true;
      }
      return;
    }

    // Forward all other reports as consumer keys (Apple consumer: media, etc.)
    Serial.printf("BT rpt 0x%02X len=%d: %02X %02X\n", rptId, len, buf[1], len >= 3 ? buf[2] : 0);
    if (len >= 3) {
      uint16_t usage = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
      uint8_t p[3] = {PKT_CONSUMER, (uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
      esp_now_send(activeMac(), p, 3);
    }
  }
};

USB Usb;
BTD Btd(&Usb);
KvmBTHID bthid(&Btd, PAIR, "0000");
KvmKbdParser kvmKbdParser;

// ── Variables for UI status ──
static volatile bool mxClassicConnected = false;

void max3421Poll() {
  Usb.Task();

  static bool wasReady = false;
  static bool wasConnected = false;

  bool dongleReady = Btd.isReady();
  if (dongleReady && !wasReady) {
    maxPresent = true;
    maxDongleReady = true;
    snprintf(maxStatusMsg, sizeof(maxStatusMsg), "inquiry...");
    Serial.println("BTD ready");
    needRedraw = true;
  }
  wasReady = dongleReady;

  // Monitor SSP passkey for display
  static uint32_t lastPasskey = 0xFFFFFFFF;
  if (Btd.sspPasskey != lastPasskey) {
    lastPasskey = Btd.sspPasskey;
    if (lastPasskey != 0) {
      passkeyActive = true;
      passkeyValue = lastPasskey;
      passkeyShownMs = millis();
      Serial.printf("PASSKEY: %06d\n", (int)lastPasskey);
      needRedraw = true;
    }
  }

  bool connected = bthid.connected;
  if (connected && !wasConnected) {
    mxClassicConnected = true;
    classicKbdKnown = true;
    bleKbdConnected = true;
    appleFnDown = false;
    activeFkeyConsumer = 0;
    kbdBatteryPct = -1;
    if (Btd.remote_name[0])
      strncpy(bleFoundName, (const char *)Btd.remote_name, sizeof(bleFoundName) - 1);
    else
      strncpy(bleFoundName, "Classic HID", sizeof(bleFoundName) - 1);
    bleState = 4;
    snprintf(maxStatusMsg, sizeof(maxStatusMsg), "classic HID!");
    Serial.println("BTHID connected");
    needRedraw = true;
  } else if (!connected && wasConnected) {
    mxClassicConnected = false;
    bleKbdConnected = false;
    kbdBatteryPct = -1;
    appleFnDown = false;
    activeFkeyConsumer = 0;
    bleState = 5;
    snprintf(maxStatusMsg, sizeof(maxStatusMsg), "classic disc");
    Serial.println("BTHID disconnected");
    // Force-clear L2CAP claim so reconnection can start fresh.
    // HCI_DISCONNECT_STATE normally does this, but our nudge may bypass it.
    Btd.l2capConnectionClaimed = false;
    needRedraw = true;
  }
  wasConnected = connected;

  // Classic BT battery comes via interrupt channel (report 0xF0) — no polling needed

  // Track connecting state for UI (between inquiry and connected)
  static bool wasConnecting = false;
  if (dongleReady && !connected) {
    bool connecting = Btd.connectToHIDDevice;
    if (connecting && !wasConnecting) {
      snprintf(maxStatusMsg, sizeof(maxStatusMsg), "connecting...");
      needRedraw = true;
    } else if (!connecting && wasConnecting && !connected) {
      snprintf(maxStatusMsg, sizeof(maxStatusMsg), "inquiry...");
      needRedraw = true;
    }
    wasConnecting = connecting;
  } else {
    wasConnecting = false;
  }

  // ── Reconnection safety net ──
  // Nudge inquiry ONCE, then wait up to 20s for library to connect.
  // Problem solved: previous code nudged repeatedly, interrupting auth/L2CAP in progress.
  static bool inquiryNudged = false;
  static uint32_t nudgeTimeMs = 0;
  if (classicKbdKnown && dongleReady && !connected) {
    if (!Btd.pairWithHIDDevice)
      Btd.pairWithHIDDevice = true;

    if (!inquiryNudged) {
      // Nudge once to start inquiry
      uint8_t hs = Btd.hci_state;
      if (hs == HCI_SCANNING_STATE || hs == HCI_CONNECT_IN_STATE) {
        Btd.hci_state = HCI_CHECK_DEVICE_SERVICE;
        inquiryNudged = true;
        nudgeTimeMs = millis();
      }
    } else {
      // Already nudged — wait for library to complete connect+auth+L2CAP
      if (millis() - nudgeTimeMs > 20000) {
        Serial.println("BT: 20s timeout, retry");
        Btd.l2capConnectionClaimed = false;  // Clear before reset so next attempt can start L2CAP
        bthid.forceReset();
        Btd.hci_disconnect(Btd.hci_handle);
        inquiryNudged = false;  // will nudge again next cycle
      }
    }
  } else {
    inquiryNudged = false;
    nudgeTimeMs = 0;
  }

  // ── Apple fn/lock from byte 8 ──
  // byte 8 bit 1 (0x02) = fn/globe, bit 3 (0x08) = lock screen
  // fn is delayed 50ms so lock can suppress it. Lock is sent immediately.
  {
    static bool prevRawFn = false, prevRawLock = false;
    static uint32_t fnChangeMs = 0;
    static bool fnSentState = false, lockSentState = false;

    bool rawFn = appleRawFn;
    bool rawLock = appleRawLock;

    if (rawFn != prevRawFn) {
      fnChangeMs = millis();
      prevRawFn = rawFn;
    }
    if (rawLock != prevRawLock) {
      prevRawLock = rawLock;
    }

    // Lock — send as Ctrl+Cmd+Q keyboard shortcut (macOS lock screen)
    if (rawLock && !lockSentState) {
      lockSentState = true;
      // If fn consumer key is active, cancel it first so macOS doesn't get confused
      if (appleFnDown) {
        uint8_t cancel[3] = {PKT_CONSUMER, 0x00, 0x00};
        esp_now_send(activeMac(), cancel, 3);
        delay(10);
      }
      fnSentState = rawFn;  // suppress pending fn
      appleFnDown = rawFn;
      // Press Ctrl+Cmd+Q
      kbd_evt_t lockEvt = {};
      lockEvt.modifiers = 0x09;  // LCtrl(0x01) + LGui(0x08)
      lockEvt.keys[0] = 0x14;   // Q
      txKbd(&lockEvt);
      delay(30);
      // Release
      kbd_evt_t relEvt = {};
      txKbd(&relEvt);
    }
    if (!rawLock && lockSentState) {
      lockSentState = false;
    }

    // fn — 50ms debounce, suppressed when lock is held
    if (rawFn != fnSentState) {
      if (rawLock) {
        fnSentState = rawFn;
        appleFnDown = rawFn;
      } else if (millis() - fnChangeMs >= 50) {
        fnSentState = rawFn;
        appleFnDown = rawFn;
        if (rawFn) {
          // Press: always send globe
          uint8_t p[3] = {PKT_CONSUMER, 0x9D, 0x02};
          esp_now_send(activeMac(), p, 3);
        } else if (activeFkeyConsumer == 0) {
          // Release: only if no F-key consumer is active (would cancel it)
          uint8_t p[3] = {PKT_CONSUMER, 0x00, 0x00};
          esp_now_send(activeMac(), p, 3);
        }
      }
    }
  }

  // F3 → Mission Control: Ctrl + Up Arrow
  if (appleF3Request) {
    appleF3Request = false;
    kbd_evt_t mc = {};
    mc.modifiers = 0x01;  // LCtrl
    mc.keys[0] = 0x52;    // Up Arrow
    txKbd(&mc);
    delay(30);
    kbd_evt_t rel = {};
    txKbd(&rel);
  }

}
static inline bool classicBtConnected() { return bthid.connected; }
static inline const char *classicBtName() { return (const char *)Btd.remote_name; }
#else
static inline void max3421Poll() {}
static inline bool classicBtConnected() { return false; }
static inline const char *classicBtName() { return ""; }
static volatile bool mxClassicConnected = false;
#endif

// ===================== USB MOUSE HOST =====================
static usb_host_client_handle_t usbClient = NULL;
static usb_device_handle_t usbDev = NULL;
static usb_transfer_t *xfers[2] = {NULL, NULL};
static uint8_t epAddr = 0, hidIfNum = 0;
static volatile bool devGone = false, xferDead = false;
static uint32_t xferDeadMs = 0;
static volatile uint32_t lastXferCbMs = 0;
static int recoverFails = 0;
static volatile bool usbResetRequest = false;

static bool findHidEp();

static void mouseXferCb(usb_transfer_t *xfer) {
  lastXferCbMs = millis();
#if DEBUG_MODE
  dbgUsbCb++;
#endif

  if (switchInProgress) {
    if (usbDev && xfer) {
      if (usb_host_transfer_submit(xfer) != ESP_OK) xferDead = true;
    } else {
      xferDead = true;
    }
    return;
  }

  if (xfer->status == USB_TRANSFER_STATUS_COMPLETED &&
      xfer->actual_num_bytes > 0) {
    lastActivityMs = millis();
    uint8_t *d = xfer->data_buffer;
    int len = xfer->actual_num_bytes;
    mouse_evt_t evt = {};
    if (len == 4) {
      evt.buttons = d[0];
      evt.x = (int8_t)d[1];
      evt.y = (int8_t)d[2];
      evt.wheel = (int8_t)d[3];
    } else if (len == 5) {
      evt.buttons = d[1];
      evt.x = (int8_t)d[2];
      evt.y = (int8_t)d[3];
      evt.wheel = (int8_t)d[4];
    } else if (len == 6) {
      evt.buttons = d[0];
      evt.x = (int16_t)(d[1] | (d[2] << 8));
      evt.y = (int16_t)(d[3] | (d[4] << 8));
      evt.wheel = (int8_t)d[5];
    } else if (len >= 7) {
      evt.buttons = d[1];
      evt.x = (int16_t)(d[2] | (d[3] << 8));
      evt.y = (int16_t)(d[4] | (d[5] << 8));
      evt.wheel = (int8_t)d[6];
    }
    xQueueSend(mouseQueue, &evt, 0);
  }

  if (xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    xferDead = true;
    return;
  }

  if (usbDev && xfer) {
    if (usb_host_transfer_submit(xfer) != ESP_OK) xferDead = true;
  } else
    xferDead = true;
}

static void usbEventCb(const usb_host_client_event_msg_t *msg, void *) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if (!usbDev) usb_host_device_open(usbClient, msg->new_dev.address, &usbDev);
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE)
    devGone = true;
}

static void cleanupUsb() {
  usbMouseReady = false;
  usb_host_client_handle_events(usbClient, 10);
  for (int i = 0; i < 2; i++) {
    if (xfers[i]) {
      usb_host_transfer_free(xfers[i]);
      xfers[i] = NULL;
    }
  }

  if (usbDev) {
    if (epAddr) usb_host_interface_release(usbClient, usbDev, hidIfNum);
    usb_host_device_close(usbClient, usbDev);
    usbDev = NULL;
  }
  epAddr = 0;
  xferDead = false;
  xferDeadMs = 0;
  lastXferCbMs = 0;
  usb_host_lib_handle_events(10, NULL);
}

static bool softRecoverUsb() {
  if (!usbDev) return false;
  for (int i = 0; i < 2; i++) {
    if (xfers[i]) {
      usb_host_transfer_free(xfers[i]);
      xfers[i] = NULL;
    }
  }
  if (epAddr) {
    usb_host_interface_release(usbClient, usbDev, hidIfNum);
    epAddr = 0;
  }
  usbMouseReady = false;
  xferDead = false;
  xferDeadMs = 0;
  lastXferCbMs = 0;
  if (devGone) return false;
  vTaskDelay(pdMS_TO_TICKS(200));
  if (devGone) return false;
  if (findHidEp()) {
    usbMouseReady = true;
    lastXferCbMs = millis();
    recoverFails = 0;
    needRedraw = true;
    return true;
  }
  recoverFails++;
  return false;
}

static bool findHidEp() {
  if (!usbDev) return false;
  const usb_config_desc_t *cfg;
  if (usb_host_get_active_config_descriptor(usbDev, &cfg) != ESP_OK)
    return false;
  const uint8_t *p = (const uint8_t *)cfg;
  int off = 0, total = cfg->wTotalLength;
  uint8_t curIf = 0;
  bool isH = false;

  while (off < total) {
    uint8_t dL = p[off], dT = (dL >= 2) ? p[off + 1] : 0;
    if (!dL) break;
    if (dT == 0x04 && dL >= 9) {
      curIf = p[off + 2];
      isH = (p[off + 5] == 3);
    }
    if (dT == 0x05 && dL >= 7 && isH) {
      uint8_t a = p[off + 2], at = p[off + 3];
      uint16_t mps = p[off + 4] | (p[off + 5] << 8);
      if ((at & 3) == 3 && (a & 0x80)) {
        hidIfNum = curIf;
        if (usb_host_interface_claim(usbClient, usbDev, hidIfNum, 0) != ESP_OK)
          return false;

        for (int i = 0; i < 2; i++) {
          if (usb_host_transfer_alloc(mps, 0, &xfers[i]) != ESP_OK) {
            if (i > 0) usb_host_transfer_free(xfers[0]);
            return false;
          }
          xfers[i]->device_handle = usbDev;
          xfers[i]->bEndpointAddress = a;
          xfers[i]->callback = mouseXferCb;
          xfers[i]->num_bytes = mps;
          xfers[i]->timeout_ms = 0;

          if (usb_host_transfer_submit(xfers[i]) != ESP_OK) {
            usb_host_transfer_free(xfers[i]);
            xfers[i] = NULL;
            return false;
          }
        }

        epAddr = a;
        xferDead = false;
        xferDeadMs = 0;
        lastXferCbMs = millis();
        return true;
      }
    }
    off += dL;
  }
  return false;
}

void usbHostTask(void *) {
  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags = ESP_INTR_FLAG_LEVEL2;
  usb_host_install(&hc);
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 5;
  cc.async.client_event_callback = usbEventCb;
  usb_host_client_register(&cc, &usbClient);

  while (true) {
    int il = idleLevel();
    int evtTimeout = (il >= 4) ? 100 : (il >= 3) ? 50 : (il >= 2) ? 20 : 1;
    usb_host_lib_handle_events(evtTimeout, NULL);
    usb_host_client_handle_events(usbClient, 0);

    if (usbResetRequest) {
      usbResetRequest = false;
      if (!usbMouseReady || xferDead) {
        cleanupUsb();
        needRedraw = true;
      }
    }

    if (recoverFails >= 3) {
      strncpy(rtcDbg, "usb-recover-fail", sizeof(rtcDbg));
      ESP.restart();
    }

    if (devGone) {
      devGone = false;
      cleanupUsb();
      needRedraw = true;
    }

    if (xferDead && usbDev) {
      if (!xferDeadMs) xferDeadMs = millis();
      uint32_t deadElapsed = millis() - xferDeadMs;
      if (deadElapsed > 6000) {
        if (!softRecoverUsb()) {
          cleanupUsb();
          needRedraw = true;
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(50));
        bool anyOk = false;
        for (int i = 0; i < 2; i++) {
          if (xfers[i] && usb_host_transfer_submit(xfers[i]) == ESP_OK)
            anyOk = true;
        }
        if (anyOk) {
          xferDead = false;
          xferDeadMs = 0;
          lastXferCbMs = millis();
        }
      }
    }

    if (usbDev && !epAddr) {
      vTaskDelay(pdMS_TO_TICKS(200));
      if (findHidEp()) {
        usbMouseReady = true;
        recoverFails = 0;
        needRedraw = true;
      }
    }

    if (il >= 2) vTaskDelay(pdMS_TO_TICKS(evtTimeout));
    else taskYIELD();
  }
}

// ===================== MOUSE SEND TASK =====================
void mouseSendTask(void *) {
  mouse_evt_t m;
  bool m4h = false;
  while (true) {
    if (switchInProgress) {
      m4h = false;
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    TickType_t waitTicks;
    int il = idleLevel();
    if (il == 0) waitTicks = portMAX_DELAY;
    else if (il <= 2) waitTicks = pdMS_TO_TICKS(5);
    else waitTicks = pdMS_TO_TICKS(50);

    if (xQueueReceive(mouseQueue, &m, waitTicks) != pdTRUE) continue;

    // Handle button 4 (device switch)
    bool m4 = (m.buttons & 0x08);
    if (m4 && !m4h && millis() - lastSwitchMs > SWITCH_DEBOUNCE_MS)
      switchRequest = true;
    m4h = m4;

    txMouse(&m);
#if DEBUG_MODE
    dbgMsTx++;
#endif
  }
}

// ===================== BLE =====================
class SecurityCallbacks : public NimBLEClientCallbacks {
  void onPassKeyEntry(NimBLEConnInfo &ci) override {
    uint32_t pk = esp_random() % 1000000;
    passkeyValue = pk;
    passkeyActive = true;
    passkeyShownMs = millis();
    pairingDone = false;
    pairingSuccess = false;
    needRedraw = true;
    NimBLEDevice::injectPassKey(ci, pk);
  }
  void onConfirmPasskey(NimBLEConnInfo &ci, uint32_t pin) override {
    passkeyValue = pin;
    passkeyActive = true;
    passkeyShownMs = millis();
    pairingDone = false;
    needRedraw = true;
    NimBLEDevice::injectConfirmPasskey(ci, true);
  }
  void onAuthenticationComplete(NimBLEConnInfo &ci) override {
    pairingDone = true;
    pairingSuccess = (ci.isEncrypted() || ci.isBonded());
    needRedraw = true;
  }
  void onDisconnect(NimBLEClient *, int) override {}
};
static SecurityCallbacks secCb;
static NimBLEAddress bleAddr;
static volatile bool bleFound = false, bleScanDone = false;
static uint32_t lastScanRedraw = 0;

static void setProbeSecMode() {
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}
static void setKbdSecMode() {
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
}
static bool nameContains(const std::string &h, const char *n) {
  if (h.empty()) return false;
  std::string a = h, b = n;
  for (auto &c : a) c = tolower(c);
  for (auto &c : b) c = tolower(c);
  return a.find(b) != std::string::npos;
}
static void waitDisconn(NimBLEClient *c, int tmo = 2000) {
  if (!c || !c->isConnected()) return;
  c->disconnect();
  uint32_t t0 = millis();
  while (c->isConnected() && (int)(millis() - t0) < tmo)
    vTaskDelay(pdMS_TO_TICKS(50));
  vTaskDelay(pdMS_TO_TICKS(200));
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    bleScanTotal++;
    std::string name = dev->getName();
    int rssi = dev->getRSSI();
    bool hasHid = dev->isAdvertisingService(NimBLEUUID("1812"));
    bool isApple = false;
    if (dev->haveManufacturerData()) {
      std::string mfr = dev->getManufacturerData();
      if (mfr.size() >= 2 &&
          ((uint8_t)mfr[0] | ((uint8_t)mfr[1] << 8)) == 0x004C)
        isApple = true;
    }
    if (!isApple && !name.empty() &&
        (nameContains(name, "Apple") || nameContains(name, "Magic")))
      isApple = true;
    addSeenDev(dev->getAddress(), name.c_str(), rssi, hasHid, isApple, false);
    if (nameContains(name, BLE_KBD_MATCH)) {
      strncpy(bleFoundName, name.c_str(), sizeof(bleFoundName) - 1);
      bleAddr = dev->getAddress();
      bleFound = true;
      bleMatchReason = 1;
      bleState = 2;
      needRedraw = true;
      NimBLEDevice::getScan()->stop();
      return;
    }
    if (hasHid) {
      strncpy(bleFoundName, name.empty() ? "(HID)" : name.c_str(),
              sizeof(bleFoundName) - 1);
      bleAddr = dev->getAddress();
      bleFound = true;
      bleMatchReason = 2;
      bleState = 2;
      needRedraw = true;
      NimBLEDevice::getScan()->stop();
      return;
    }
    if (millis() - lastScanRedraw > SCAN_REDRAW_MS) {
      needRedraw = true;
      lastScanRedraw = millis();
    }
  }
  void onScanEnd(const NimBLEScanResults &, int) override {
    bleScanDone = true;
  }
};
static ScanCb scanCbInstance;
static void bleReportCb(NimBLERemoteCharacteristic *, uint8_t *d, size_t l,
                        bool) {
  if (l < 8) return;
  lastActivityMs = millis();
  kbd_evt_t evt;
  evt.modifiers = d[0];
  memcpy(evt.keys, &d[2], 6);
  xQueueSend(kbdQueue, &evt, 0);
}

static int probeHid(NimBLEClient *c, const NimBLEAddress &a, char *on,
                    size_t nl) {
  on[0] = '\0';
  int r = -1;
  if (!c) return r;
  if (c->isConnected()) waitDisconn(c);
  if (c->connect(a)) {
    r = 0;
    if (c->isConnected()) {
      readGapName(c, on, nl);
      if (c->isConnected() && c->getService(NimBLEUUID("1812"))) r = 1;
    }
    waitDisconn(c);
  }
  return r;
}

static bool readGapName(NimBLEClient *c, char *out, size_t maxLen) {
  out[0] = '\0';
  if (!c || !c->isConnected()) return false;
  NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
  if (!gap || !c->isConnected()) return false;
  NimBLERemoteCharacteristic *nm = gap->getCharacteristic(NimBLEUUID("2A00"));
  if (!nm || !nm->canRead() || !c->isConnected()) return false;
  std::string gn = nm->readValue();
  if (gn.empty()) return false;
  strncpy(out, gn.c_str(), maxLen - 1);
  out[maxLen - 1] = '\0';
  return true;
}

static int readBleBattery(NimBLEClient *c) {
  if (!c || !c->isConnected()) return -1;
  NimBLERemoteService *bas = c->getService(NimBLEUUID((uint16_t)0x180F));
  if (!bas || !c->isConnected()) return -1;
  NimBLERemoteCharacteristic *bl = bas->getCharacteristic(NimBLEUUID((uint16_t)0x2A19));
  if (!bl || !bl->canRead() || !c->isConnected()) return -1;
  uint8_t val = bl->readValue<uint8_t>();
  return (val <= 100) ? (int)val : -1;
}

static inline void resetPairState() {
  passkeyActive = false;
  pairingDone = false;
  pairingSuccess = false;
}

static void markHidFound(int si, int reason, const char *name) {
  if (si < 0 || si >= SEEN_DEV_MAX) return;
  bleAddr = seenDevs[si].addr;
  bleFound = true;
  bleMatchReason = reason;
  strncpy(bleFoundName, (name && name[0]) ? name : "(HID)", sizeof(bleFoundName) - 1);
  bleState = 2;
  needRedraw = true;
}

static void setDevStatus(int si, const char *fmt, ...) {
  if (si < 0 || si >= SEEN_DEV_MAX) return;
  char tmp[16];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  xSemaphoreTake(seenMutex, portMAX_DELAY);
  strncpy(seenDevs[si].status, tmp, sizeof(seenDevs[si].status) - 1);
  seenDevs[si].status[sizeof(seenDevs[si].status) - 1] = '\0';
  xSemaphoreGive(seenMutex);
  needRedraw = true;
}

// Device type string: CA, BA, C, B
static const char* devType(int si) {
  if (si < 0 || si >= SEEN_DEV_MAX) return "?";
  if (seenDevs[si].isClassic && seenDevs[si].isApple) return "CA";
  if (!seenDevs[si].isClassic && seenDevs[si].isApple) return "BA";
  if (seenDevs[si].isClassic) return "C";
  return "B";
}

static void doProbe(NimBLEClient *c, int si, int pn, int tot) {
  if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) return;
  seen_dev_t &d = seenDevs[si];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "%d/%d %s %s", pn, tot,
           devType(si), d.mac_short);
  bleState = 6;
  setDevStatus(si, "..");
  char pn2[SEEN_NAME_LEN] = "";
  int r = probeHid(c, d.addr, pn2, sizeof(pn2));
  const char *rs = (r == 1) ? "HID!" : (r == 0) ? "noHID" : "fail";
  setDevStatus(si, "%s", rs);
  if (r == 1) {
    int reason = (pn2[0] && nameContains(std::string(pn2), BLE_KBD_MATCH)) ? 3 : 4;
    markHidFound(si, reason, pn2);
  }
}

static void doAppleProbe(NimBLEClient *c, int si, int pn, int tot) {
  if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) return;
  seen_dev_t &d = seenDevs[si];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "%d/%d %s %s", pn, tot,
           devType(si), d.mac_short);
  bleState = 6;
  setDevStatus(si, "pair..");
  resetPairState();
  if (c->isConnected()) waitDisconn(c);
  if (!c->connect(d.addr)) {
    setDevStatus(si, "noConn");
    passkeyActive = false;
    return;
  }
  NimBLERemoteService *hid =
      c->isConnected() ? c->getService(NimBLEUUID("1812")) : nullptr;
  if (!hid && passkeyActive && !pairingDone && c->isConnected()) {
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "type PIN on kbd!");
    setDevStatus(si, "PIN..");
    uint32_t t0 = millis();
    while (!pairingDone && c->isConnected() &&
           (millis() - t0 < APPLE_PAIR_TIMEOUT_MS))
      vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!hid && pairingDone && pairingSuccess && c->isConnected())
    hid = c->getService(NimBLEUUID("1812"));
  char pn2[SEEN_NAME_LEN] = "";
  readGapName(c, pn2, sizeof(pn2));
  bool ok = (hid && c->isConnected());
  const char *rs = ok                                 ? "HID!"
                   : (pairingDone && !pairingSuccess) ? "pfail"
                   : (passkeyActive && !pairingDone)  ? "tmout"
                                                      : "noHID";
  setDevStatus(si, "%s", rs);
  if (ok)
    markHidFound(si, 5, pn2[0] ? pn2 : "(Apple HID)");
  if (c->isConnected()) waitDisconn(c, 5000);
  resetPairState();
  vTaskDelay(pdMS_TO_TICKS(300));
}

static bool bleDoConnect() {
  bleState = 3;
  char sm[12];
  shortMac(bleAddr.toString().c_str(), sm, sizeof(sm));
  strncpy(bleConnAddr, sm, sizeof(bleConnAddr));
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "connect %s", sm);
  needRedraw = true;
  resetPairState();
  NimBLEClient *c = nullptr;
  bool connected = false;
  for (int att = 0; att < MAX_CONNECT_RETRIES && !connected; att++) {
    if (att > 0) {
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "retry %d/%d %s", att + 1,
               MAX_CONNECT_RETRIES, sm);
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(1000 + att * 500));
    }
    resetPairState();
    c = NimBLEDevice::createClient();
    if (!c) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    c->setClientCallbacks(&secCb);
    c->setConnectionParams(6, 12, 0, 600);
    if (c->connect(bleAddr)) {
      connected = true;
      break;
    }
    NimBLEDevice::deleteClient(c);
    c = nullptr;
  }
  if (!connected) {
    bleState = 0;
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "connect fail %s", sm);
    needRedraw = true;
    return false;
  }
  if (bleFoundName[0] == '\0' || bleFoundName[0] == '(') {
    if (readGapName(c, bleFoundName, sizeof(bleFoundName)))
      needRedraw = true;
  }
  NimBLERemoteService *hid =
      c->isConnected() ? c->getService(NimBLEUUID("1812")) : nullptr;
  if (!hid && passkeyActive && !pairingDone && c->isConnected()) {
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "enter PIN on kbd");
    needRedraw = true;
    uint32_t t0 = millis();
    while (!pairingDone && c->isConnected() &&
           (millis() - t0 < PASSKEY_DISPLAY_MS))
      vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!hid && pairingDone && pairingSuccess && c->isConnected())
    hid = c->getService(NimBLEUUID("1812"));
  if (!hid || !c->isConnected()) {
    if (c->isConnected()) c->disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    NimBLEDevice::deleteClient(c);
    passkeyActive = false;
    bleState = 0;
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "no HID svc");
    needRedraw = true;
    return false;
  }
  auto chars = hid->getCharacteristics(true);
  int subs = 0;
  for (auto &ch : chars)
    if (ch->getUUID() == NimBLEUUID("2A4D") && ch->canNotify())
      if (ch->subscribe(true, bleReportCb)) subs++;
  knownKbdAddr = bleAddr;
  knownKbdValid = true;
  bleKbdConnected = true;
  bleState = 4;
  passkeyActive = false;
  usbResetRequest = true;
  kbdBatteryPct = readBleBattery(c);
  needRedraw = true;
  {
    uint32_t lastBatMs = millis();
    while (c->isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      if (idleLevel() <= 2 && (millis() - lastBatMs >= 600000)) {
        int bat = readBleBattery(c);
        if (bat >= 0 && bat != kbdBatteryPct) {
          kbdBatteryPct = bat;
          needRedraw = true;
        }
        lastBatMs = millis();
      }
    }
  }
  kbdBatteryPct = -1;
  bleKbdConnected = false;
  bleState = 5;
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "disconnected");
  needRedraw = true;
  NimBLEDevice::deleteClient(c);
  bleFound = false;
  return true;
}

void bleTask(void *) {
  bool nimbleOff = false;
  NimBLEDevice::init("CoreS3-KVM");
  setKbdSecMode();
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCbInstance, true);
  scan->setActiveScan(true);
  scan->setInterval(200);
  scan->setWindow(25);  // 15.6ms out of 125ms = 12.5% radio duty — less mouse lag during scan

#if USE_MAX_MODULE
  // Brief wait for MAX3421E dongle init (non-blocking, just a few seconds)
  {
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "wait BT dongle");
    needRedraw = true;
    uint32_t wt = millis();
    while (!maxDongleReady && (millis() - wt < 5000))
      vTaskDelay(pdMS_TO_TICKS(200));
  }
#endif

  while (true) {
    // ── 1. Classic BT currently connected → stay until disconnect ──
    if (classicBtConnected() && !bleKbdConnected) {
      classicKbdKnown = true;
      appleFnDown = false;
      bleKbdConnected = true;
      kbdBatteryPct = -1;  // will be filled by requestBattery() in max3421Poll
      snprintf(bleFoundName, sizeof(bleFoundName), "Classic HID");
      if (classicBtName()[0])
        strncpy(bleFoundName, classicBtName(), sizeof(bleFoundName) - 1);
      bleState = 4;
      needRedraw = true;
      // Classic BT goes through MAX3421E (SPI), not ESP32 radio.
      // Deinit NimBLE to fully free the radio for ESP-NOW mouse data.
      if (!nimbleOff) {
        NimBLEDevice::deinit(true);
        nimbleOff = true;
      }
      while (classicBtConnected()) vTaskDelay(pdMS_TO_TICKS(500));
      kbdBatteryPct = -1;
      bleKbdConnected = false;
      appleFnDown = false;
      bleState = 5;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "classic disc");
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // ── 2. Classic known → wait for library to reconnect (forever) ──
    if (classicKbdKnown && !bleKbdConnected) {
      bleState = 5;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "BT reconnect...");
      needRedraw = true;
      while (!classicBtConnected()) vTaskDelay(pdMS_TO_TICKS(500));
      continue;  // reconnected → #1 handles it
    }

    // ── 3. BLE keyboard known → reconnect forever ──
    if (knownKbdValid && !bleKbdConnected) {
      if (classicBtConnected()) continue;  // classic connected meanwhile → #1
      bleFound = true;
      bleAddr = knownKbdAddr;
      if (bleFoundName[0] == '\0')
        strncpy(bleFoundName, "(reconnecting)", sizeof(bleFoundName) - 1);
      bool wasConn = bleDoConnect();
      if (wasConn) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      // Failed — wait and retry, never give up
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // ── 4. Mouse-only (scanned, nothing found yet) ──
    if (probesDone) {
      bleState = 7;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "mouse-only");
      needRedraw = true;
      while (!knownKbdValid && !classicBtConnected()) vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    bleState = 1;
    bleMatchReason = 0;
    bleFound = false;
    bleFoundName[0] = '\0';
    bleConnAddr[0] = '\0';
    resetSeenDevs();

    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "scanning...");
    needRedraw = true;

    uint32_t budgetStart = millis();
    while (!bleFound && !classicBtConnected() && (millis() - budgetStart < BLE_SCAN_BUDGET_MS)) {
      bleScanDone = false;
      if (!scan->start(0, false)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      uint32_t burstEnd = millis() + BLE_SCAN_BURST_MS;
      while (!bleScanDone && !bleFound && !classicBtConnected() && millis() < burstEnd &&
             (millis() - budgetStart < BLE_SCAN_BUDGET_MS))
        vTaskDelay(pdMS_TO_TICKS(50));
      scan->stop();
      if (bleFound || classicBtConnected()) break;
      vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_GAP_MS));
    }
    scan->stop();

    // Classic connected during scan — go handle it at top of loop
    if (classicBtConnected()) continue;

    if (bleFound && !classicBtConnected()) {
      setKbdSecMode();
      bool ok = bleDoConnect();
      if (!ok && !knownKbdValid) {
        bleFound = false;
        probesDone = true;
      }
      if (!knownKbdValid) vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    if (classicBtConnected()) continue;

    // ── Unified probe list: classic + BLE sorted by RSSI ──
    int sorted[SEEN_DEV_MAX], sortedN = getTopByRssi(sorted, SEEN_DEV_MAX);
    int probeList[BLE_PROBE_MAX], probeCnt = 0;
    for (int s = 0; s < sortedN && probeCnt < BLE_PROBE_MAX; s++) {
      int i = sorted[s];
      if (seenDevs[i].rssi < BLE_PROBE_MIN_RSSI) continue;
      if (!seenDevs[i].isClassic) {
        if (seenDevs[i].name[0]) continue;       // BLE: skip named (already checked in scan)
      }
      probeList[probeCnt++] = i;
      seenDevs[i].probeTarget = true;
    }

    if (probeCnt > 0 && !bleFound && !classicBtConnected()) {
      strncpy(rtcDbg, "probe-start", sizeof(rtcDbg));
      if (!pc) {
        pc = NimBLEDevice::createClient();
        if (pc) pc->setClientCallbacks(&secCb);
      }
      for (int p = 0; p < probeCnt && !bleFound && !classicBtConnected(); p++) {
        int si = probeList[p];
        if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) continue;
        seen_dev_t &d = seenDevs[si];

        if (d.isClassic) {
          // Classic devices handled by BTHID library automatically
          continue;
        } else {
          // ── BLE probe ──
          if (!pc) continue;
          bool apple = d.isApple;
          if (apple) {
            strncpy(rtcDbg, "apple-probe", sizeof(rtcDbg));
            setKbdSecMode();
            pc->setConnectionParams(6, 12, 0, 600);
            doAppleProbe(pc, si, p + 1, probeCnt);
          } else {
            strncpy(rtcDbg, "ble-probe", sizeof(rtcDbg));
            setProbeSecMode();
            pc->setConnectionParams(12, 24, 0, 200);
            doProbe(pc, si, p + 1, probeCnt);
          }
          if (pc->isConnected()) waitDisconn(pc, 3000);
          vTaskDelay(pdMS_TO_TICKS(apple ? 1000 : 600));
        }
      }
      if (pc && pc->isConnected()) waitDisconn(pc, 3000);
      strncpy(rtcDbg, "probe-cleanup", sizeof(rtcDbg));
      resetPairState();
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(300));
    }

    strncpy(rtcDbg, "pre-setKbdSec", sizeof(rtcDbg));
    vTaskDelay(pdMS_TO_TICKS(200));
    setKbdSecMode();
    strncpy(rtcDbg, "post-setKbdSec", sizeof(rtcDbg));

    if (classicBtConnected()) continue;

    if (bleFound) {
      strncpy(rtcDbg, "final-connect", sizeof(rtcDbg));
      bool ok = bleDoConnect();
      if (!ok && !knownKbdValid) {
        bleFound = false;
        probesDone = true;
      }
      if (!knownKbdValid) vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    strncpy(rtcDbg, "pre-mouseOnly", sizeof(rtcDbg));
    probesDone = true;
    usbResetRequest = true;
  }
}

// ===================== DISPLAY =====================
static const char *matchStr(int r) {
  switch (r) {
    case 1:
      return "name";
    case 2:
      return "HID";
    case 3:
      return "pName";
    case 4:
      return "pHID";
    case 5:
      return "Apple";
    default:
      return "";
  }
}
// ── UI color palette ──
#define COL_HDR     0x10A2  // dark header
#define COL_CARD    0x18E3  // dark card
#define COL_OK_BG   0x0280  // dark green card
#define COL_WARN_BG 0x4200  // dark yellow card
#define COL_ERR_BG  0x3000  // dark red card
#define COL_SEP     0x2104  // separator line
#define COL_LABEL   0xAD55  // muted label
#define COL_DIM     0x4208  // very dim
#define COL_ORANGE  0xFB20  // orange-red

static void drawPasskey() {
  auto &d = M5.Display;
  d.startWrite();
  d.fillScreen(0x0841);  // very dark blue

  // Title
  d.setTextSize(2);
  d.setTextColor(WHITE);
  d.setCursor(104, 12);
  d.print("PAIRING");

  // Device name centered
  if (bleFoundName[0]) {
    d.setTextSize(1);
    d.setTextColor(CYAN);
    int nx = (320 - (int)strlen(bleFoundName) * 6) / 2;
    if (nx < 4) nx = 4;
    d.setCursor(nx, 38);
    d.print(bleFoundName);
  }

  // PIN card
  d.fillRoundRect(40, 56, 240, 72, 8, 0x18C3);
  d.drawRoundRect(40, 56, 240, 72, 8, 0x4A69);

  char pin[12];
  if (passkeyValue == 0) snprintf(pin, sizeof(pin), "0000");
  else snprintf(pin, sizeof(pin), "%06d", (int)passkeyValue);

  d.setTextSize(4);
  d.setTextColor(GREEN);
  int px = (320 - (int)strlen(pin) * 24) / 2;
  d.setCursor(px, 72);
  d.print(pin);

  // Instructions
  d.setTextSize(1);
  d.setTextColor(YELLOW);
  d.setCursor(55, 144);
  d.print("Type this PIN on the keyboard");
  d.setCursor(100, 158);
  d.print("then press Enter");

  // Status
  bool pd = pairingDone, ps = pairingSuccess;
  if (pd) {
    d.setTextSize(2);
    d.setTextColor(ps ? GREEN : RED);
    const char *s = ps ? "Paired OK!" : "FAILED";
    d.setCursor((320 - (int)strlen(s) * 12) / 2, 184);
    d.print(s);
  } else {
    d.setTextSize(1);
    d.setTextColor(COL_LABEL);
    char wb[24];
    snprintf(wb, sizeof(wb), "Waiting... %ds", (millis() - passkeyShownMs) / 1000);
    d.setCursor((320 - (int)strlen(wb) * 6) / 2, 190);
    d.print(wb);
  }

  // Bottom status
  d.setTextSize(1);
  d.setCursor(8, 226);
  d.setTextColor(0x4A69);
  if (maxStatusMsg[0]) d.printf("BT: %s", maxStatusMsg);

  d.endWrite();
}

struct scan_disp_entry_t {
  char mac[12];
  char name[18];
  char status[16];
  int rssi;
  uint8_t flags;
};

#define BAT_SAMPLES 8
#define BAT_SAMPLE_MS 30000 

static float batVSamples[BAT_SAMPLES] = {0};
static int   batPctSamples[BAT_SAMPLES] = {0};
static int   batIdx = 0, batCnt = 0;
static uint32_t batCheckLast = 0;
static bool extPowerDetected = false;
static void updateBatTrend() {
    if (millis() - batCheckLast < BAT_SAMPLE_MS) return;
    batCheckLast = millis();

    float v = M5.Power.getBatteryVoltage();
    int pct = M5.Power.getBatteryLevel();

    batVSamples[batIdx % BAT_SAMPLES] = v;
    batPctSamples[batIdx % BAT_SAMPLES] = pct;
    batIdx++;
    if (batCnt < BAT_SAMPLES) batCnt++;

    bool was = extPowerDetected;

    if (batCnt >= 4) {
        int   oldPct = batPctSamples[(batIdx - batCnt) % BAT_SAMPLES];
        float oldV   = batVSamples[(batIdx - batCnt) % BAT_SAMPLES];

        if (pct > oldPct)            extPowerDetected = true;
        else if (v > oldV + 3.0f)    extPowerDetected = true;
        else if (pct < oldPct - 1)   extPowerDetected = false;
        else if (v < oldV - 15.0f)   extPowerDetected = false;
        // hysteresis
    }

    if (extPowerDetected != was) needRedraw = true;
    if (batCnt < 4) needRedraw = true;
}

static void drawUI() {
  if (screenTurnedOff) return;
  if (passkeyActive) { drawPasskey(); return; }

  int cBS = bleState, cMR = bleMatchReason;
  bool cUR = usbMouseReady;
  bool classicConn = (bool)mxClassicConnected;
  bool kbdConn = (bool)bleKbdConnected;
  int batPct = M5.Power.getBatteryLevel();
  float batV = M5.Power.getBatteryVoltage() / 1000.0f;

  auto &d = M5.Display;
  d.startWrite();
  d.fillScreen(BLACK);

  // ═══ HEADER BAR (0-27) ═══
  d.fillRect(0, 0, 320, 27, COL_HDR);

  // PC target
  d.setTextSize(2);
  d.setCursor(8, 6);
  d.setTextColor(WHITE);
  d.print("PC ");
  d.setTextColor(activeTarget == 0 ? GREEN : CYAN);
  d.printf("%d", activeTarget + 1);

  // Mouse indicator
  uint16_t mC = cUR ? GREEN : COL_DIM;
  d.fillCircle(120, 13, 4, mC);
  d.setTextSize(1);
  d.setCursor(128, 10);
  d.setTextColor(mC);
  d.print("Mouse");

  // Keyboard indicator
  uint16_t kC = kbdConn ? GREEN : (cBS >= 1 && cBS <= 6 ? YELLOW : COL_DIM);
  d.fillCircle(180, 13, 4, kC);
  d.setCursor(188, 10);
  d.setTextColor(kC);
  d.print("Kbd");

  // Battery (right edge)
  uint16_t bC = batPct > 20 ? GREEN : (batPct > 10 ? YELLOW : RED);
  // Battery icon: small rectangle outline + nub
  d.drawRect(272, 6, 22, 12, bC);
  d.fillRect(294, 9, 2, 6, bC);
  int fill = (batPct * 18) / 100;
  if (fill > 0) d.fillRect(274, 8, fill, 8, bC);
  d.setTextSize(1);
  d.setCursor(300, 10);
  d.setTextColor(bC);
  d.printf("%d", batPct);
  if (extPowerDetected && batCnt >= 4) {
    d.setCursor(272, 20);
    d.setTextColor(GREEN);
    d.print("CHG");
  }

  // ═══ STATUS LINE (29-39) ═══
  d.setTextSize(1);
#if USE_MAX_MODULE
  d.setCursor(8, 30);
  if (classicConn) {
    d.setTextColor(GREEN);
    d.print("BT: ");
    d.setTextColor(WHITE);
    if (Btd.remote_name[0])
      d.print((const char *)Btd.remote_name);
    else
      d.print("Classic HID");
  } else if (maxDongleReady && Btd.connectToHIDDevice) {
    d.setTextColor(YELLOW);
    d.print("BT: connecting");
    if (Btd.remote_name[0]) {
      d.setTextColor(WHITE);
      d.printf(" %s", (const char *)Btd.remote_name);
    }
  } else if (maxDongleReady) {
    d.setTextColor(CYAN);
    d.print("BT: inquiry...");
  } else if (maxStatusMsg[0]) {
    d.setTextColor(maxPresent ? YELLOW : COL_DIM);
    d.printf("BT: %s", maxStatusMsg);
  }
  // BLE sub-status (right side, only when not in classic connected mode)
  if (!classicConn) {
    d.setCursor(200, 30);
    if (kbdConn) {
      d.setTextColor(GREEN);
      d.print("BLE: connected");
    } else if (cBS == 1) {
      d.setTextColor(YELLOW);
      d.printf("BLE: scan %d", (int)seenDevCount);
    } else if (cBS == 6) {
      d.setTextColor(YELLOW);
      d.print("BLE: probe");
    } else if (cBS == 3) {
      d.setTextColor(YELLOW);
      d.print("BLE: connect..");
    }
  }
#endif

  // Separator
  d.drawFastHLine(8, 42, 304, COL_SEP);

  // ═══ MAIN AREA (44 - ~210) ═══
  int yM = 46;

  if (kbdConn) {
    // ── Connected card ──
    d.fillRoundRect(6, yM, 308, 52, 6, COL_OK_BG);
    d.drawRoundRect(6, yM, 308, 52, 6, GREEN);
    d.setTextSize(2);
    d.setCursor(14, yM + 6);
    d.setTextColor(GREEN);
    d.print("CONNECTED");
    // Keyboard battery (BLE only)
    int kBat = kbdBatteryPct;
    if (kBat >= 0) {
      uint16_t bc = kBat > 20 ? GREEN : (kBat > 10 ? YELLOW : RED);
      int bx = 252;
      d.drawRect(bx, yM + 8, 18, 10, bc);
      d.fillRect(bx + 18, yM + 11, 2, 4, bc);
      int bf = (kBat * 14) / 100;
      if (bf > 0) d.fillRect(bx + 2, yM + 10, bf, 6, bc);
      d.setTextSize(1);
      d.setCursor(bx + 24, yM + 9);
      d.setTextColor(bc);
      d.printf("%d%%", kBat);
    }
    d.setTextSize(1);
    d.setCursor(14, yM + 24);
    d.setTextColor(WHITE);
    d.print(bleFoundName[0] && bleFoundName[0] != '(' ? bleFoundName : "Keyboard");
    d.setCursor(14, yM + 36);
    d.setTextColor(COL_LABEL);
    d.print(classicConn ? "Classic Bluetooth" : "Bluetooth LE");
    if (bleConnAddr[0]) d.printf("  %s", bleConnAddr);
    yM += 58;

  } else if (cBS == 3) {
    // ── Connecting card ──
    d.fillRoundRect(6, yM, 308, 44, 6, COL_WARN_BG);
    d.drawRoundRect(6, yM, 308, 44, 6, YELLOW);
    d.setTextSize(2);
    d.setCursor(14, yM + 6);
    d.setTextColor(YELLOW);
    d.print("CONNECTING");
    d.setTextSize(1);
    d.setCursor(14, yM + 28);
    d.setTextColor(WHITE);
    if (bleFoundName[0]) d.print(bleFoundName);
    else if (bleConnAddr[0]) d.print(bleConnAddr);
    else d.print(bleStatusMsg);
    yM += 50;

  } else if (cBS == 2) {
    // ── Found card ──
    d.fillRoundRect(6, yM, 308, 36, 6, 0x0320);
    d.drawRoundRect(6, yM, 308, 36, 6, GREEN);
    d.setTextSize(1);
    d.setCursor(14, yM + 6);
    d.setTextColor(GREEN);
    d.printf("FOUND: %s (%s)", bleFoundName, matchStr(cMR));
    d.setCursor(14, yM + 20);
    d.setTextColor(COL_LABEL);
    d.print("connecting...");
    yM += 42;

  } else if (cBS == 5) {
    // ── Disconnected card ──
    d.fillRoundRect(6, yM, 308, 40, 6, COL_ERR_BG);
    d.drawRoundRect(6, yM, 308, 40, 6, COL_ORANGE);
    d.setTextSize(2);
    d.setCursor(14, yM + 5);
    d.setTextColor(COL_ORANGE);
    d.print("DISCONNECTED");
    d.setTextSize(1);
    d.setCursor(14, yM + 25);
    d.setTextColor(COL_LABEL);
    d.print(bleFoundName[0] ? bleFoundName : "reconnecting...");
    yM += 46;

  } else if (cBS == 7) {
    // ── Mouse-only card ──
    d.fillRoundRect(6, yM, 308, 26, 6, COL_CARD);
    d.setTextSize(1);
    d.setCursor(14, yM + 9);
    d.setTextColor(COL_LABEL);
    d.print("Mouse-only mode (no keyboard found)");
    yM += 32;

  } else if (cBS == 0 && bleStatusMsg[0]) {
    // ── Waiting / initial status ──
    d.setTextSize(1);
    d.setCursor(14, yM);
    d.setTextColor(COL_LABEL);
    d.print(bleStatusMsg);
    yM += 14;
  }

  // ── Probe status line ──
  if (cBS == 6 && bleStatusMsg[0]) {
    d.setTextSize(1);
    d.setCursor(8, yM);
    d.setTextColor(YELLOW);
    d.printf("Probing: %s", bleStatusMsg);
    yM += 12;
  }

  // ═══ DEVICE LIST ═══
  if (cBS == 1 || cBS == 6 || cBS == 7) {
    bool probeOnly = (cBS == 6 || cBS == 7);
    scan_disp_entry_t dd[TOP_DISPLAY];
    int ddN = 0, ddTotal = 0;
    xSemaphoreTake(seenMutex, portMAX_DELAY);
    {
      int all[SEEN_DEV_MAX], n = 0;
      for (int i = 0; i < seenDevCount && i < SEEN_DEV_MAX; i++)
        if (seenDevs[i].used && (!probeOnly || seenDevs[i].probeTarget))
          all[n++] = i;
      for (int a = 0; a < n - 1; a++)
        for (int b = a + 1; b < n; b++)
          if (seenDevs[all[b]].rssi > seenDevs[all[a]].rssi) {
            int t = all[a]; all[a] = all[b]; all[b] = t;
          }
      ddN = (n < TOP_DISPLAY) ? n : TOP_DISPLAY;
      for (int t = 0; t < ddN; t++) {
        int i = all[t];
        memcpy(dd[t].mac, seenDevs[i].mac_short, sizeof(dd[t].mac));
        if (seenDevs[i].name[0]) {
          strncpy(dd[t].name, seenDevs[i].name, sizeof(dd[t].name) - 1);
          dd[t].name[sizeof(dd[t].name) - 1] = '\0';
        } else dd[t].name[0] = '\0';
        strncpy(dd[t].status, seenDevs[i].status, sizeof(dd[t].status) - 1);
        dd[t].status[sizeof(dd[t].status) - 1] = '\0';
        dd[t].rssi = seenDevs[i].rssi;
        dd[t].flags = (seenDevs[i].name[0] ? 0x01 : 0) |
                      (seenDevs[i].hasHidUuid ? 0x02 : 0) |
                      (seenDevs[i].isApple ? 0x04 : 0) |
                      (seenDevs[i].isClassic ? 0x08 : 0);
      }
      ddTotal = n;
    }
    xSemaphoreGive(seenMutex);

    // Column header
    if (ddN > 0) {
      d.setTextSize(1);
      d.setCursor(8, yM);
      d.setTextColor(COL_DIM);
      d.print(" #  T  Device                     dB  Stat");
      yM += 11;
    }

    int yLimit = 206;
    for (int t = 0; t < ddN; t++) {
      int yP = yM + t * 11;
      if (yP > yLimit) break;
      d.setCursor(8, yP);

      // Color by status > type
      uint16_t lc;
      if (dd[t].status[0]) {
        if (strstr(dd[t].status, "HID!")) lc = GREEN;
        else if (strstr(dd[t].status, "noHID") || strstr(dd[t].status, "fail") ||
                 strstr(dd[t].status, "tmout") || strstr(dd[t].status, "pfail") ||
                 strstr(dd[t].status, "noConn") || strstr(dd[t].status, "rej"))
          lc = COL_ORANGE;
        else lc = YELLOW;
      } else if (dd[t].flags & 0x02) lc = GREEN;    // HID UUID
      else if (dd[t].flags & 0x04) lc = YELLOW;     // Apple
      else if (dd[t].flags & 0x08) lc = CYAN;       // Classic
      else if (dd[t].flags & 0x01) lc = WHITE;      // Named
      else lc = COL_DIM;
      d.setTextColor(lc);

      bool isC = dd[t].flags & 0x08, isA = dd[t].flags & 0x04;
      const char *tp = (isC && isA) ? "CA" : isA ? "BA" : isC ? "C " : "B ";

      char line[54];
      if (dd[t].name[0])
        snprintf(line, sizeof(line), "%2d  %s  %-23.23s %3d  %s",
                 t + 1, tp, dd[t].name, dd[t].rssi, dd[t].status);
      else
        snprintf(line, sizeof(line), "%2d  %s  (%-8.8s)%12s %3d  %s",
                 t + 1, tp, dd[t].mac, "", dd[t].rssi, dd[t].status);
      d.print(line);
    }

    if (!probeOnly && ddTotal > TOP_DISPLAY) {
      d.setCursor(8, yM + ddN * 11 + 2);
      d.setTextColor(COL_DIM);
      d.printf("+%d more", ddTotal - TOP_DISPLAY);
    }
  }

  // ═══ BOTTOM BAR ═══
  d.drawFastHLine(8, 216, 304, COL_SEP);

  d.setTextSize(1);
#if DEBUG_MODE
  float cpuTemp = 0;
  if (temp_handle) temperature_sensor_get_celsius(temp_handle, &cpuTemp);
  d.setCursor(8, 218);
  d.setTextColor(cpuTemp > 60 ? RED : CYAN);
  d.printf("T:%.0fC U:%d E:%d Q:%d", cpuTemp, (int)dbgUsbHz, (int)dbgEspHz, dbgQPeak);

#endif

  d.setCursor(8, 228);
  if (extPowerDetected && batCnt >= 4) {
    float oldV = batVSamples[(batIdx - batCnt) % BAT_SAMPLES];
    int oldPct = batPctSamples[(batIdx - batCnt) % BAT_SAMPLES];
    float windowMin = (batCnt - 1) * BAT_SAMPLE_MS / 60000.0f;
    float pctPerHr = (windowMin > 0.1f) ? (batPct - oldPct) * 60.0f / windowMin : 0;
    d.setTextColor(GREEN);
    if (pctPerHr > 0.3f) {
      int etaMin = (int)((100 - batPct) * 60.0f / pctPerHr);
      d.printf("Charging %d%%  %.2fV  +%.0f%%/h  ~%dh%02dm",
               batPct, batV, pctPerHr, etaMin / 60, etaMin % 60);
    } else {
      float mvPerMin = (windowMin > 0.1f) ? (batV * 1000.0f - oldV) / windowMin : 0;
      d.printf("Charging %d%%  %.2fV  +%.1fmV/m", batPct, batV, mvPerMin);
    }
  } else if (batCnt < 4) {
    d.setTextColor(YELLOW);
    int secLeft = (4 - batCnt) * BAT_SAMPLE_MS / 1000;
    d.printf("Power: detecting %ds...  %d%%", secLeft, batPct);
  } else {
    d.setTextColor(batPct > 20 ? COL_LABEL : RED);
    d.printf("Battery %d%%  %.2fV", batPct, batV);
  }

  // Idle level indicator (right side of bottom bar)
  {
    int il = idleLevel();
    d.setCursor(296, 228);
    d.setTextColor(il == 0 ? GREEN : (il <= 2 ? YELLOW : COL_ORANGE));
    d.printf("I%d", il);
  }

  d.endWrite();
}

// ===================== SETUP =====================
void setup() {
  // Hold MAX3421E in reset (CS LOW) while peripherals init
  pinMode(MX_CS, OUTPUT);
  digitalWrite(MX_CS, LOW);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.end();
  M5.Power.begin();
  M5.Power.setUsbOutput(true);

  Serial.begin(115200);
  // Wait for USB CDC serial to enumerate on host (up to 3s)
  for (int i = 0; i < 30 && !Serial; i++) delay(100);
  delay(200);
  Serial.println("\n\n=== KVM CoreS3 SE starting ===");

  M5.Display.setBrightness(60);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.println("KVM Init...");

  CRGB leds[10];
  FastLED.addLeds<WS2812, 5, GRB>(leds, 10);
  fill_solid(leds, 10, CRGB::Black);
  FastLED.show();

  {
    bool validDbg = false;
    if (rtcDbg[0] >= 0x20 && rtcDbg[0] < 0x7F) {
      rtcDbg[sizeof(rtcDbg) - 1] = '\0';
      validDbg = true;
      for (int i = 0; rtcDbg[i]; i++) {
        if (rtcDbg[i] < 0x20 || rtcDbg[i] >= 0x7F) { validDbg = false; break; }
      }
    }
    if (validDbg) {
      wasReboot = true;
      M5.Display.setTextSize(2);
      M5.Display.setCursor(10, 40);
      M5.Display.setTextColor(RED);
      M5.Display.print("LAST CRASH:");
      M5.Display.setTextSize(2);
      M5.Display.setCursor(10, 70);
      M5.Display.setTextColor(YELLOW);
      M5.Display.print(rtcDbg);
      M5.Display.setTextSize(1);
      M5.Display.setCursor(10, 100);
      M5.Display.setTextColor(WHITE);
      M5.Display.print("(waiting 8s...)");
      delay(8000);
    }
  }
  memset(rtcDbg, 0, sizeof(rtcDbg));
  strncpy(rtcDbg, "setup-ok", sizeof(rtcDbg));

  seenMutex = xSemaphoreCreateMutex();
  mouseQueue = xQueueCreate(128, sizeof(mouse_evt_t));
  kbdQueue = xQueueCreate(32, sizeof(kbd_evt_t));

#if DEBUG_MODE
  temperature_sensor_config_t temp_sensor_config =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
  temperature_sensor_install(&temp_sensor_config, &temp_handle);
  temperature_sensor_enable(temp_handle);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_now_init();
  esp_now_register_send_cb(onEspNowSendDone);
  memcpy(peer1.peer_addr, ATOM_MAC_1, 6);
  peer1.channel = ESPNOW_CHAN;
  peer1.encrypt = false;
  peer1.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer1);
  memcpy(peer2.peer_addr, ATOM_MAC_2, 6);
  peer2.channel = ESPNOW_CHAN;
  peer2.encrypt = false;
  peer2.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer2);
  // Broadcast peer for mouse data (no ACK wait)
  {
    esp_now_peer_info_t bp = {};
    memset(bp.peer_addr, 0xFF, 6);
    bp.channel = ESPNOW_CHAN;
    bp.encrypt = false;
    bp.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&bp);
  }
  // Increase ESP-NOW PHY rate from default 1Mbps to 6.5Mbps HT20.
  // At 1Mbps, air time alone limits to ~600 pps (with WiFi task overhead).
  // At MCS0 HT20, practical throughput is 2500+ pps — enough for 1000Hz mouse.
  {
    esp_now_rate_config_t rate_cfg = {};
    rate_cfg.phymode = WIFI_PHY_MODE_HT20;
    rate_cfg.rate = WIFI_PHY_RATE_MCS0_LGI;
    rate_cfg.ersu = false;
    rate_cfg.dcm = false;
    const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_set_peer_rate_config(bcast, &rate_cfg);
    esp_now_set_peer_rate_config(ATOM_MAC_1, &rate_cfg);
    esp_now_set_peer_rate_config(ATOM_MAC_2, &rate_cfg);
  }
  sendCmd(ATOM_MAC_1, PKT_ACTIVATE);
  sendCmd(ATOM_MAC_2, PKT_DEACTIVATE);

  xTaskCreatePinnedToCore(usbHostTask, "USB", 8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(mouseSendTask, "MOUSE", 4096, NULL, 9, NULL, 0);

#if USE_MAX_MODULE
  {
    // Release MAX3421E from reset (was held LOW since start of setup)
    digitalWrite(MX_CS, HIGH);
    delay(20);

    Btd.useSimplePairing = true;  // Enable SSP for Apple keyboards
    bthid.SetReportParser(KEYBOARD_PARSER_ID, &kvmKbdParser);
    if (Usb.Init() == -1) {
      strncpy(maxStatusMsg, "USB FAIL", sizeof(maxStatusMsg));
    } else {
      snprintf(maxStatusMsg, sizeof(maxStatusMsg), "USB ok");
    }
    needRedraw = true;
  }
#endif

#if WITH_KEYBOARD && !USE_MAX_MODULE
  xTaskCreatePinnedToCore(bleTask, "BLE", 64 * 1024, NULL, 1, NULL, 0);
#endif

  delay(300);
  lastActivityMs = millis();
#if DEBUG_MODE
  dbgLastTick = millis();
#endif

  batCheckLast = millis() - BAT_SAMPLE_MS;
  drawUI();
}

// ===================== LOOP =====================
static uint32_t lastBeat = 0;
static bool prevUsb = false, prevBle = false, prevPk = false;
static int prevBleS = -1;

void loop() {
  M5.update();
  max3421Poll();
#if DEBUG_MODE
  dbgTick();
  int qn = uxQueueMessagesWaiting(mouseQueue);
  if (qn > dbgQPeak) dbgQPeak = qn;
#endif

  if ((M5.BtnA.wasPressed() || switchRequest) &&
      millis() - lastSwitchMs > SWITCH_DEBOUNCE_MS) {
    lastActivityMs = millis();
    lastPCSwitchMs = lastActivityMs;
    switchRequest = false;
    switchTarget();
  }

  // Other touch buttons: just wake screen for 20s
  if (M5.BtnB.wasPressed() || M5.BtnC.wasPressed() || M5.BtnPWR.wasPressed()) {
    lastActivityMs = millis();
    lastScreenTouchMs = lastActivityMs;
  }

  kbd_evt_t k;
  while (xQueueReceive(kbdQueue, &k, 0) == pdTRUE) txKbd(&k);

  uint32_t hbInterval;
  int loopDelay;

  int idlelvl = idleLevel();
  switch (idlelvl) {
    case 4: hbInterval = IDLE4_HEARTBEAT_MS; loopDelay = IDLE4_LOOP_MS; break;
    case 3: hbInterval = IDLE3_HEARTBEAT_MS; loopDelay = IDLE3_LOOP_MS; break;
    case 2: hbInterval = IDLE2_HEARTBEAT_MS; loopDelay = IDLE2_LOOP_MS; break;
    case 1: hbInterval = IDLE1_HEARTBEAT_MS; loopDelay = IDLE1_LOOP_MS; break;
    default: hbInterval = HEARTBEAT_MS; loopDelay = 1; break;
  }

  if (millis() - lastBeat > hbInterval) {
    sendCmd(activeMac(), PKT_ACTIVATE);
    sendCmd(inactiveMac(), PKT_DEACTIVATE);
    lastBeat = millis();
  }

  bool cU = usbMouseReady, cB = bleKbdConnected, cP = passkeyActive;
  int cS = bleState;
  bool screenNeeded = false;

  // Track state changes to show screen briefly
  static int prevScreenState = -1;
  static uint32_t lastStateChangeMs = 0;
  if (cS != prevScreenState) {
    prevScreenState = cS;
    lastStateChangeMs = millis();
  }

  // BLE setup phases: scanning(1), found/probing(2), connecting(3), post-setup(6)
  if (cS >= 1 && cS <= 3) screenNeeded = true;
  if (cS == 5 || cS == 6) screenNeeded = true;  // disconnected / post-setup

  // Show screen 5s after any state change (e.g. connected, mouse-only)
  if (millis() - lastStateChangeMs < 5000) screenNeeded = true;

  // Passkey active
  if (cP) screenNeeded = true;

  // 10s after PC switch
  if (millis() - lastPCSwitchMs < 10000) screenNeeded = true;

  // 20s after touch button press
  if (millis() - lastScreenTouchMs < 20000) screenNeeded = true;

  if (screenNeeded && screenTurnedOff) {
      M5.Display.wakeup();
      M5.Display.setBrightness(60);
      screenTurnedOff = false;
      needRedraw = true;
  } else if (!screenNeeded && !screenTurnedOff) {
      M5.Display.setBrightness(0);
      M5.Display.sleep();
      screenTurnedOff = true;
  }

  if (idlelvl != lastIdleLevel) {
      int prevIdle = lastIdleLevel;
      lastIdleLevel = idlelvl;
      if (idlelvl >= 3) {
        wifiNeedsBackToNormal = true;
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        esp_wifi_set_max_tx_power(40);
      } else if (wifiNeedsBackToNormal) {
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_max_tx_power(80);
        wifiNeedsBackToNormal = false;
        // Waking from deep idle — burst-send PKT_ACTIVATE so target recovers fast
        if (prevIdle >= 3) {
          delay(5);  // let WiFi stabilize
          for (int i = 0; i < 3; i++) {
            sendCmd(activeMac(), PKT_ACTIVATE);
            delay(2);
          }
          lastBeat = millis();
        }
      }
  }

  updateBatTrend();
  bool force = false;
  if (cP) {
    static uint32_t lp = 0;
    if (millis() - lp > 1000) {
      force = true;
      lp = millis();
    }
  }
  if (force || needRedraw || cU != prevUsb || cB != prevBle || cS != prevBleS ||
      cP != prevPk) {
    prevUsb = cU;
    prevBle = cB;
    prevBleS = cS;
    prevPk = cP;
    needRedraw = false;
    drawUI();
  }

  delay(loopDelay);
}