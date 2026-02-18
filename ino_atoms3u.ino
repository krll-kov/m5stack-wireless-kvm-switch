#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "USBCDC.h"
#include "freertos/queue.h"

#define DEBUG_MODE false

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
#if DEBUG_MODE
bool tud_cdc_n_write_flush(uint8_t itf);
#endif
}

#define ESPNOW_CHAN 1

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT 0x05
#define PKT_CONSUMER 0x06
#define PKT_APPLE_FN 0x07

#define BOOT_GRACE_MS       5000
#define ESPNOW_TIMEOUT_MS   3400
#define USB_GONE_REPLUG_MS  3000
#define SLEEP_SETTLE_MS     3000

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
#if DEBUG_MODE
USBCDC DbgSerial;
#endif

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

#if DEBUG_MODE
static volatile uint32_t dbgEspRecv = 0;
static uint32_t dbgLastStatMs = 0;
static volatile uint32_t dbgUsbSends = 0;
static volatile uint32_t dbgMerges = 0;
static volatile uint32_t dbgBusyWaitSum = 0;
static volatile uint32_t dbgBusyWaitCnt = 0;
static volatile uint32_t dbgBusyWaitMax = 0;
static volatile uint8_t  dbgMouseQPeak = 0;
static volatile uint8_t  dbgKbdQPeak = 0;
#endif

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
#if DEBUG_MODE
  dbgEspRecv++;
#endif

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
        UsbKbd.releaseAll();
        { HIDMouseReport rel = {}; sendMouseDirect(rel); }
        prevBtn = 0;
        hasPending = false;
        deviceActive = true;
        lastInputMs = millis();
      }
    }

    bool hasActivity = false;

#if DEBUG_MODE
    { uint8_t mq = uxQueueMessagesWaiting(mouseQ);
      uint8_t kq = uxQueueMessagesWaiting(kbdQ);
      if (mq > dbgMouseQPeak) dbgMouseQPeak = mq;
      if (kq > dbgKbdQPeak) dbgKbdQPeak = kq; }
#endif

    {
      while (deviceActive) {
        if (!tud_hid_n_ready(0)) {
          int w = 0;
          for (; w < 500 && !tud_hid_n_ready(0); w++)
            delayMicroseconds(10);
#if DEBUG_MODE
          dbgBusyWaitSum += w;
          dbgBusyWaitCnt++;
          if ((uint32_t)w > dbgBusyWaitMax) dbgBusyWaitMax = w;
#endif
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
          MousePkt extra;
          while (xQueueReceive(mouseQ, &extra, 0) == pdTRUE) {
            p.btn = extra.btn;
            p.dx += extra.dx;
            p.dy += extra.dy;
            p.whl += extra.whl;
          }
#if DEBUG_MODE
          dbgMerges++;
#endif
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
#if DEBUG_MODE
        dbgUsbSends++;
#endif
      }
    }
    if (hasActivity) lastInputMs = millis();

#if DEBUG_MODE
    {
      uint32_t now = millis();
      if (now - dbgLastStatMs >= 1000) {
        uint32_t elapsed = now - dbgLastStatMs;
        dbgLastStatMs = now;
        uint16_t usbHz  = elapsed ? (uint16_t)(dbgUsbSends * 1000UL / elapsed) : 0;
        uint16_t espHz  = elapsed ? (uint16_t)(dbgEspRecv  * 1000UL / elapsed) : 0;
        uint16_t mrgHz  = elapsed ? (uint16_t)(dbgMerges   * 1000UL / elapsed) : 0;
        uint16_t avgW   = dbgBusyWaitCnt ? (uint16_t)((dbgBusyWaitSum * 10UL) / dbgBusyWaitCnt) : 0;
        uint16_t maxW   = (uint16_t)(dbgBusyWaitMax * 10);
        DbgSerial.printf("usb:%d esp:%d mrg:%d w:%d/%d q:%d,%d act:%d\n",
          usbHz, espHz, mrgHz, avgW, maxW,
          dbgMouseQPeak, dbgKbdQPeak, deviceActive ? 1 : 0);
        tud_cdc_n_write_flush(0);
        dbgUsbSends = 0; dbgEspRecv = 0; dbgMerges = 0;
        dbgBusyWaitSum = 0; dbgBusyWaitCnt = 0; dbgBusyWaitMax = 0;
        dbgMouseQPeak = 0; dbgKbdQPeak = 0;
      }
    }
#endif

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
        taskYIELD();
      }
    }
  }
}

static void reinitEspNow() {
  esp_now_deinit();
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
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
#if DEBUG_MODE
  DbgSerial.begin();
#endif
  USB.usbAttributes(0xA0);
  USB.begin();

  mouseQ = xQueueCreate(128, sizeof(MousePkt));
  kbdQ = xQueueCreate(32, sizeof(KbdPkt));
  consumerQ = xQueueCreate(16, sizeof(uint16_t));
  appleFnQ = xQueueCreate(8, sizeof(uint8_t));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRecv);
  }

  lastPacketMs = millis();
#if DEBUG_MODE
  dbgLastStatMs = millis();
#endif
  xTaskCreatePinnedToCore(usbTask, "USB", 4096, NULL, 10, NULL, 1);
}

void loop() {
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
          deviceActive = true;
          lastInputMs = now;
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
        deviceActive = true;
        lastInputMs = now;
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
      deviceActive = true;
      lastInputMs = millis();
    }
    wasSuspended = false;
    wakeReplugDone = false;
    wasActiveBeforeSuspend = false;
    suspendStartMs = 0;
  } else if (suspendStartMs != 0) {
    // Brief bounce — restore without reinit
    suspendStartMs = 0;
    if (wasActiveBeforeSuspend) {
      deviceActive = true;
      lastInputMs = millis();
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