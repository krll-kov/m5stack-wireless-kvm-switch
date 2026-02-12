# 🖱️⌨️ M5Stack Wireless KVM Switch

> Wireless keyboard & mouse KVM switch (**both IN and OUT**) based on M5Stack CoreS3 SE and Atom S3U — capable of handling a **1000 Hz wireless mouse** and **Bluetooth keyboard**.

Switch between two PCs with a single mouse button press. No cables for peripherals, no lag.

---

## 📦 Hardware

| Component | Qty |
|---|:-:|
| [M5Stack CoreS3 SE IoT Controller](https://shop.m5stack.com/products/m5stack-cores3-se-iot-controller-w-o-battery-bottom) (without Battery Bottom) | 1 |
| [AtomS3U](https://shop.m5stack.com/products/atoms3u) ESP32S3 Dev Kit with USB-A | 2 |
| [M5GO Battery Bottom3](https://shop.m5stack.com/products/m5go-battery-bottom3-for-cores3-only) (for CoreS3 only) | 1 |
| [M5GO / FIRE Battery Bottom](https://shop.m5stack.com/products/battery-bottom-charging-base) Charging Base | 1 |
| [M5Stack USB Module V1.2 — for M5Core](https://shop.m5stack.com/products/usb-module-with-max3421e-v1-2?variant=44512358793473) *(For legacy BT only)* | 1 |
| [Bluetooth 4.0 USB Module](https://botland.store/usb-bluetooth-adapters/3561-bluetooth-40-to-usb-module-6959420908691.html) *(For legacy BT only)* | 1 |
| M3×25 DIN 912 A2 screws | 2 |
| M3×22 DIN 912 A2 screws | 2 |

### Tested Peripherals

| Device | Role |
|---|---|
| Apple Magic Keyboard (wireless, USB-C, 2021) | Bluetooth keyboard |
| Keychron M3 Mini (wireless USB-C dongle) | 1000 Hz wireless mouse |

---

## 🛠️ Setup Guide

> This guide uses **macOS**. For other operating systems, download the equivalent files for your platform.

### 1 — Install Arduino IDE

Download from [arduino.cc/en/software](https://www.arduino.cc/en/software).

### 2 — Install USB Driver

Plug the first **Atom S3U** into your Mac, then install the USB driver from  
👉 [docs.m5stack.com/en/download](https://docs.m5stack.com/en/download)

I used the `.pkg` installer — no reboot needed, just follow the prompts.

<img width="1227" alt="USB driver download page" src="https://github.com/user-attachments/assets/c5f534b6-a41b-49d2-b936-1a9b752347ee" />

### 3 — Verify the Port

Open Arduino IDE → **Tools → Ports**.  
Confirm `/dev/cu.usbmodem…` appears (if not — driver isn't installed).

<img width="805" alt="Arduino port selection" src="https://github.com/user-attachments/assets/e313367a-efe7-4d20-92ee-d5eebb17751c" />

### 4 — Add M5Stack Board URL

Go to **Settings → Additional boards manager URLs** and paste:

```
https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
```

<img width="886" alt="Board manager URL" src="https://github.com/user-attachments/assets/2bed3b3b-b131-4c99-9587-f28b29754c4f" />

### 5 — Install Board Packages

**Tools → Board → Boards Manager** — search and install:

- **M5Stack** by M5Stack  
- **esp32** by Espressif Systems

<img width="347" alt="M5Stack board package" src="https://github.com/user-attachments/assets/64eaae41-0942-4eb1-9433-b4c6b0c3464f" />
<img width="416" alt="ESP32 board package" src="https://github.com/user-attachments/assets/f2fe4450-c7dd-468c-9c8c-9e0df3674858" />

### 6 — Select Board

**Tools → Board → M5Stack → M5AtomS3**

<img width="972" alt="Board selection" src="https://github.com/user-attachments/assets/4be5d35f-2a74-4b5a-885a-4ad44b395c0a" />

### 7 — Install Libraries

**Tools → Manage Libraries** — install:

- `M5Unified`  
- `NimBLE-Arduino` (by h2zero)
- `FastLED` (by Daniel Garcia)

<img width="828" alt="Library manager" src="https://github.com/user-attachments/assets/e6a6d9ba-8ea7-4ac3-9e12-511c020b295e" />

---

## 🔍 Step 1 — Get MAC Addresses of Both Atom S3U

Paste this sketch and upload it to the **first** Atom S3U:

```ino
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  WiFi.mode(WIFI_STA);
  
  Serial.println("\n===================");
  Serial.println("MAC Address:");
  Serial.println(WiFi.macAddress());
  Serial.println("===================");
}

void loop() {
  delay(5000);
  Serial.println(WiFi.macAddress());
}
```

Upload via **Sketch → Upload**, then open **Tools → Serial Monitor** and copy the MAC address (e.g. `3C:DC:75:ED:FB:4C`). **Save it.**

<img width="1513" alt="Upload sketch" src="https://github.com/user-attachments/assets/d8a80515-bc94-4b97-935c-26dea4b829ad" />
<img width="948" alt="Serial monitor MAC address" src="https://github.com/user-attachments/assets/e59a0336-30f1-411f-8348-48dc098f92f5" />

Now **swap to the second Atom S3U** and repeat. Save both MAC addresses.

---

## ⚡ Step 2 — Flash the Atom S3U Receiver Firmware

> ⚠️ **Important:** Before uploading, change **USB Mode** AND **USB UPLOAD MODE** to **USB-OTG (TinyUSB)**. (BOTH!)

<img width="848" alt="USB-OTG mode" src="https://github.com/user-attachments/assets/f418d42b-d1da-4661-996c-152d55c2fdb7" />

Upload the following sketch to **both** Atom S3U devices:

<details>
<summary>📄 <b>Atom S3U Receiver Sketch</b> — click to expand</summary>

```ino
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "freertos/queue.h"

// ── TinyUSB health-check functions (always linked when USB-OTG mode) ──
extern "C" {
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_remote_wakeup(void);
void tud_disconnect(void);
void tud_connect(void);
}

#define ESPNOW_CHAN 1

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT 0x05

// ── Watchdog thresholds ──
#define BOOT_GRACE_MS 5000      // USB enumeration grace period after boot
#define ESPNOW_TIMEOUT_MS 3400  // no packets from CoreS3 → esp_now_deinit

static uint32_t usbGoneSinceMs = 0;
#define USB_GONE_REPLUG_MS 30000

#define USB_IDLE_TIMEOUT_MS_2 60000
#define USB_IDLE_DELAY_MS_2   250
#define USB_IDLE_TIMEOUT_MS_1 10000
#define USB_IDLE_DELAY_MS_1  1

USBHIDKeyboard UsbKbd;
USBHIDMouse UsbMouse;

#pragma pack(push, 1)
struct MousePkt {
  uint8_t btn;
  int16_t dx, dy;
  int8_t whl;
};
struct KbdPkt {
  uint8_t mod;
  uint8_t keys[6];
};
struct HIDMouseReport {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
  int8_t pan;
};
#pragma pack(pop)

QueueHandle_t mouseQ;
QueueHandle_t kbdQ;

volatile bool deviceActive = false;
volatile bool pendingActivate = false;
volatile bool pendingDeactivate = false;
static uint8_t prevBtn = 0;

// ── Health state ──
static uint32_t bootMs = 0;
volatile uint32_t lastPacketMs = 0;

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 1) return;
  lastPacketMs = millis();

  switch (data[0]) {
    case PKT_ACTIVATE:
      pendingActivate = true;
      break;
    case PKT_DEACTIVATE:
      pendingDeactivate = true;
      break;
    case PKT_MOUSE:
      if (len >= 7) {
        MousePkt p;
        p.btn = data[1];
        p.dx = (int16_t)(data[2] | (data[3] << 8));
        p.dy = (int16_t)(data[4] | (data[5] << 8));
        p.whl = (int8_t)data[6];
        xQueueSend(mouseQ, &p, 0);
      }
      break;
    case PKT_KEYBOARD:
      if (len >= 8) {
        KbdPkt k;
        k.mod = data[1];
        memcpy(k.keys, &data[2], 6);
        xQueueSend(kbdQ, &k, 0);
      }
      break;
    case PKT_HEARTBEAT:
      break;
  }
}

static inline bool ensureUsbAwake() {
    if (tud_suspended()) {
        tud_remote_wakeup();
        vTaskDelay(pdMS_TO_TICKS(20));
        return tud_mounted() && !tud_suspended();
    }
    return tud_mounted();
}

static uint32_t lastInputMs = 0;

void usbTask(void *pvParameters) {
  MousePkt p;
  KbdPkt k;
  HIDMouseReport rpt;

  while (true) {
    if (pendingDeactivate) {
      pendingDeactivate = false;
      if (deviceActive) {
        deviceActive = false;
        UsbKbd.releaseAll();
        UsbMouse.release(MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
        prevBtn = 0;
        xQueueReset(mouseQ);
        xQueueReset(kbdQ);
      }
    }
    if (pendingActivate) {
      pendingActivate = false;
      if (!deviceActive) {
        UsbKbd.releaseAll();
        UsbMouse.release(MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
        prevBtn = 0;
        deviceActive = true;
        lastInputMs = millis();
      }
    }

    bool hasActivity = false;
    while (deviceActive && xQueueReceive(mouseQ, &p, 0) == pdTRUE) {
      hasActivity = true;
      if (!ensureUsbAwake()) break;
    
      int16_t rx = p.dx;
      int16_t ry = p.dy;
      int8_t rw = p.whl;

      do {
        int8_t sx = (rx > 127) ? 127 : (rx < -127) ? -127 : (int8_t)rx;
        int8_t sy = (ry > 127) ? 127 : (ry < -127) ? -127 : (int8_t)ry;

        rpt.buttons = p.btn;
        rpt.x = sx;
        rpt.y = sy;
        rpt.wheel = rw;
        rpt.pan = 0;
        UsbMouse.sendReport(rpt);

        rx -= sx;
        ry -= sy;
        rw = 0;

      } while (rx != 0 || ry != 0);

      prevBtn = p.btn;
    }

    while (deviceActive && xQueueReceive(kbdQ, &k, 0) == pdTRUE) {
      hasActivity = true;
      if (!ensureUsbAwake()) break;

      KeyReport rpt;
      rpt.modifiers = k.mod;
      rpt.reserved = 0;
      memcpy(rpt.keys, k.keys, 6);
      UsbKbd.sendReport(&rpt);
    }
    if (hasActivity) lastInputMs = millis();

    if (!deviceActive) {
      vTaskDelay(100);
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
    } else {
      uint32_t millisNow = millis() - lastInputMs;
      if (millisNow > USB_IDLE_TIMEOUT_MS_2) {
          vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_2));
      } else if (millisNow > USB_IDLE_TIMEOUT_MS_1) {
          vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_1));  
      } else {
        taskYIELD();
      }
    }
  }
}

void setup() {
  bootMs = millis();

  UsbMouse.begin();
  UsbKbd.begin();
  USB.usbAttributes(0xA0);
  USB.begin();

  mouseQ = xQueueCreate(128, sizeof(MousePkt));
  kbdQ = xQueueCreate(32, sizeof(KbdPkt));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onRecv);

  lastPacketMs = millis();
  xTaskCreatePinnedToCore(usbTask, "USB", 4096, NULL, 10, NULL, 1);
}
void loop() {
  uint32_t now = millis();
  bool grace = (now - bootMs) < BOOT_GRACE_MS;

  bool mounted = tud_mounted();
  bool suspended = tud_suspended();

  // ── USB host gone (port powered off) ──
  if (!mounted && !suspended && !grace) {
    if (deviceActive) {
      deviceActive = false;
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
    }
    lastPacketMs = now;

    if (usbGoneSinceMs == 0) {
      usbGoneSinceMs = now;
    } else if (now - usbGoneSinceMs > USB_GONE_REPLUG_MS) {
      tud_disconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));
      tud_connect();
      usbGoneSinceMs = now;
    }

    delay(500);
    return;
  }

  // ── USB host suspended (Mac sleeping) ──
  if (suspended) {
    if (deviceActive) {
      deviceActive = false;
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
    }
    lastPacketMs = now;
    usbGoneSinceMs = 0;
    delay(500);
    return;
  }

  // ── Normal operation: mounted == true ──
  usbGoneSinceMs = 0;

  if (!grace && (now - lastPacketMs > ESPNOW_TIMEOUT_MS)) {
    deviceActive = false;
    xQueueReset(mouseQ);
    xQueueReset(kbdQ);

    esp_now_deinit();
    esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() == ESP_OK) {
      esp_now_register_recv_cb(onRecv);
    }
    lastPacketMs = now;
  }

  delay(100);
}
```

</details>

After upload you'll likely see this error — **that's expected and means it worked:**

> *"Port monitor error: command 'open' failed: no such file or directory."*

<img width="535" alt="Expected error" src="https://github.com/user-attachments/assets/78412161-a1da-4c09-9c98-0fd6c2aa9a8c" />
<img width="977" alt="Upload success" src="https://github.com/user-attachments/assets/aeb8694a-97b2-4cb4-babc-9a8a97992d04" />

> 💡 **Need to re-flash later?** Hold the reset button on the Atom S3U → insert into USB (keep holding) → wait 1 second → release. The port will reappear in Arduino.

---

## 🧠 Step 3 — Flash the CoreS3 SE (Main Controller)

Connect the **CoreS3 SE** to your Mac and select **Tools → Board → M5Stack → M5CoreS3**.

<img width="909" alt="CoreS3 board selection" src="https://github.com/user-attachments/assets/31de6870-00b4-46dc-8fb6-66f62e7f2c45" />

### ⚙️ Configuration

Before uploading, **update these values** in the sketch:

| Variable | Description | Example |
|---|---|---|
| `ATOM_MAC_1[]` | MAC of first Atom S3U | `{0x3C, 0xDC, 0x75, 0xED, 0xFB, 0x4C}` |
| `ATOM_MAC_2[]` | MAC of second Atom S3U | `{0xD0, 0xCF, 0x13, 0x0F, 0x90, 0x48}` |
| `BLE_KBD_MATCH` | Part of your keyboard's BT name (no special chars) | `"Keyboard"` |
| `MOUSE_SEND_HZ` | Set higher than actual mouse input rate to get real number, for example if mouse is 1000, set this to 2100 and you'll get real 1000 (you can verify number with debug mode), it's just how delay works in programming | `2100` |
| `USE_MAX_MODULE` | Set `true` if using the classic USB module | `false` |
| `WITH_KEYBOARD` | Set `false` for mouse-only mode | `true` |
| `BLE_PROBE_MIN_RSSI` | Raise to `-80` if keyboard is far away | `-55` |
| `DEBUG_MODE` | Set to true to see debug mode stats, usefull for MOUSE_SEND_HZ measuring | `false` |

> 💡 The switch button is bound to **Mouse4** (`m.buttons & 0x08`) by default.

<details>
<summary>📄 <b>CoreS3 SE Main Controller Sketch</b> — click to expand</summary>

```ino
#include <Arduino.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <FastLED.h>
#include <SPI.h>
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
#define USE_MAX_MODULE false

// Set true to show poll-rate / queue / send-rate overlay
#define DEBUG_MODE false

uint8_t ATOM_MAC_1[] = {0x3C, 0xDC, 0x75, 0xED, 0xFB, 0x4C};
uint8_t ATOM_MAC_2[] = {0xD0, 0xCF, 0x13, 0x0F, 0x90, 0x48};

#define BLE_KBD_MATCH "Keyboard"
#define ESPNOW_CHAN 1
// Real send speed will be 1000 with this number, it just has to be higher than
// actual mouse hz input
#define MOUSE_SEND_HZ 2100
#define MOUSE_SEND_US (1000000 / MOUSE_SEND_HZ)
#define SWITCH_DEBOUNCE_MS 300
#define HEARTBEAT_MS 500
#define BLE_SCAN_BUDGET_MS 20000
#define SEEN_DEV_MAX 48
#define SEEN_NAME_LEN 40
#define BLE_PROBE_MAX 8
#define BLE_PROBE_MIN_RSSI -50
#define SCAN_COL_ROWS 12
#define TOP_DISPLAY (SCAN_COL_ROWS * 2)
#define PASSKEY_DISPLAY_MS 30000
#define APPLE_PAIR_TIMEOUT_MS 20000
#define MAX_CONNECT_RETRIES 5
#define SCAN_REDRAW_MS 1000
#define BLE_SCAN_BURST_MS 3000
#define BLE_SCAN_GAP_MS 200

#if USE_MAX_MODULE
#define MAX_CS_PIN 6
#define MAX_INT_PIN 10
#define MAX_MOSI_PIN 37
#define MAX_MISO_PIN 35
#define MAX_SCK_PIN 36
#endif

#if USE_MAX_MODULE
#define rRCVFIFO 0x08
#define rSNDFIFO 0x10
#define rSUDFIFO 0x20
#define rRCVBC 0x30
#define rSNDBC 0x38
#define rUSBIRQ 0x68
#define rUSBCTL 0x78
#define rPINCTL 0x88
#define rREVISION 0x90
#define rHIRQ 0xC8
#define rHIEN 0xD0
#define rMODE 0xD8
#define rPERADDR 0xE0
#define rHCTL 0xE8
#define rHXFR 0xF0
#define rHRSL 0xF8
#endif

#define tokSETUP 0x10
#define tokIN 0x00
#define tokOUT 0x20
#define tokINHS 0x80
#define tokOUTHS 0xA0
#define hrSUCCESS 0x00
#define hrNAK 0x04

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04

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
#if USE_MAX_MODULE
static SemaphoreHandle_t spiMutex;
#endif
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
static volatile bool classicScanRequest = false;
static volatile bool classicScanDone = false;

RTC_NOINIT_ATTR char rtcDbg[48];
static bool wasReboot = false;
temperature_sensor_handle_t temp_handle = NULL;

static volatile uint32_t lastActivityMs = 0;
static volatile uint32_t lastPCSwitchMs = 0;
static volatile int lastIdleLevel = 0;
static volatile bool screenTurnedOff = false;
static volatile bool wifiNeedsBackToNormal = false;

#define IDLE1_TIMEOUT_MS 10000
#define IDLE2_TIMEOUT_MS 60000
#define IDLE3_TIMEOUT_MS 300000
#define IDLE4_TIMEOUT_MS 900000

#define IDLE1_MOUSE_SEND_HZ (MOUSE_SEND_HZ / 2)
#define IDLE2_MOUSE_SEND_HZ 100
#define IDLE3_MOUSE_SEND_HZ 30
#define IDLE4_MOUSE_SEND_HZ 2
#define IDLE1_MOUSE_SEND_US (1000000 / IDLE1_MOUSE_SEND_HZ)
#define IDLE2_MOUSE_SEND_US (1000000 / IDLE2_MOUSE_SEND_HZ)
#define IDLE3_MOUSE_SEND_US (1000000 / IDLE3_MOUSE_SEND_HZ)
#define IDLE4_MOUSE_SEND_US (1000000 / IDLE4_MOUSE_SEND_HZ)

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
  int rssi;
  bool used, hasHidUuid, isApple, isClassic;
};
static seen_dev_t seenDevs[SEEN_DEV_MAX];
static volatile int seenDevCount = 0, bleScanTotal = 0;

static char probeLog[BLE_PROBE_MAX * 2][52];
static volatile int probeLogCount = 0, probeTotalExpected = 0;

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
    if (rssi > seenDevs[idx].rssi) seenDevs[idx].rssi = rssi;
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
static void txMouse(const mouse_evt_t *m) {
  uint8_t p[7] = {PKT_MOUSE,
                  (uint8_t)(m->buttons & 0x07),
                  (uint8_t)(m->x & 0xFF),
                  (uint8_t)((m->x >> 8) & 0xFF),
                  (uint8_t)(m->y & 0xFF),
                  (uint8_t)((m->y >> 8) & 0xFF),
                  (uint8_t)m->wheel};
  esp_now_send(activeMac(), p, 7);
#if DEBUG_MODE
  dbgEspTx++;
#endif
}
static void txKbd(const kbd_evt_t *k) {
  uint8_t p[8] = {PKT_KEYBOARD, k->modifiers};
  memcpy(&p[2], k->keys, 6);
  esp_now_send(activeMac(), p, 8);
}

// ===================== MAX3421E DRIVER =====================
#if USE_MAX_MODULE

static SPIClass maxSPI(HSPI);
static const SPISettings maxSPISet(10000000, MSBFIRST, SPI_MODE0);

static uint8_t mxRd(uint8_t reg) {
  uint8_t v = 0;
  if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
    maxSPI.beginTransaction(maxSPISet);
    digitalWrite(MAX_CS_PIN, LOW);
    maxSPI.transfer(reg);
    v = maxSPI.transfer(0);
    digitalWrite(MAX_CS_PIN, HIGH);
    maxSPI.endTransaction();
    xSemaphoreGive(spiMutex);
  }
  return v;
}
static void mxWr(uint8_t reg, uint8_t val) {
  if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
    maxSPI.beginTransaction(maxSPISet);
    digitalWrite(MAX_CS_PIN, LOW);
    maxSPI.transfer(reg | 0x02);
    maxSPI.transfer(val);
    digitalWrite(MAX_CS_PIN, HIGH);
    maxSPI.endTransaction();
    xSemaphoreGive(spiMutex);
  }
}
static void mxWrN(uint8_t reg, const uint8_t *d, int n) {
  if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
    maxSPI.beginTransaction(maxSPISet);
    digitalWrite(MAX_CS_PIN, LOW);
    maxSPI.transfer(reg | 0x02);
    for (int i = 0; i < n; i++) maxSPI.transfer(d[i]);
    digitalWrite(MAX_CS_PIN, HIGH);
    maxSPI.endTransaction();
    xSemaphoreGive(spiMutex);
  }
}
static void mxRdN(uint8_t reg, uint8_t *d, int n) {
  if (xSemaphoreTake(spiMutex, portMAX_DELAY) == pdTRUE) {
    maxSPI.beginTransaction(maxSPISet);
    digitalWrite(MAX_CS_PIN, LOW);
    maxSPI.transfer(reg);
    for (int i = 0; i < n; i++) d[i] = maxSPI.transfer(0);
    digitalWrite(MAX_CS_PIN, HIGH);
    maxSPI.endTransaction();
    xSemaphoreGive(spiMutex);
  }
}

static bool mxWaitHXFR(int tmoMs = 500) {
  uint32_t t0 = millis();
  while (!(mxRd(rHIRQ) & 0x80)) {
    if ((int)(millis() - t0) > tmoMs) return false;
    vTaskDelay(1);
  }
  mxWr(rHIRQ, 0x80);
  return true;
}

static uint8_t mxXfer(uint8_t token, uint8_t ep, int nakLimit = 300) {
  for (int naks = 0; naks <= nakLimit; naks++) {
    mxWr(rHXFR, token | ep);
    if (!mxWaitHXFR()) return 0xFF;
    uint8_t r = mxRd(rHRSL) & 0x0F;
    if (r != hrNAK) return r;
    vTaskDelay(1);
  }
  return hrNAK;
}

static bool mxCtrlWrite(uint8_t addr, uint8_t bmRT, uint8_t bReq, uint16_t wVal,
                        uint16_t wIdx, uint16_t wLen, const uint8_t *data) {
  mxWr(rPERADDR, addr);
  uint8_t setup[8] = {bmRT,
                      bReq,
                      (uint8_t)(wVal & 0xFF),
                      (uint8_t)(wVal >> 8),
                      (uint8_t)(wIdx & 0xFF),
                      (uint8_t)(wIdx >> 8),
                      (uint8_t)(wLen & 0xFF),
                      (uint8_t)(wLen >> 8)};
  mxWrN(rSUDFIFO, setup, 8);
  if (mxXfer(tokSETUP, 0, 5) != hrSUCCESS) return false;
  if (wLen > 0 && data) {
    uint16_t sent = 0;
    uint8_t toggle = 1;
    while (sent < wLen) {
      uint16_t chunk = wLen - sent;
      if (chunk > 64) chunk = 64;
      mxWr(rHCTL, toggle ? 0x80 : 0x40);
      mxWrN(rSNDFIFO, data + sent, chunk);
      mxWr(rSNDBC, chunk);
      if (mxXfer(tokOUT, 0, 200) != hrSUCCESS) return false;
      sent += chunk;
      toggle ^= 1;
    }
  }
  return mxXfer(tokINHS, 0, 200) == hrSUCCESS;
}

static int mxCtrlRead(uint8_t addr, uint8_t bmRT, uint8_t bReq, uint16_t wVal,
                      uint16_t wIdx, uint16_t wLen, uint8_t *buf) {
  mxWr(rPERADDR, addr);
  uint8_t setup[8] = {bmRT,
                      bReq,
                      (uint8_t)(wVal & 0xFF),
                      (uint8_t)(wVal >> 8),
                      (uint8_t)(wIdx & 0xFF),
                      (uint8_t)(wIdx >> 8),
                      (uint8_t)(wLen & 0xFF),
                      (uint8_t)(wLen >> 8)};
  mxWrN(rSUDFIFO, setup, 8);
  if (mxXfer(tokSETUP, 0, 5) != hrSUCCESS) return -1;
  int total = 0;
  uint8_t toggle = 1;
  while (total < wLen) {
    mxWr(rHCTL, toggle ? 0x20 : 0x10);
    uint8_t r = mxXfer(tokIN, 0, 200);
    if (r != hrSUCCESS) break;
    int bc = mxRd(rRCVBC);
    if (bc > 0) {
      if (total + bc > wLen) bc = wLen - total;
      mxRdN(rRCVFIFO, buf + total, bc);
    }
    total += bc;
    toggle ^= 1;
    if (bc < 64) break;
  }
  mxXfer(tokOUTHS, 0, 200);
  return total;
}

static uint8_t mxEvtToggle = 0;
static int mxIntrIn(uint8_t addr, uint8_t ep, uint8_t *buf, int maxLen) {
  mxWr(rPERADDR, addr);
  mxWr(rHCTL, mxEvtToggle ? 0x20 : 0x10);
  mxWr(rHXFR, tokIN | ep);
  if (!mxWaitHXFR(100)) return -1;
  uint8_t r = mxRd(rHRSL) & 0x0F;
  if (r == hrNAK) return 0;
  if (r != hrSUCCESS) return -1;
  int bc = mxRd(rRCVBC);
  if (bc > maxLen) bc = maxLen;
  if (bc > 0) mxRdN(rRCVFIFO, buf, bc);
  mxEvtToggle ^= 1;
  return bc;
}

static uint8_t mxBtAddr = 0, mxBtEvtEp = 0;

static bool mxChipInit() {
  pinMode(MAX_CS_PIN, OUTPUT);
  digitalWrite(MAX_CS_PIN, HIGH);
  pinMode(MAX_INT_PIN, INPUT);
  maxSPI.begin(MAX_SCK_PIN, MAX_MISO_PIN, MAX_MOSI_PIN, -1);
  mxWr(rUSBCTL, 0x20);
  delay(30);
  mxWr(rUSBCTL, 0x00);
  uint32_t t0 = millis();
  while (!(mxRd(rUSBIRQ) & 0x01)) {
    if (millis() - t0 > 1000) return false;
    delay(5);
  }
  uint8_t rev = mxRd(rREVISION);
  if (rev == 0x00 || rev == 0xFF) return false;
  mxWr(rPINCTL, 0x10);
  mxWr(rMODE, 0x01 | 0x40 | 0x20 | 0x08);
  mxWr(rHIRQ, 0xFF);
  mxWr(rHIEN, 0x60);
  return true;
}
static bool mxWaitConnect(int tmoMs) {
  uint32_t t0 = millis();
  while ((int)(millis() - t0) < tmoMs) {
    uint8_t hirq = mxRd(rHIRQ);
    if (hirq & 0x20) {
      mxWr(rHIRQ, 0x20);
      if (mxRd(rHRSL) & 0xC0) return true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return false;
}
static bool mxBusReset() {
  mxWr(rHCTL, 0x01);
  uint32_t t0 = millis();
  while (mxRd(rHCTL) & 0x01) {
    if (millis() - t0 > 2000) return false;
    delay(10);
  }
  delay(200);
  uint8_t hrsl = mxRd(rHRSL);
  bool ls = (hrsl & 0x40) && !(hrsl & 0x80);
  uint8_t mode = mxRd(rMODE);
  if (ls)
    mode |= 0x02;
  else
    mode &= ~0x02;
  mxWr(rMODE, mode);
  mxWr(rHIRQ, 0x04);
  delay(20);
  return true;
}
static bool mxEnumerateBt() {
  uint8_t buf[128];
  if (mxCtrlRead(0, 0x80, 0x06, 0x0100, 0, 8, buf) < 8) return false;
  if (!mxCtrlWrite(0, 0x00, 0x05, 1, 0, 0, NULL)) return false;
  delay(20);
  mxBtAddr = 1;
  int n = mxCtrlRead(1, 0x80, 0x06, 0x0100, 0, 18, buf);
  if (n < 8) return false;
  bool isBtDev = (buf[4] == 0xE0 && buf[5] == 0x01 && buf[6] == 0x01);
  n = mxCtrlRead(1, 0x80, 0x06, 0x0200, 0, 128, buf);
  if (n < 4) return false;
  uint16_t total = buf[2] | (buf[3] << 8);
  if (total > (uint16_t)n) total = n;
  bool isBtIf = false;
  mxBtEvtEp = 0;
  int off = 0;
  while (off < total) {
    uint8_t dL = buf[off];
    if (!dL) break;
    uint8_t dT = (off + 1 < total) ? buf[off + 1] : 0;
    if (dT == 0x04 && dL >= 9) {
      isBtIf = (buf[off + 5] == 0xE0 && buf[off + 6] == 0x01 &&
                buf[off + 7] == 0x01);
      if (!isBtDev && isBtIf) isBtDev = true;
    }
    if (dT == 0x05 && dL >= 7 && (isBtIf || isBtDev)) {
      uint8_t ea = buf[off + 2], et = buf[off + 3] & 0x03;
      if (et == 0x03 && (ea & 0x80) && !mxBtEvtEp) mxBtEvtEp = ea & 0x0F;
    }
    off += dL;
  }
  if (!isBtDev || !mxBtEvtEp) return false;
  if (!mxCtrlWrite(1, 0x00, 0x09, buf[5], 0, 0, NULL)) return false;
  delay(50);
  mxEvtToggle = 0;
  return true;
}
static bool mxHciCmd(uint16_t op, const uint8_t *par, uint8_t pl) {
  uint8_t cmd[259];
  cmd[0] = op & 0xFF;
  cmd[1] = (op >> 8) & 0xFF;
  cmd[2] = pl;
  if (pl > 0 && par) memcpy(&cmd[3], par, pl);
  return mxCtrlWrite(mxBtAddr, 0x20, 0x00, 0, 0, 3 + pl, cmd);
}
static bool mxHciWaitComplete(uint16_t op, int tmo) {
  uint8_t evt[64];
  uint32_t t0 = millis();
  while ((int)(millis() - t0) < tmo) {
    int n = mxIntrIn(mxBtAddr, mxBtEvtEp, evt, sizeof(evt));
    if (n >= 6 && evt[0] == 0x0E) {
      if ((evt[3] | (evt[4] << 8)) == op) return evt[5] == 0;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}
static void mxParseInqResult(const uint8_t *evt, int len) {
  if (len < 2) return;
  uint8_t code = evt[0], plen = evt[1];
  const uint8_t *p = &evt[2];
  if (plen < 1) return;
  if (code == 0x2F && plen >= 15) {
    uint8_t bd[6];
    for (int b = 0; b < 6; b++) bd[b] = p[5 - b];
    int8_t rssi = (int8_t)p[13];
    bool hasHid = ((p[10] & 0x1F) == 5);
    char name[SEEN_NAME_LEN] = "";
    if (plen >= 15 + 240) {
      const uint8_t *eir = &p[14];
      int eo = 0;
      while (eo < 238 && eir[eo]) {
        uint8_t tl = eir[eo], tt = eir[eo + 1];
        if ((tt == 0x08 || tt == 0x09) && tl > 1) {
          int nl = (tl - 1 < SEEN_NAME_LEN - 1) ? tl - 1 : SEEN_NAME_LEN - 1;
          memcpy(name, &eir[eo + 2], nl);
          name[nl] = '\0';
        }
        eo += tl + 1;
      }
    }
    char as[18];
    snprintf(as, sizeof(as), "%02x:%02x:%02x:%02x:%02x:%02x", bd[0], bd[1],
             bd[2], bd[3], bd[4], bd[5]);
    addSeenDev(NimBLEAddress(as, 0), name, rssi, hasHid, false, true);
    needRedraw = true;
  } else if (code == 0x22 && plen >= 2) {
    int numR = p[0];
    const uint8_t *r = &p[1];
    for (int i = 0; i < numR; i++) {
      if ((int)((r - &p[1]) + 14) > plen - 1) break;
      uint8_t bd[6];
      for (int b = 0; b < 6; b++) bd[b] = r[5 - b];
      char as[18];
      snprintf(as, sizeof(as), "%02x:%02x:%02x:%02x:%02x:%02x", bd[0], bd[1],
               bd[2], bd[3], bd[4], bd[5]);
      addSeenDev(NimBLEAddress(as, 0), "", (int8_t)r[13], ((r[10] & 0x1F) == 5),
                 false, true);
      r += 14;
    }
    needRedraw = true;
  }
}

void max3421Task(void *) {
  if (!mxChipInit()) {
    vTaskDelete(NULL);
    return;
  }
  maxPresent = true;
  needRedraw = true;
  bool ready = false;
  for (int att = 0; att < 10 && !ready; att++) {
    if (mxWaitConnect(5000) && mxBusReset()) {
      delay(100);
      if (mxEnumerateBt() && mxHciCmd(0x0C03, NULL, 0) &&
          mxHciWaitComplete(0x0C03, 3000)) {
        uint8_t m = 2;
        if (mxHciCmd(0x0C45, &m, 1)) mxHciWaitComplete(0x0C45, 1000);
        maxDongleReady = true;
        ready = true;
        needRedraw = true;
      }
    }
    if (!ready) vTaskDelay(pdMS_TO_TICKS(2000));
  }
  if (!ready) {
    maxPresent = false;
    vTaskDelete(NULL);
    return;
  }
  bool inqAct = false;
  while (true) {
    if (classicScanRequest && !inqAct) {
      uint8_t par[5] = {0x33, 0x8B, 0x9E, 0x08, 0x00};
      if (mxHciCmd(0x0401, par, 5)) {
        inqAct = true;
        classicScanDone = false;
      } else {
        classicScanDone = true;
        classicScanRequest = false;
      }
    }
    uint8_t evt[270];
    int n = mxIntrIn(mxBtAddr, mxBtEvtEp, evt, sizeof(evt));
    if (n > 2) {
      if (evt[0] == 0x01) {
        inqAct = false;
        classicScanDone = true;
        classicScanRequest = false;
        needRedraw = true;
      } else if (evt[0] == 0x22 || evt[0] == 0x2F)
        mxParseInqResult(evt, n);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
#else
void max3421Task(void *) { vTaskDelete(NULL); }
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
    usb_host_lib_handle_events(1, NULL);
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

    taskYIELD();
  }
}

// ===================== MOUSE SEND TASK =====================
void mouseSendTask(void *) {
  mouse_evt_t m, acc = {};
  uint8_t prevBtn = 0;
  bool dirty = false, m4h = false;
  uint32_t lastUs = 0;
  while (true) {
    if (switchInProgress) {
      acc = {};
      dirty = false;
      prevBtn = 0;
      m4h = false;
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    TickType_t waitTicks;
    int il = idleLevel();
    if (il == 0) waitTicks = portMAX_DELAY;
    else if (il <= 2) waitTicks = pdMS_TO_TICKS(5);
    else waitTicks = pdMS_TO_TICKS(50);
    
    bool got = xQueueReceive(mouseQueue, &m, waitTicks) == pdTRUE;

    // Batch-drain: if we got one, grab all pending without blocking
    if (got && !switchInProgress) {
      do {
        bool m4 = (m.buttons & 0x08);
        if (m4 && !m4h && millis() - lastSwitchMs > SWITCH_DEBOUNCE_MS)
          switchRequest = true;
        m4h = m4;
        uint8_t b3 = m.buttons & 0x07;
        bool bc = (b3 != prevBtn);
        acc.buttons = m.buttons;
        acc.x += m.x;
        acc.y += m.y;
        acc.wheel += m.wheel;
        dirty = true;
        if (bc) {
          txMouse(&acc);
          prevBtn = b3;
          acc = {};
          acc.buttons = m.buttons;
          dirty = false;
          lastUs = micros();
#if DEBUG_MODE
          dbgMsTx++;
#endif
        }
      } while (!switchInProgress && xQueueReceive(mouseQueue, &m, 0) == pdTRUE);
    }

    uint32_t sendInterval;
    switch (idleLevel()) {
      case 4: sendInterval = IDLE4_MOUSE_SEND_US; break;
      case 3: sendInterval = IDLE3_MOUSE_SEND_US; break;
      case 2: sendInterval = IDLE2_MOUSE_SEND_US; break;
      case 1: sendInterval = IDLE1_MOUSE_SEND_US; break;
      default: sendInterval = MOUSE_SEND_US; break;
    }
    if (dirty && micros() - lastUs >= sendInterval) {
      txMouse(&acc);
      acc = {};
      dirty = false;
      lastUs = micros();
#if DEBUG_MODE
      dbgMsTx++;
#endif
    }
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
      NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
      if (gap && c->isConnected()) {
        NimBLERemoteCharacteristic *nm =
            gap->getCharacteristic(NimBLEUUID("2A00"));
        if (nm && nm->canRead() && c->isConnected()) {
          std::string gn = nm->readValue();
          if (!gn.empty()) {
            strncpy(on, gn.c_str(), nl - 1);
            on[nl - 1] = '\0';
          }
        }
      }
      if (c->isConnected() && c->getService(NimBLEUUID("1812"))) r = 1;
    }
    waitDisconn(c);
  }
  return r;
}

static void doProbe(NimBLEClient *c, int si, int pn, int tot) {
  if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) return;
  seen_dev_t &d = seenDevs[si];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "probe %d/%d %s", pn, tot,
           d.mac_short);
  bleState = 6;
  needRedraw = true;
  int li = (probeLogCount < BLE_PROBE_MAX * 2) ? probeLogCount++ : -1;
  if (li >= 0)
    snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d %c(%s)[%d] ..", pn, tot,
             d.isClassic ? 'C' : 'B', d.mac_short, d.rssi);
  char pn2[SEEN_NAME_LEN] = "";
  int r = probeHid(c, d.addr, pn2, sizeof(pn2));
  const char *rs = (r == 1) ? "HID!" : (r == 0) ? "noHID" : "fail";
  if (li >= 0) {
    if (pn2[0])
      snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d %c(%s)[%d] %s \"%s\"",
               pn, tot, d.isClassic ? 'C' : 'B', d.mac_short, d.rssi, rs, pn2);
    else
      snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d %c(%s)[%d] %s", pn,
               tot, d.isClassic ? 'C' : 'B', d.mac_short, d.rssi, rs);
  }
  needRedraw = true;
  if (r == 1) {
    bleAddr = d.addr;
    bleFound = true;
    bleMatchReason =
        (pn2[0] && nameContains(std::string(pn2), BLE_KBD_MATCH)) ? 3 : 4;
    strncpy(bleFoundName, pn2[0] ? pn2 : "(HID)", sizeof(bleFoundName) - 1);
    bleState = 2;
    needRedraw = true;
  }
}

static void doAppleProbe(NimBLEClient *c, int si, int pn, int tot) {
  if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) return;
  seen_dev_t &d = seenDevs[si];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "APL %d/%d %s", pn, tot,
           d.mac_short);
  bleState = 6;
  needRedraw = true;
  int li = (probeLogCount < BLE_PROBE_MAX * 2) ? probeLogCount++ : -1;
  if (li >= 0)
    snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d B(%s)[%d] pair..", pn,
             tot, d.mac_short, d.rssi);
  passkeyActive = false;
  pairingDone = false;
  pairingSuccess = false;
  if (c->isConnected()) waitDisconn(c);
  if (!c->connect(d.addr)) {
    if (li >= 0)
      snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d B(%s)[%d] no conn", pn,
               tot, d.mac_short, d.rssi);
    needRedraw = true;
    passkeyActive = false;
    return;
  }
  NimBLERemoteService *hid =
      c->isConnected() ? c->getService(NimBLEUUID("1812")) : nullptr;
  if (!hid && passkeyActive && !pairingDone && c->isConnected()) {
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "type PIN on kbd!");
    needRedraw = true;
    uint32_t t0 = millis();
    while (!pairingDone && c->isConnected() &&
           (millis() - t0 < APPLE_PAIR_TIMEOUT_MS))
      vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!hid && pairingDone && pairingSuccess && c->isConnected())
    hid = c->getService(NimBLEUUID("1812"));
  char pn2[SEEN_NAME_LEN] = "";
  if (c->isConnected()) {
    NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
    if (gap && c->isConnected()) {
      NimBLERemoteCharacteristic *nm =
          gap->getCharacteristic(NimBLEUUID("2A00"));
      if (nm && nm->canRead() && c->isConnected()) {
        std::string gn = nm->readValue();
        if (!gn.empty()) strncpy(pn2, gn.c_str(), sizeof(pn2) - 1);
      }
    }
  }
  bool ok = (hid && c->isConnected());
  const char *rs = ok                                 ? "HID!"
                   : (pairingDone && !pairingSuccess) ? "pfail"
                   : (passkeyActive && !pairingDone)  ? "tmout"
                                                      : "noHID";
  if (li >= 0) {
    if (pn2[0])
      snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d B(%s)[%d] %s \"%s\"",
               pn, tot, d.mac_short, d.rssi, rs, pn2);
    else
      snprintf(probeLog[li], sizeof(probeLog[0]), "%d/%d B(%s)[%d] %s", pn, tot,
               d.mac_short, d.rssi, rs);
  }
  needRedraw = true;
  if (ok) {
    bleAddr = d.addr;
    bleFound = true;
    bleMatchReason = 5;
    strncpy(bleFoundName, pn2[0] ? pn2 : "(Apple HID)",
            sizeof(bleFoundName) - 1);
    bleState = 2;
    needRedraw = true;
  }
  if (c->isConnected()) waitDisconn(c, 5000);
  passkeyActive = false;
  pairingDone = false;
  pairingSuccess = false;
  vTaskDelay(pdMS_TO_TICKS(300));
}

static bool bleDoConnect() {
  bleState = 3;
  char sm[12];
  shortMac(bleAddr.toString().c_str(), sm, sizeof(sm));
  strncpy(bleConnAddr, sm, sizeof(bleConnAddr));
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "connect %s", sm);
  needRedraw = true;
  passkeyActive = false;
  pairingDone = false;
  pairingSuccess = false;
  NimBLEClient *c = nullptr;
  bool connected = false;
  for (int att = 0; att < MAX_CONNECT_RETRIES && !connected; att++) {
    if (att > 0) {
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "retry %d/%d %s", att + 1,
               MAX_CONNECT_RETRIES, sm);
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(1000 + att * 500));
    }
    passkeyActive = false;
    pairingDone = false;
    pairingSuccess = false;
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
    NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
    if (gap && c->isConnected()) {
      NimBLERemoteCharacteristic *nm =
          gap->getCharacteristic(NimBLEUUID("2A00"));
      if (nm && nm->canRead() && c->isConnected()) {
        std::string gn = nm->readValue();
        if (!gn.empty()) {
          strncpy(bleFoundName, gn.c_str(), sizeof(bleFoundName) - 1);
          needRedraw = true;
        }
      }
    }
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
  needRedraw = true;
  while (c->isConnected()) vTaskDelay(pdMS_TO_TICKS(500));
  bleKbdConnected = false;
  bleState = 5;
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "disconnected");
  needRedraw = true;
  NimBLEDevice::deleteClient(c);
  bleFound = false;
  return true;
}

void bleTask(void *) {
  NimBLEDevice::init("CoreS3-KVM");
  setKbdSecMode();
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCbInstance, true);
  scan->setActiveScan(true);
  scan->setInterval(200);
  scan->setWindow(50);

  while (true) {
    if (USE_MAX_MODULE && maxDongleReady) {
      classicScanDone = false;
      classicScanRequest = true;
    }

    if (knownKbdValid && !bleKbdConnected) {
      bleFound = true;
      bleAddr = knownKbdAddr;
      if (bleFoundName[0] == '\0')
        strncpy(bleFoundName, "(reconnecting)", sizeof(bleFoundName) - 1);
      bool wasConn = bleDoConnect();
      if (wasConn) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      knownKbdValid = false;
      probesDone = true;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (probesDone) {
      bleState = 7;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "mouse-only");
      needRedraw = true;
      while (!knownKbdValid) vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    bleState = 1;
    bleMatchReason = 0;
    bleFound = false;
    bleFoundName[0] = '\0';
    bleConnAddr[0] = '\0';
    resetSeenDevs();
    probeLogCount = 0;
    probeTotalExpected = 0;
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "scanning...");
    needRedraw = true;

    if (maxDongleReady) {
      classicScanDone = false;
      classicScanRequest = true;
    }

    uint32_t budgetStart = millis();
    while (!bleFound && (millis() - budgetStart < BLE_SCAN_BUDGET_MS)) {
      bleScanDone = false;
      if (!scan->start(0, false)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      uint32_t burstEnd = millis() + BLE_SCAN_BURST_MS;
      while (!bleScanDone && !bleFound && millis() < burstEnd &&
             (millis() - budgetStart < BLE_SCAN_BUDGET_MS))
        vTaskDelay(pdMS_TO_TICKS(50));
      scan->stop();
      if (bleFound) break;
      vTaskDelay(pdMS_TO_TICKS(BLE_SCAN_GAP_MS));
    }
    scan->stop();

    if (classicScanRequest && !classicScanDone) {
#if USE_MAX_MODULE
      if (maxDongleReady) mxHciCmd(0x0402, NULL, 0);
#endif
      uint32_t w0 = millis();
      while (!classicScanDone && millis() - w0 < 3000)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    classicScanRequest = false;

    if (bleFound) {
      setKbdSecMode();
      bool ok = bleDoConnect();
      if (!ok && !knownKbdValid) {
        bleFound = false;
        probesDone = true;
      }
      if (!knownKbdValid) vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    int sorted[SEEN_DEV_MAX], sortedN = getTopByRssi(sorted, SEEN_DEV_MAX);
    int probeList[BLE_PROBE_MAX], probeCnt = 0;
    for (int s = 0; s < sortedN && probeCnt < BLE_PROBE_MAX; s++) {
      int i = sorted[s];
      if (seenDevs[i].rssi < BLE_PROBE_MIN_RSSI) continue;
      if (seenDevs[i].name[0]) continue;
      probeList[probeCnt++] = i;
    }
    probeTotalExpected = probeCnt;
    probeLogCount = 0;

    if (probeCnt > 0 && !bleFound) {
      strncpy(rtcDbg, "probe-start", sizeof(rtcDbg));
      if (!pc) {
        pc = NimBLEDevice::createClient();
        if (pc) pc->setClientCallbacks(&secCb);
      }
      if (pc) {
        for (int p = 0; p < probeCnt && !bleFound; p++) {
          int si = probeList[p];
          if (si < 0 || si >= SEEN_DEV_MAX || !seenDevs[si].used) continue;
          bool apple = seenDevs[si].isApple;
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
        if (pc->isConnected()) waitDisconn(pc, 3000);
        strncpy(rtcDbg, "probe-cleanup", sizeof(rtcDbg));
        passkeyActive = false;
        pairingDone = false;
        pairingSuccess = false;
        needRedraw = true;
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    }

    strncpy(rtcDbg, "pre-setKbdSec", sizeof(rtcDbg));
    vTaskDelay(pdMS_TO_TICKS(200));
    setKbdSecMode();
    strncpy(rtcDbg, "post-setKbdSec", sizeof(rtcDbg));

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
static void drawPasskey() {
  M5.Display.startWrite();
  M5.Display.fillScreen(0x0010);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(20, 30);
  M5.Display.print("BLE Pairing");
  M5.Display.setCursor(20, 60);
  M5.Display.setTextColor(YELLOW);
  M5.Display.print("Type on keyboard:");
  M5.Display.setTextSize(5);
  M5.Display.setTextColor(GREEN);
  char buf[12];
  snprintf(buf, sizeof(buf), "%06d", (int)passkeyValue);
  M5.Display.setCursor(70, 110);
  M5.Display.print(buf);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 180);
  bool pd = pairingDone, ps = pairingSuccess;
  if (pd) {
    M5.Display.setTextColor(ps ? GREEN : RED);
    M5.Display.print(ps ? "Paired OK!" : "FAILED");
  } else {
    M5.Display.setTextColor(WHITE);
    M5.Display.printf("Waiting... %ds", (millis() - passkeyShownMs) / 1000);
  }
  if (bleFoundName[0]) {
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 220);
    M5.Display.setTextColor(CYAN);
    M5.Display.printf("Device: %s", bleFoundName);
  }
  M5.Display.endWrite();
}

struct scan_disp_entry_t {
  char mac[12];
  int rssi;
  uint8_t flags;
};

static void drawUI() {
  if (screenTurnedOff) return;

  if (passkeyActive) {
    drawPasskey();
    return;
  }
  int cBS = bleState, cMR = bleMatchReason, cSC = seenDevCount,
      cPLC = probeLogCount, cPT = probeTotalExpected;
  bool cUR = usbMouseReady, cMP = maxPresent, cMR2 = maxDongleReady;

  M5.Display.startWrite();
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(10, 4);
  M5.Display.setTextColor(WHITE);
  M5.Display.print("PC ");
  M5.Display.setTextColor(activeTarget == 0 ? GREEN : CYAN);
  M5.Display.print(activeTarget + 1);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 36);
  M5.Display.setTextColor(cUR ? GREEN : RED);
  M5.Display.printf("M: %s", cUR ? "OK" : "--");
  if (cMP) {
    M5.Display.setCursor(150, 36);
    M5.Display.setTextColor(cMR2 ? CYAN : YELLOW);
    M5.Display.printf("BT: %s", cMR2 ? "OK" : "..");
  }

  M5.Display.setCursor(10, 60);
  switch (cBS) {
    case 0:
      M5.Display.setTextColor(RED);
      M5.Display.printf("K: %s", bleStatusMsg);
      break;
    case 1:
      M5.Display.setTextColor(YELLOW);
      M5.Display.printf("K: scan %d dev", cSC);
      break;
    case 2:
      M5.Display.setTextColor(GREEN);
      M5.Display.printf("K: FOUND (%s)", matchStr(cMR));
      break;
    case 3:
      M5.Display.setTextColor(YELLOW);
      M5.Display.printf("K: %s", bleStatusMsg);
      break;
    case 4:
      M5.Display.setTextColor(GREEN);
      M5.Display.print("K: CONNECTED");
      break;
    case 5:
      M5.Display.setTextColor(RED);
      M5.Display.print("K: lost");
      break;
    case 6:
      M5.Display.setTextColor(YELLOW);
      M5.Display.printf("K: %s", bleStatusMsg);
      break;
    case 7:
      M5.Display.setTextColor(0x7BEF);
      M5.Display.print("K: mouse-only");
      break;
  }

  int yBase = 82;
  M5.Display.setTextSize(1);
  if (bleFoundName[0] && bleFoundName[0] != '(') {
    M5.Display.setCursor(10, yBase);
    M5.Display.setTextColor(GREEN);
    M5.Display.printf(">> %s", bleFoundName);
    yBase += 12;
  } else if (bleConnAddr[0] && (cBS == 3 || cBS == 5)) {
    M5.Display.setCursor(10, yBase);
    M5.Display.setTextColor(YELLOW);
    M5.Display.printf(">> %s", bleConnAddr);
    yBase += 12;
  }

  int y = yBase + 2;

  if (cBS == 1) {
    scan_disp_entry_t dd[TOP_DISPLAY];
    int ddN = 0, ddTotal = 0;
    xSemaphoreTake(seenMutex, portMAX_DELAY);
    {
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
      ddN = (n < TOP_DISPLAY) ? n : TOP_DISPLAY;
      for (int t = 0; t < ddN; t++) {
        int i = all[t];
        memcpy(dd[t].mac, seenDevs[i].mac_short, sizeof(dd[t].mac));
        dd[t].rssi = seenDevs[i].rssi;
        dd[t].flags = (seenDevs[i].name[0] ? 0x01 : 0) |
                      (seenDevs[i].hasHidUuid ? 0x02 : 0) |
                      (seenDevs[i].isApple ? 0x04 : 0) |
                      (seenDevs[i].isClassic ? 0x08 : 0);
      }
      ddTotal = seenDevCount;
    }
    xSemaphoreGive(seenMutex);
    for (int t = 0; t < ddN; t++) {
      int col = t / SCAN_COL_ROWS, row = t % SCAN_COL_ROWS;
      int xP = col == 0 ? 10 : 168, yP = y + row * 11;
      if (yP > 228) break;
      M5.Display.setCursor(xP, yP);
      if (dd[t].flags & 0x01)
        M5.Display.setTextColor(WHITE);
      else if (dd[t].flags & 0x02)
        M5.Display.setTextColor(GREEN);
      else if (dd[t].flags & 0x04)
        M5.Display.setTextColor(YELLOW);
      else if (dd[t].flags & 0x08)
        M5.Display.setTextColor(CYAN);
      else
        M5.Display.setTextColor(0x7BEF);
      M5.Display.printf("%d:%c(%s)[%d]", t + 1,
                        (dd[t].flags & 0x08) ? 'C' : 'B', dd[t].mac,
                        dd[t].rssi);
    }
    if (ddTotal > TOP_DISPLAY) {
      M5.Display.setCursor(10, y + SCAN_COL_ROWS * 11);
      M5.Display.setTextColor(DARKGREY);
      M5.Display.printf("+%d more", ddTotal - TOP_DISPLAY);
    }
  } else if (cBS == 6) {
    M5.Display.setCursor(10, y);
    M5.Display.setTextColor(CYAN);
    M5.Display.printf("Probing %d of %d devices", cPT, cSC);
    y += 14;
    for (int i = 0; i < cPLC && y < 230; i++) {
      M5.Display.setCursor(10, y);
      if (strstr(probeLog[i], "HID!"))
        M5.Display.setTextColor(GREEN);
      else if (strstr(probeLog[i], "noHID") || strstr(probeLog[i], "fail") ||
               strstr(probeLog[i], "no conn") || strstr(probeLog[i], "tmout") ||
               strstr(probeLog[i], "pfail"))
        M5.Display.setTextColor(0xFB20);
      else if (strstr(probeLog[i], "pair"))
        M5.Display.setTextColor(CYAN);
      else
        M5.Display.setTextColor(YELLOW);
      M5.Display.print(probeLog[i]);
      y += 12;
    }
  } else if (cBS == 7) {
    M5.Display.setCursor(10, y);
    M5.Display.setTextColor(WHITE);
    M5.Display.print("Working as mouse-only KVM");
    y += 14;
    if (cPLC > 0) {
      M5.Display.setCursor(10, y);
      M5.Display.setTextColor(DARKGREY);
      M5.Display.print("Last results:");
      y += 12;
      for (int i = 0; i < cPLC && y < 230; i++) {
        M5.Display.setCursor(10, y);
        M5.Display.setTextColor(DARKGREY);
        M5.Display.print(probeLog[i]);
        y += 12;
      }
    }
  }

  // Footer line 1: debug or standard
  M5.Display.setTextSize(1);
#if DEBUG_MODE
  float cpuTemp = 0;
  if (temp_handle) temperature_sensor_get_celsius(temp_handle, &cpuTemp);

  M5.Display.setCursor(5, 210);
  M5.Display.setTextColor(cpuTemp > 60 ? RED : CYAN);
  // T:Temp | U:InputHz | E:OutputHz | Q:Queue
  M5.Display.printf("T:%.0fC U:%d E:%d Q:%d", cpuTemp, (int)dbgUsbHz,
                    (int)dbgEspHz, dbgQPeak);
#endif
  M5.Display.setCursor(5, 220);
  M5.Display.setTextColor(wasReboot ? RED : DARKGREY);
  M5.Display.printf("%s%s", wasReboot ? "!" : "", rtcDbg);

  int bat = M5.Power.getBatteryLevel();

  M5.Display.setCursor(5, 230);

  M5.Display.setTextColor(bat < 20 ? RED : GREEN);
  M5.Display.printf("Bat:%d%%  ", bat);

  M5.Display.setTextColor(DARKGREY);
  M5.Display.printf("S:%d H:%d", uxTaskGetStackHighWaterMark(NULL),
                    esp_get_free_heap_size() / 1024);

  M5.Display.endWrite();
}

// ===================== SETUP =====================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.end();
  M5.Power.begin();
  M5.Power.setUsbOutput(true);
  Serial.begin(115200);
  delay(100);
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

  if (rtcDbg[0] >= 0x20 && rtcDbg[0] < 0x7F) {
    wasReboot = true;
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, 40);
    M5.Display.setTextColor(RED);
    M5.Display.printf("PREV CRASH: %s", rtcDbg);
    delay(5000);
  }
  strncpy(rtcDbg, "setup-ok", sizeof(rtcDbg));

  seenMutex = xSemaphoreCreateMutex();
#if USE_MAX_MODULE
  spiMutex = xSemaphoreCreateMutex();
#endif
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
  sendCmd(ATOM_MAC_1, PKT_ACTIVATE);
  sendCmd(ATOM_MAC_2, PKT_DEACTIVATE);

  xTaskCreatePinnedToCore(usbHostTask, "USB", 8192, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(mouseSendTask, "MOUSE", 4096, NULL, 9, NULL, 0);

#if WITH_KEYBOARD
  xTaskCreatePinnedToCore(bleTask, "BLE", 64 * 1024, NULL, 1, NULL, 0);
#if USE_MAX_MODULE
  xTaskCreatePinnedToCore(max3421Task, "MAX", 8192, NULL, 1, NULL, 0);
#endif
#endif

  delay(300);
  lastActivityMs = millis();
#if DEBUG_MODE
  dbgLastTick = millis();
#endif
  drawUI();
}

// ===================== LOOP =====================
static uint32_t lastBeat = 0;
static bool prevUsb = false, prevBle = false, prevPk = false;
static int prevBleS = -1;

void loop() {
  M5.update();
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

  // BLE setup phases: scanning, found, connecting, connected-first-time, probing
  if (cS >= 1 && cS <= 6) screenNeeded = true;

  // Passkey active
  if (cP) screenNeeded = true;

  // 10s after PC switch
  if (millis() - lastPCSwitchMs < 10000) screenNeeded = true;

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
      lastIdleLevel = idlelvl;
      if (idlelvl >= 3) {
        wifiNeedsBackToNormal = true;
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        esp_wifi_set_max_tx_power(40);
      } else if (wifiNeedsBackToNormal) {
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_set_max_tx_power(80); 
        wifiNeedsBackToNormal = false;
      }
  }

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
```

</details>

---

## 🚀 First Run

1. Press the **reset button once** (don't hold) on the CoreS3 SE
2. Plug in your **mouse dongle** to the CoreS3 USB port
3. The device will **auto-scan and pair** your Bluetooth keyboard
4. Make sure to **"Forget"** the keyboard in macOS Bluetooth settings first if it was previously paired with your Mac

---

## ⚠️ Known Issues

| Issue | Explanation |
|---|---|
| **Mouse lag during BT scan** | The device has a single radio module shared between ESP-NOW (mouse) and BLE (keyboard). Lag stops once keyboard pairing completes. |
| **Security PIN prompt** | Some keyboards require entering a 6-digit PIN displayed on the CoreS3 screen. |
| **First input delay after idle** | The device enters power-saving mode after inactivity (10 sec = 1ms delay, 1 minute = 20ms delay, 5 min = 50ms delay, 15 min = 100ms delay). The first mouse movement after wake may feel slightly delayed. |
| **Mouse rate is not 1000Hz** | Read speed of your mouse is pure 1:1 from your mouse settings (as long as device is capable of this rate. I did not test it with mouse that has higher than 1000Hz, but it does support 1000Hz input!). But output works differently, you have to update MOUSE_SEND_HZ variable and set it a value that is higher than actual mouse rate, for example for my keychrone m3 mini i get 1000Hz ouput rate when i set MOUSE_SEND_HZ to 1750-2000 (this number is more like code delay between iterations) |
| **Battery status does not update** | If you don't use debug mode, the only way to update the screen is to press mouse4 to switch pc or to plug-out/in mouse dongle, this is made for performance reasons |
| **Screen goes black** | This is done because of power efficiency - screen is only displayed during setup/pc switch (10sec here), without it battery will drain faster than it's charing from battery bottom |

---

## 🏗️ Architecture

```
┌──────────────┐      ESP-NOW       ┌──────────────┐
│  Wireless    │  ───────────────►  │  AtomS3U #1  │──── USB ──── PC 1
│  Mouse       │                    └──────────────┘
│  (dongle) ──►│ CoreS3 SE          ┌──────────────┐
│              │  ───────────────►  │  AtomS3U #2  │──── USB ──── PC 2
│  Bluetooth   │      ESP-NOW       └──────────────┘
│  Keyboard ──►│
└──────────────┘
        │
   [Mouse4] = Switch PC
```

The CoreS3 SE acts as the central hub: it reads the USB mouse dongle and BLE keyboard, then forwards all HID events over ESP-NOW to whichever Atom S3U is currently active. Each Atom S3U appears as a standard USB keyboard + mouse to its host PC.

---

## 📜 License

[GPL-3.0-1](https://github.com/krll-kov/m5stack-wireless-kvm-switch?tab=GPL-3.0-1-ov-file#readme)
