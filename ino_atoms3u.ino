#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"
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

// XOR key for keyboard payload obfuscation (must match CoreS3 SE)
static const uint8_t KBD_XOR[7] = {0x4B,0x56,0x4D,0x53,0x77,0x31,0x7A};

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT 0x05
#define PKT_CONSUMER 0x06

// ── Watchdog thresholds ──
#define BOOT_GRACE_MS 5000      // USB enumeration grace period after boot
#define ESPNOW_TIMEOUT_MS 3400  // no packets from CoreS3 → esp_now_deinit

static uint32_t usbGoneSinceMs = 0;
#define USB_GONE_REPLUG_MS 30000

#define USB_IDLE_TIMEOUT_MS_4 900000
#define USB_IDLE_DELAY_MS_4   100
#define USB_IDLE_TIMEOUT_MS_3 300000
#define USB_IDLE_DELAY_MS_3   50
#define USB_IDLE_TIMEOUT_MS_2 60000
#define USB_IDLE_DELAY_MS_2   20
#define USB_IDLE_TIMEOUT_MS_1 10000
#define USB_IDLE_DELAY_MS_1  1

USBHIDKeyboard UsbKbd;
USBHIDMouse UsbMouse;
USBHIDConsumerControl UsbConsumer;

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
      if (len >= 8) {
        uint8_t dec[7];
        for (int i = 0; i < 7; i++) dec[i] = data[1 + i] ^ KBD_XOR[i];
        KbdPkt k;
        k.mod = dec[0];
        memcpy(k.keys, &dec[1], 6);
        xQueueSend(kbdQ, &k, 0);
      }
      break;
    case PKT_CONSUMER:
      if (len >= 3 && deviceActive) {
        uint16_t usage = data[1] | ((uint16_t)data[2] << 8);
        if (usage)
          UsbConsumer.press(usage);
        else
          UsbConsumer.release();
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
        UsbConsumer.release();
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

void setup() {
  bootMs = millis();

  UsbMouse.begin();
  UsbKbd.begin();
  UsbConsumer.begin();
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