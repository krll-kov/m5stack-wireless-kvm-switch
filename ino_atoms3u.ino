#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "USBCDC.h"
#include "freertos/queue.h"

// Debug is no longer a build switch. The receiver mirrors the transmitter:
// it turns itself on when CoreS3 telemetry (PKT_DEBUG) starts arriving and
// off again a few seconds after it stops. Toggle it with BtnC on the CoreS3.

extern "C" {
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_remote_wakeup(void);
void tud_disconnect(void);
void tud_connect(void);
bool tud_hid_n_ready(uint8_t instance);
bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      void const* report, uint16_t len);
uint8_t tud_hid_n_get_protocol(uint8_t instance);
}

// 2.4GHz channel for ESP-NOW. Must match on the CoreS3 and every AtomS3U.
// 13 sits at the top edge of the band, so it only has neighbours on one side.
// Channels 12-13 are outside the ESP32 default country range and will not take
// until that range is declared - see applyEspNowChannel().
#define ESPNOW_CHAN 13
#define ESPNOW_COUNTRY "PL"

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT 0x05
#define PKT_CONSUMER 0x06
#define PKT_APPLE_FN 0x07
#define PKT_DEBUG 0x08     // transmitter telemetry, once a second while debug is on

#define BOOT_GRACE_MS       5000
#define ESPNOW_TIMEOUT_MS   3400
#define USB_GONE_REPLUG_MS  3000
#define SLEEP_SETTLE_MS     7500

#define USB_IDLE_TIMEOUT_MS_4 900000
#define USB_IDLE_DELAY_MS_4   100
#define USB_IDLE_TIMEOUT_MS_3 300000
#define USB_IDLE_DELAY_MS_3   50
#define USB_IDLE_TIMEOUT_MS_2 60000
#define USB_IDLE_DELAY_MS_2   20
#define USB_IDLE_TIMEOUT_MS_1 10000
#define USB_IDLE_DELAY_MS_1   1

#define APPLE_FN_REPORT_ID 5

static const uint8_t appleFnDesc[] = {
  0x06, 0xFF, 0x00,           // Usage Page (Apple Vendor Top Case 0x00FF)
  0x09, 0x03,                 // Usage (Keyboard Fn)
  0xA1, 0x01,                 // Collection (Application)
    0x85, APPLE_FN_REPORT_ID, //   Report ID
    0x06, 0xFF, 0x00,         //   Usage Page (Apple Vendor Top Case)
    0x09, 0x03,               //   Usage (Keyboard Fn)
    0x15, 0x00,               //   Logical Minimum (0)
    0x25, 0x01,               //   Logical Maximum (1)
    0x75, 0x01,               //   Report Size (1)
    0x95, 0x01,               //   Report Count (1)
    0x81, 0x02,               //   Input (Data, Variable, Absolute)
    0x75, 0x07,               //   Report Size (7)
    0x95, 0x01,               //   Report Count (1)
    0x81, 0x01,               //   Input (Constant)
  0xC0                        // End Collection
};

class USBHIDAppleFn : public USBHIDDevice {
  USBHID hid;
public:
  USBHIDAppleFn() : hid() {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
      hid.addDevice(this, sizeof(appleFnDesc));
    }
  }
  void begin() { hid.begin(); }
  uint16_t _onGetDescriptor(uint8_t *dst) override {
    memcpy(dst, appleFnDesc, sizeof(appleFnDesc));
    return sizeof(appleFnDesc);
  }
};

static const uint8_t mouseDesc16[] = {
  0x05, 0x01,              // Usage Page (Generic Desktop)
  0x09, 0x02,              // Usage (Mouse)
  0xA1, 0x01,              // Collection (Application)
    0x85, 0x02,            //   Report ID (2)
    0x09, 0x01,            //   Usage (Pointer)
    0xA1, 0x00,            //   Collection (Physical)
      0x05, 0x09,          //     Usage Page (Button)
      0x19, 0x01,          //     Usage Minimum (1)
      0x29, 0x05,          //     Usage Maximum (5)
      0x15, 0x00,          //     Logical Minimum (0)
      0x25, 0x01,          //     Logical Maximum (1)
      0x95, 0x05,          //     Report Count (5)
      0x75, 0x01,          //     Report Size (1)
      0x81, 0x02,          //     Input (Data, Variable, Absolute)
      0x95, 0x01,          //     Report Count (1)
      0x75, 0x03,          //     Report Size (3)
      0x81, 0x01,          //     Input (Constant) - padding
      0x05, 0x01,          //     Usage Page (Generic Desktop)
      0x09, 0x30,          //     Usage (X)
      0x09, 0x31,          //     Usage (Y)
      0x16, 0x01, 0x80,    //     Logical Minimum (-32767)
      0x26, 0xFF, 0x7F,    //     Logical Maximum (32767)
      0x95, 0x02,          //     Report Count (2)
      0x75, 0x10,          //     Report Size (16)
      0x81, 0x06,          //     Input (Data, Variable, Relative)
      0x09, 0x38,          //     Usage (Wheel)
      0x15, 0x81,          //     Logical Minimum (-127)
      0x25, 0x7F,          //     Logical Maximum (127)
      0x95, 0x01,          //     Report Count (1)
      0x75, 0x08,          //     Report Size (8)
      0x81, 0x06,          //     Input (Data, Variable, Relative)
      0x05, 0x0C,          //     Usage Page (Consumer)
      0x0A, 0x38, 0x02,    //     Usage (AC Pan)
      0x15, 0x81,          //     Logical Minimum (-127)
      0x25, 0x7F,          //     Logical Maximum (127)
      0x95, 0x01,          //     Report Count (1)
      0x75, 0x08,          //     Report Size (8)
      0x81, 0x06,          //     Input (Data, Variable, Relative)
    0xC0,                  //   End Collection
  0xC0                     // End Collection
};

class USBHIDMouse16 : public USBHIDDevice {
  USBHID hid;
public:
  USBHIDMouse16() : hid() {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
      hid.addDevice(this, sizeof(mouseDesc16));
    }
  }
  void begin() { hid.begin(); }
  uint16_t _onGetDescriptor(uint8_t *dst) override {
    memcpy(dst, mouseDesc16, sizeof(mouseDesc16));
    return sizeof(mouseDesc16);
  }
};

USBHIDKeyboard UsbKbd;
USBHIDMouse16 UsbMouse;
USBHIDConsumerControl UsbConsumer;
USBHIDAppleFn UsbAppleFn;

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
  int16_t x;
  int16_t y;
  int8_t wheel;
  int8_t pan;
};
#pragma pack(pop)

QueueHandle_t mouseQ;
QueueHandle_t kbdQ;
QueueHandle_t consumerQ;
QueueHandle_t appleFnQ;

volatile bool deviceActive = false;
volatile bool pendingActivate = false;
volatile bool pendingDeactivate = false;
static uint8_t prevBtn = 0;

static bool wasSuspended = false;
static bool wakeReplugDone = false;
static bool wasActiveBeforeSuspend = false;
volatile bool hostSuspended = false;

static uint32_t bootMs = 0;
volatile uint32_t lastPacketMs = 0;
static uint32_t usbGoneSinceMs = 0;
static uint32_t suspendStartMs = 0;
static uint32_t lastInputMs = 0;

// ── Debug stats ──
// All of it is gated on dbgOn; while it is false nothing is counted or printed,
// so normal operation is untouched. Output goes to the USB CDC port.
#define DBG_TIMEOUT_MS 5000   // debug off this long after the last telemetry frame
static volatile bool dbgOn = false;
static volatile bool dbgArm = false;   // set with dbgOn; cleared once loop() re-arms
static volatile uint32_t dbgLastTeleMs = 0;
static volatile uint32_t dbgEspRecv = 0;
static volatile uint32_t dbgRxMouse = 0, dbgRxHb = 0;
static uint32_t dbgLastStatMs = 0;
static volatile uint32_t dbgUsbSends = 0;
static volatile uint32_t dbgMerges = 0;
static volatile uint32_t dbgBusyWaitSum = 0;
static volatile uint32_t dbgBusyWaitCnt = 0;
static volatile uint32_t dbgBusyWaitMax = 0;
static volatile uint8_t  dbgMouseQPeak = 0;
static volatile uint8_t  dbgKbdQPeak = 0;
static volatile uint32_t dbgRxGapMax = 0;    // longest silence on the air, ms
// Buckets for inter-packet gaps. A per-second maximum hides whether dropouts
// are rare spikes or a steady pattern, and the pattern is what identifies the
// source: <20, 20-49, 50-99, 100-199, 200-499, >=500 ms.
#define DBG_BUCKETS 6
static volatile uint16_t dbgGapHist[DBG_BUCKETS];
static inline uint8_t dbgBucket(uint32_t ms) {
  if (ms < 20) return 0;
  if (ms < 50) return 1;
  if (ms < 100) return 2;
  if (ms < 200) return 3;
  if (ms < 500) return 4;
  return 5;
}
static volatile int32_t  dbgRssiSum = 0;
static volatile uint32_t dbgRssiN = 0;
static volatile int8_t   dbgRssiMin = 0;
static uint32_t dbgLastRxMs = 0;
// Last telemetry payload from the transmitter, copied out of the RX callback.
static uint8_t dbgTele[40];
static volatile uint8_t dbgTeleLen = 0;
static volatile bool dbgTeleNew = false;

// Mirror of the transmitter's dbg_pkt_t (ino_cores3se.ino). If ver ever changes
// the receiver falls back to a hex dump, so it never needs reflashing for this.
#pragma pack(push, 1)
typedef struct {
  uint8_t  ver;                                  // 2
  uint16_t winMs;                                // measurement window, ms
  uint16_t usbHz, espHz, msHz;                   // counts over winMs
  uint16_t usbGapMax;                            // gap between HID reports, ms
  uint16_t qPeak;
  uint16_t failMouse, failKbd, failCmd;
  uint16_t txGapMax, sendMaxUs, usbDead;
  uint8_t  idle;
  uint32_t heap;
} dbg_pkt_t;
#pragma pack(pop)

#include <mbedtls/aes.h>

// AES-128 key for keyboard encryption (must match CoreS3 SE targets[].aesKey)
static const uint8_t AES_KEY[16] = {0x4B,0x56,0x4D,0x53,0x77,0x31,0x7A,0xDE,0xAD,0xBE,0xEF,0x42,0x13,0x37,0xCA,0xFE};

static void aesCtrDecrypt(const uint8_t *key, const uint8_t *in, uint8_t *out, int payloadLen) {
  // in[0..3] = counter, in[4..] = encrypted payload
  uint8_t block[16] = {0};
  memcpy(block, in, 4);
  uint8_t keystream[16];

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, block, keystream);
  mbedtls_aes_free(&ctx);

  for (int i = 0; i < payloadLen; i++) out[i] = in[4 + i] ^ keystream[i];
}

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 1) return;
  lastPacketMs = millis();

  // Telemetry frame: turns debug on here and is handed to loop() for printing.
  if (data[0] == PKT_DEBUG) {
    if (!dbgOn) dbgArm = true;   // loop() resets the window/gap references
    dbgOn = true;
    dbgLastTeleMs = millis();
    uint8_t n = (len - 1 > (int)sizeof(dbgTele)) ? (uint8_t)sizeof(dbgTele) : (uint8_t)(len - 1);
    memcpy(dbgTele, &data[1], n);
    dbgTeleLen = n;
    dbgTeleNew = true;
    return;
  }

  if (dbgOn) {
    uint32_t nw = millis();
    int8_t rs = (info && info->rx_ctrl) ? (int8_t)info->rx_ctrl->rssi : 0;
    dbgEspRecv++;
    dbgRssiSum += rs;
    if (dbgRssiN++ == 0 || rs < dbgRssiMin) dbgRssiMin = rs;
    if (dbgLastRxMs) {
      uint32_t gp = nw - dbgLastRxMs;
      if (gp > dbgRxGapMax) dbgRxGapMax = gp;
      dbgGapHist[dbgBucket(gp)]++;
    }
    dbgLastRxMs = nw;
    if (data[0] == PKT_MOUSE) dbgRxMouse++;
    else if (data[0] == PKT_ACTIVATE || data[0] == PKT_DEACTIVATE) dbgRxHb++;
  }

  switch (data[0]) {
    case PKT_ACTIVATE:
      pendingActivate = true;
      break;
    case PKT_DEACTIVATE:
      pendingDeactivate = true;
      break;
    case PKT_MOUSE:
      if (len >= 7 && deviceActive) {
        MousePkt p;
        p.btn = data[1];
        p.dx = (int16_t)(data[2] | (data[3] << 8));
        p.dy = (int16_t)(data[4] | (data[5] << 8));
        p.whl = (int8_t)data[6];
        xQueueSend(mouseQ, &p, 0);
      }
      break;
    case PKT_KEYBOARD:
      if (len >= 12) {
        uint8_t dec[7];
        aesCtrDecrypt(AES_KEY, &data[1], dec, 7);
        KbdPkt k;
        k.mod = dec[0];
        memcpy(k.keys, &dec[1], 6);
        xQueueSend(kbdQ, &k, 0);
      }
      break;
    case PKT_CONSUMER:
      if (len >= 7 && deviceActive) {
        uint8_t dec[2];
        aesCtrDecrypt(AES_KEY, &data[1], dec, 2);
        uint16_t usage = dec[0] | ((uint16_t)dec[1] << 8);
        xQueueSend(consumerQ, &usage, 0);
      }
      break;
    case PKT_APPLE_FN:
      if (len >= 6 && deviceActive) {
        uint8_t dec[1];
        aesCtrDecrypt(AES_KEY, &data[1], dec, 1);
        xQueueSend(appleFnQ, &dec[0], 0);
      }
      break;
    case PKT_HEARTBEAT:
      break;
  }
}

static inline bool ensureUsbAwake() {
  if (tud_suspended()) {
    tud_remote_wakeup();
    vTaskDelay(pdMS_TO_TICKS(2));
    return tud_mounted() && !tud_suspended();
  }
  return tud_mounted();
}

#define MOUSE_REPORT_ID    2
#define KBD_REPORT_ID      1
#define CONSUMER_REPORT_ID 4

static inline bool sendMouseDirect(const HIDMouseReport &rpt) {
  if (!tud_hid_n_ready(0)) return false;
  uint8_t rid = (tud_hid_n_get_protocol(0) == 0) ? 0 : MOUSE_REPORT_ID;
  return tud_hid_n_report(0, rid, &rpt, sizeof(rpt));
}

static inline bool sendKbdDirect(const KbdPkt &k) {
  if (!tud_hid_n_ready(0)) return false;
  uint8_t rid = (tud_hid_n_get_protocol(0) == 0) ? 0 : KBD_REPORT_ID;
  uint8_t report[8];
  report[0] = k.mod;
  report[1] = 0;
  memcpy(&report[2], k.keys, 6);
  return tud_hid_n_report(0, rid, report, 8);
}

static inline bool sendConsumerDirect(uint16_t usage) {
  if (!tud_hid_n_ready(0)) return false;
  uint8_t rid = (tud_hid_n_get_protocol(0) == 0) ? 0 : CONSUMER_REPORT_ID;
  return tud_hid_n_report(0, rid, &usage, sizeof(usage));
}

static inline bool sendAppleFnDirect(uint8_t state) {
  if (!tud_hid_n_ready(0)) return false;
  uint8_t report = state ? 0x01 : 0x00;
  return tud_hid_n_report(0, APPLE_FN_REPORT_ID, &report, 1);
}

void usbTask(void *pvParameters) {
  MousePkt p;
  KbdPkt k;
  uint16_t cu;
  uint8_t fn;
  HIDMouseReport rpt;
  bool hasPending = false;
  MousePkt pending;

  while (true) {
    if (hostSuspended) {
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
      xQueueReset(consumerQ);
      xQueueReset(appleFnQ);
      pendingActivate = false;
      pendingDeactivate = false;
      hasPending = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (pendingDeactivate) {
      pendingDeactivate = false;
      if (deviceActive) {
        deviceActive = false;
        UsbKbd.releaseAll();
        { HIDMouseReport rel = {}; sendMouseDirect(rel); }
        sendConsumerDirect(0);
        sendAppleFnDirect(0);
        prevBtn = 0;
        hasPending = false;
        xQueueReset(mouseQ);
        xQueueReset(kbdQ);
        xQueueReset(consumerQ);
        xQueueReset(appleFnQ);
      }
    }
    if (pendingActivate) {
      pendingActivate = false;
      if (!deviceActive) {
        xQueueReset(mouseQ);
        xQueueReset(kbdQ);
        xQueueReset(consumerQ);
        xQueueReset(appleFnQ);
        UsbKbd.releaseAll();
        { HIDMouseReport rel = {}; sendMouseDirect(rel); }
        sendConsumerDirect(0);
        sendAppleFnDirect(0);
        prevBtn = 0;
        hasPending = false;
        deviceActive = true;
        lastInputMs = millis();
      }
    }

    bool hasActivity = false;

    if (dbgOn) {
      uint8_t mq = uxQueueMessagesWaiting(mouseQ);
      uint8_t kq = uxQueueMessagesWaiting(kbdQ);
      if (mq > dbgMouseQPeak) dbgMouseQPeak = mq;
      if (kq > dbgKbdQPeak) dbgKbdQPeak = kq;
    }

    {
      while (deviceActive) {
        if (!tud_hid_n_ready(0)) {
          int w = 0;
          for (; w < 500 && !tud_hid_n_ready(0); w++)
            delayMicroseconds(10);
          if (dbgOn) {
            dbgBusyWaitSum += w;
            dbgBusyWaitCnt++;
            if ((uint32_t)w > dbgBusyWaitMax) dbgBusyWaitMax = w;
          }
          if (!tud_hid_n_ready(0)) break;
        }

        if (!ensureUsbAwake()) break;

        if (xQueueReceive(kbdQ, &k, 0) == pdTRUE) {
          sendKbdDirect(k);
          hasActivity = true;
          continue;
        }
        if (xQueueReceive(consumerQ, &cu, 0) == pdTRUE) {
          sendConsumerDirect(cu);
          hasActivity = true;
          continue;
        }
        if (xQueueReceive(appleFnQ, &fn, 0) == pdTRUE) {
          sendAppleFnDirect(fn);
          hasActivity = true;
          continue;
        }

        if (hasPending) {
          p = pending;
          hasPending = false;
        } else if (xQueueReceive(mouseQ, &p, 0) == pdTRUE) {
        } else {
          break;
        }

        if (uxQueueMessagesWaiting(mouseQ) > 64) {
          // Collapse the backlog into one report. Saturating adds: the fields
          // are int16/int8 and a long run of large deltas would otherwise wrap
          // and throw the cursor the opposite way.
          MousePkt extra;
          while (xQueueReceive(mouseQ, &extra, 0) == pdTRUE) {
            p.btn = extra.btn;
            int32_t nx = (int32_t)p.dx + extra.dx;
            int32_t ny = (int32_t)p.dy + extra.dy;
            int32_t nw = (int32_t)p.whl + extra.whl;
            p.dx  = (int16_t)(nx >  32767 ?  32767 : (nx < -32767 ? -32767 : nx));
            p.dy  = (int16_t)(ny >  32767 ?  32767 : (ny < -32767 ? -32767 : ny));
            p.whl = (int8_t) (nw >    127 ?    127 : (nw <   -127 ?   -127 : nw));
          }
          if (dbgOn) dbgMerges++;
        }

        rpt.buttons = p.btn;
        rpt.x = p.dx;
        rpt.y = p.dy;
        rpt.wheel = p.whl;
        rpt.pan = 0;

        if (!sendMouseDirect(rpt)) {
          pending = p;
          hasPending = true;
          break;
        }
        if (p.dx != 0 || p.dy != 0 || p.whl != 0 || p.btn != prevBtn)
          hasActivity = true;
        prevBtn = p.btn;
        if (dbgOn) dbgUsbSends++;
      }
    }
    if (hasActivity) lastInputMs = millis();


    if (!deviceActive) {
      vTaskDelay(100);
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
      xQueueReset(consumerQ);
      xQueueReset(appleFnQ);
    } else {
      uint32_t millisNow = millis() - lastInputMs;
      if (millisNow > USB_IDLE_TIMEOUT_MS_4) {
        vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_4));
      } else if (millisNow > USB_IDLE_TIMEOUT_MS_3) {
        vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_3));
      } else if (millisNow > USB_IDLE_TIMEOUT_MS_2) {
        vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_2));
      } else if (millisNow > USB_IDLE_TIMEOUT_MS_1) {
        vTaskDelay(pdMS_TO_TICKS(USB_IDLE_DELAY_MS_1));
      } else {
        // taskYIELD() only hands off to tasks of equal or higher priority, and
        // this task runs at priority 10 on the same core as loop() (priority 1,
        // LoopCore=1). Yielding alone therefore starved loop() completely while
        // the mouse was moving - measured at ~2 minutes without a single pass -
        // which stops all USB mount/suspend/replug handling. Give it a real
        // slot ~20x/sec; one tick costs at most a single mouse report.
        static uint32_t lastLoopSlotMs = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastLoopSlotMs >= 50) {
          lastLoopSlotMs = nowMs;
          vTaskDelay(1);
        } else {
          taskYIELD();
        }
      }
    }
  }
}

// Declares the allowed channel range so channels 12-13 are reachable, then
// sets the channel and records what actually took.
//
// Do NOT replace this with esp_wifi_set_country_code(). That call boot-loops
// the AtomS3U: the panic lands in esp_psram_check_ptr_addr() with the interrupt
// watchdog firing out of a spinlock, on a build with no PSRAM. It is harmless
// on the CoreS3 (PSRAM=enabled), which is why the fault looked one-sided. The
// older struct API below sets schan/nchan directly and never reaches that path.
static uint8_t espNowChannel = ESPNOW_CHAN;
static void applyEspNowChannel() {
  wifi_country_t c = {};
  c.cc[0] = ESPNOW_COUNTRY[0];
  c.cc[1] = ESPNOW_COUNTRY[1];
  c.cc[2] = 0;
  c.schan = 1;
  c.nchan = 13;
  c.max_tx_power = 20;
  c.policy = WIFI_COUNTRY_POLICY_MANUAL;
  esp_wifi_set_country(&c);
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
  // esp_wifi_set_channel() fails quietly if the range still forbids it, so keep
  // the channel that is actually in effect rather than the one we asked for.
  uint8_t got = 0;
  wifi_second_chan_t sec;
  if (esp_wifi_get_channel(&got, &sec) == ESP_OK) espNowChannel = got;
}

static void reinitEspNow() {
  esp_now_deinit();
  applyEspNowChannel();
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRecv);
  }
}

void setup() {
  bootMs = millis();

  UsbMouse.begin();
  UsbKbd.begin();
  UsbConsumer.begin();
  UsbAppleFn.begin();
  USB.usbAttributes(0xA0);
  USB.begin();

  mouseQ = xQueueCreate(128, sizeof(MousePkt));
  kbdQ = xQueueCreate(32, sizeof(KbdPkt));
  consumerQ = xQueueCreate(16, sizeof(uint16_t));
  appleFnQ = xQueueCreate(8, sizeof(uint8_t));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  applyEspNowChannel();
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRecv);
  }

  lastPacketMs = millis();
  dbgLastStatMs = millis();
  xTaskCreatePinnedToCore(usbTask, "USB", 4096, NULL, 10, NULL, 1);
}

// Prints one line of transmitter telemetry and one of receiver stats per second.
// Called from loop() so a slow CDC write can never stall the HID path.
static void dbgService() {
  // One-shot, printed as soon as a host opens the CDC port: confirms which
  // channel is really in effect without needing the transmitter to be running.
  static bool bootPrinted = false;
  if (!bootPrinted && Serial) {
    bootPrinted = true;
    wifi_country_t cc = {};
    esp_wifi_get_country(&cc);
    Serial.printf("BOOT wanted=%u actual=%u country=%c%c schan=%u nchan=%u\n",
                  ESPNOW_CHAN, espNowChannel, cc.cc[0], cc.cc[1], cc.schan, cc.nchan);
  }
  if (dbgOn && millis() - dbgLastTeleMs > DBG_TIMEOUT_MS) {
    dbgOn = false;            // transmitter left debug mode
    dbgLastRxMs = 0;
  }
  if (!dbgOn || !Serial) return;

  if (dbgArm) {          // first pass after debug came on: start a clean window
    dbgArm = false;
    dbgLastStatMs = millis();
    dbgLastRxMs = 0;
    dbgEspRecv = dbgRxMouse = dbgRxHb = 0;
    dbgUsbSends = dbgMerges = 0;
    dbgBusyWaitSum = dbgBusyWaitCnt = dbgBusyWaitMax = 0;
    dbgMouseQPeak = dbgKbdQPeak = 0;
    dbgRxGapMax = 0; dbgRssiSum = 0; dbgRssiN = 0;
    for (int i = 0; i < DBG_BUCKETS; i++) dbgGapHist[i] = 0;
    return;
  }

  if (dbgTeleNew) {
    dbgTeleNew = false;
    if (dbgTeleLen >= sizeof(dbg_pkt_t) && dbgTele[0] == 2) {
      dbg_pkt_t t;
      memcpy(&t, dbgTele, sizeof(t));
      // Counts, not rates - directly comparable with the RX line below.
      Serial.printf("TX win=%ums usb=%u esp=%u ms=%u q=%u fail=%u/%u/%u txgap=%ums usbgap=%ums send=%uus dead=%u idle=%u heap=%lu\n",
        t.winMs, t.usbHz, t.espHz, t.msHz, t.qPeak, t.failMouse, t.failKbd, t.failCmd,
        t.txGapMax, t.usbGapMax, t.sendMaxUs, t.usbDead, t.idle, (unsigned long)t.heap);
    } else {
      Serial.print("TX raw ");        // unknown format - dump it rather than guess
      for (uint8_t i = 0; i < dbgTeleLen; i++) Serial.printf("%02X", dbgTele[i]);
      Serial.println();
    }
  }

  uint32_t now = millis();
  if (now - dbgLastStatMs < 1000) return;
  uint32_t el = now - dbgLastStatMs;
  dbgLastStatMs = now;
  uint16_t avgW  = dbgBusyWaitCnt ? (uint16_t)((dbgBusyWaitSum * 10UL) / dbgBusyWaitCnt) : 0;
  int rssiAvg = dbgRssiN ? (int)(dbgRssiSum / (int32_t)dbgRssiN) : 0;
  // Raw counts plus the window they cover, so TX and RX lines can be compared
  // directly. Printing one side as a rate and the other as a count made the
  // loss figures meaningless.
  Serial.printf("RX win=%lums esp=%lu mouse=%lu hb=%lu gapmax=%lums rssi=%d/%d usb=%lu mrg=%lu w=%u/%lu q=%u,%u act=%d\n",
    (unsigned long)el, (unsigned long)dbgEspRecv, (unsigned long)dbgRxMouse,
    (unsigned long)dbgRxHb, (unsigned long)dbgRxGapMax,
    (int)dbgRssiMin, rssiAvg, (unsigned long)dbgUsbSends, (unsigned long)dbgMerges,
    avgW, (unsigned long)(dbgBusyWaitMax * 10),
    dbgMouseQPeak, dbgKbdQPeak, deviceActive ? 1 : 0);
  Serial.printf("   gaps <20=%u 20-49=%u 50-99=%u 100-199=%u 200-499=%u 500+=%u\n",
    dbgGapHist[0], dbgGapHist[1], dbgGapHist[2], dbgGapHist[3], dbgGapHist[4], dbgGapHist[5]);

  dbgUsbSends = dbgEspRecv = dbgMerges = 0;
  dbgRxMouse = dbgRxHb = 0;
  dbgBusyWaitSum = dbgBusyWaitCnt = dbgBusyWaitMax = 0;
  dbgMouseQPeak = dbgKbdQPeak = 0;
  dbgRxGapMax = 0; dbgRssiSum = 0; dbgRssiN = 0;
  for (int i = 0; i < DBG_BUCKETS; i++) dbgGapHist[i] = 0;
}

void loop() {
  dbgService();
  uint32_t now = millis();
  bool grace = (now - bootMs) < BOOT_GRACE_MS;
  bool mounted = tud_mounted();
  bool suspended = tud_suspended();

  // USB bus lost (host powered off or wake transition)
  if (!mounted && !suspended && !grace) {
    hostSuspended = false;

    if (wasSuspended) {
      if (!wakeReplugDone) {
        // Confirmed sleep → immediate replug + reinit
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
        tud_connect();
        reinitEspNow();
        if (wasActiveBeforeSuspend) {
          deviceActive = false;
          pendingActivate = true;  // let usbTask do proper HID init
        }
        wakeReplugDone = true;
        lastPacketMs = now;
        delay(200);
        return;
      }
      // Already replugged — wait for Mac to enumerate
      lastPacketMs = now;
      delay(50);
      return;
    }

    if (suspendStartMs != 0) {
      // Brief bounce — restore without replug or reinit
      if (wasActiveBeforeSuspend) {
        deviceActive = false;
        pendingActivate = true;
      }
      wasActiveBeforeSuspend = false;
      suspendStartMs = 0;
      lastPacketMs = now;
      delay(50);
      return;
    }

    // Truly gone — not a wake transition
    if (deviceActive) {
      deviceActive = false;
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
      xQueueReset(consumerQ);
      xQueueReset(appleFnQ);
    }
    lastPacketMs = now;

    if (usbGoneSinceMs == 0) {
      usbGoneSinceMs = now;
    } else if (now - usbGoneSinceMs > USB_GONE_REPLUG_MS) {
      tud_disconnect();
      vTaskDelay(pdMS_TO_TICKS(500));
      tud_connect();
      usbGoneSinceMs = now;
    }

    delay(200);
    return;
  }

  // USB suspended by host (skip during boot grace — enumeration can look like suspend)
  if (suspended && !grace) {
    hostSuspended = true;
    if (suspendStartMs == 0) {
      suspendStartMs = now;
      wasActiveBeforeSuspend = deviceActive;
    }
    if (now - suspendStartMs >= SLEEP_SETTLE_MS) {
      wasSuspended = true;
    }
    if (deviceActive) {
      deviceActive = false;
      xQueueReset(mouseQ);
      xQueueReset(kbdQ);
      xQueueReset(consumerQ);
      xQueueReset(appleFnQ);
    }
    lastPacketMs = now;
    usbGoneSinceMs = 0;
    delay(500);
    return;
  }

  // Normal operation — mounted and not suspended
  hostSuspended = false;
  usbGoneSinceMs = 0;

  if (wasSuspended) {
    if (!wakeReplugDone) {
      // Woke up with USB still mounted — reinit ESP-NOW
      reinitEspNow();
    }
    if (wasActiveBeforeSuspend && !deviceActive) {
      pendingActivate = true;  // let usbTask do proper HID init
    }
    wasSuspended = false;
    wakeReplugDone = false;
    wasActiveBeforeSuspend = false;
    suspendStartMs = 0;
  } else if (suspendStartMs != 0) {
    // Brief bounce — restore without reinit
    suspendStartMs = 0;
    if (wasActiveBeforeSuspend) {
      deviceActive = false;
      pendingActivate = true;  // let usbTask do proper HID init
    }
    wasActiveBeforeSuspend = false;
  }

  if (!grace && (now - lastPacketMs > ESPNOW_TIMEOUT_MS)) {
    deviceActive = false;
    xQueueReset(mouseQ);
    xQueueReset(kbdQ);
    xQueueReset(consumerQ);
    xQueueReset(appleFnQ);

    reinitEspNow();
    lastPacketMs = now;
  }

  delay(100);
}