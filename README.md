# m5stack-wireless-kvm-switch
Wireless keyboard and mouse kvm switch (both IN and OUT) based on M5Stack CoreS3 SE and Atom S3U that can handle 1000 Hz wireless mouse and blueooth keyboard

Setup guide (I'm using MacOS so it will be for Mac), but can be used on other OS, just download files for your OS:
- Install Arduino IDE: https://www.arduino.cc/en/software
- Insert Atom S3U (usb thing that looks like a flash drive) into MAC 
- Install USB Driver: https://docs.m5stack.com/en/download (I used pkg, did not reboot the system, just opened installer, clicked yes according to guide in repo)
<img width="1227" height="566" alt="image" src="https://github.com/user-attachments/assets/c5f534b6-a41b-49d2-b936-1a9b752347ee" />


- Open Arduino IDE and go to Tools -> Ports, check if /dev/cu.usbmodem... location exists (if yes - driver is working, if no - you did not install driver from step 2)
<img width="805" height="362" alt="image" src="https://github.com/user-attachments/assets/e313367a-efe7-4d20-92ee-d5eebb17751c" />


- In Arduino go to settings -> Additional boards manager URLs -> paste this url there: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
<img width="886" height="531" alt="image" src="https://github.com/user-attachments/assets/2bed3b3b-b131-4c99-9587-f28b29754c4f" />


- Click Tools -> Board -> Boards Manager, input "M5Stack" and install "M5Stack by M5Stack", also install esp32 by Espressif System 
<img width="347" height="420" alt="image" src="https://github.com/user-attachments/assets/64eaae41-0942-4eb1-9433-b4c6b0c3464f" />
<img width="416" height="212" alt="image" src="https://github.com/user-attachments/assets/f2fe4450-c7dd-468c-9c8c-9e0df3674858" />



- Click Tools -> Board -> M5Stack, select M5AtomS3
<img width="972" height="912" alt="image" src="https://github.com/user-attachments/assets/4be5d35f-2a74-4b5a-885a-4ad44b395c0a" />


- Click Tools -> Manage Libraries, input "M5Unified" and "NimBLE-Arduino" and install them
<img width="828" height="788" alt="image" src="https://github.com/user-attachments/assets/e6a6d9ba-8ea7-4ac3-9e12-511c020b295e" />


- Go to sketch menu and paste this into it:
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

- Go to Sketch, click Upload, wait for it to upload
<img width="1513" height="1117" alt="image" src="https://github.com/user-attachments/assets/d8a80515-bc94-4b97-935c-26dea4b829ad" />


- Click Tools -> Serial Monitor, copy mac address from filed below (For me it's 3C:DC:75:ED:FB:4C), save it somewhere
<img width="948" height="1110" alt="image" src="https://github.com/user-attachments/assets/e59a0336-30f1-411f-8348-48dc098f92f5" />


- Now insert the second M5AtomS3 into mac (instead of first one) and repeat same steps starting from "sketch upload", my second mac address is D0:CF:13:0F:90:48, also save it somewhere

- Change your sketch to this one and upload it to both "Atom S3U" devices, but this time change the usb mode to USB-OTG(TinyUSB)
<img width="848" height="169" alt="image" src="https://github.com/user-attachments/assets/f418d42b-d1da-4661-996c-152d55c2fdb7" />

```ino
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "freertos/queue.h"

#define ESPNOW_CHAN 1

#define PKT_MOUSE      0x01
#define PKT_KEYBOARD   0x02
#define PKT_ACTIVATE   0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT  0x05

USBHIDKeyboard UsbKbd;
USBHIDMouse    UsbMouse;

#pragma pack(push, 1)
struct MousePkt {
  uint8_t btn;
  int16_t dx;
  int16_t dy;
  int8_t  whl;
};

struct KbdPkt {
  uint8_t mod;
  uint8_t keys[6];
};
#pragma pack(pop)

QueueHandle_t mouseQ;
QueueHandle_t kbdQ;

volatile bool deviceActive   = false;
volatile bool pendingActivate   = false;
volatile bool pendingDeactivate = false;
static uint8_t prevBtn = 0;

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 1) return;

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
        p.dx  = (int16_t)(data[2] | (data[3] << 8));
        p.dy  = (int16_t)(data[4] | (data[5] << 8));
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

void setup() {
  UsbMouse.begin();
  UsbKbd.begin();
  USB.begin();

  mouseQ = xQueueCreate(128, sizeof(MousePkt));
  kbdQ   = xQueueCreate(32, sizeof(KbdPkt));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onRecv);
  }
}

void loop() {
  // handle state changes HERE, same thread as USB operations
  if (pendingDeactivate) {
    pendingDeactivate = false;
    if (deviceActive) {  
      deviceActive = false;
      UsbKbd.releaseAll();
      UsbMouse.release(MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
      prevBtn = 0;

      MousePkt dm;
      KbdPkt   dk;
      while (xQueueReceive(mouseQ, &dm, 0) == pdTRUE) {}
      while (xQueueReceive(kbdQ,   &dk, 0) == pdTRUE) {}
    }
  }

  if (pendingActivate) {
    pendingActivate = false;
    if (!deviceActive) {
      UsbKbd.releaseAll();
      UsbMouse.release(MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
      prevBtn = 0;
      deviceActive = true;
    }
  }

  // process mouse
  MousePkt p;
  while (deviceActive && xQueueReceive(mouseQ, &p, 0) == pdTRUE) {
    if (p.btn != prevBtn) {
      uint8_t chg = p.btn ^ prevBtn;
      if (chg & 0x01) { (p.btn & 0x01) ? UsbMouse.press(MOUSE_LEFT)   : UsbMouse.release(MOUSE_LEFT);   }
      if (chg & 0x02) { (p.btn & 0x02) ? UsbMouse.press(MOUSE_RIGHT)  : UsbMouse.release(MOUSE_RIGHT);  }
      if (chg & 0x04) { (p.btn & 0x04) ? UsbMouse.press(MOUSE_MIDDLE) : UsbMouse.release(MOUSE_MIDDLE); }
      prevBtn = p.btn;
    }

    int16_t rx = p.dx;
    int16_t ry = p.dy;
    int8_t  rw = p.whl;

    while (rx != 0 || ry != 0 || rw != 0) {
      int8_t sx = (int8_t)constrain(rx, -127, 127);
      int8_t sy = (int8_t)constrain(ry, -127, 127);
      UsbMouse.move(sx, sy, rw);
      rx -= sx;
      ry -= sy;
      rw  = 0;
    }
  }

  // process keyboard
  KbdPkt k;
  while (deviceActive && xQueueReceive(kbdQ, &k, 0) == pdTRUE) {
    KeyReport rpt;
    rpt.modifiers = k.mod;
    rpt.reserved  = 0;
    memcpy(rpt.keys, k.keys, 6);
    UsbKbd.sendReport(&rpt);
  }

  // drain when inactive
  if (!deviceActive) {
    MousePkt dm;
    KbdPkt   dk;
    while (xQueueReceive(mouseQ, &dm, 0) == pdTRUE) {}
    while (xQueueReceive(kbdQ,   &dk, 0) == pdTRUE) {}
  }

  delay(1);
}
```

- Most likely you'll see this window and error "Port monitor error: command 'open' failed: no such file or directory. Could not connect to /dev/cu.usbmodem1101 serial port." in Arduino - that's okay, it means everything works properly (If you'll neet to change Sketch in future - Press and hold reset button in AtomS3U, insert it into usb port (you still have to press that button), wait for 1 second, unpress the button, now you'll see port selection again in Arduino) 
<img width="535" height="193" alt="image" src="https://github.com/user-attachments/assets/78412161-a1da-4c09-9c98-0fd6c2aa9a8c" />
<img width="977" height="678" alt="image" src="https://github.com/user-attachments/assets/aeb8694a-97b2-4cb4-babc-9a8a97992d04" />


- Now connect CoreS3 SE to your macbook, go to Board -> M5Stack -> M5CoreS3 
<img width="909" height="1104" alt="image" src="https://github.com/user-attachments/assets/31de6870-00b4-46dc-8fb6-66f62e7f2c45" />

- Paste this into sketch; You must change these values:
uint8_t ATOM_MAC_1[] = {0x3C, 0xDC, 0x75, 0xED, 0xFB, 0x4C}; - mac address of first S3U dongle
uint8_t ATOM_MAC_2[] = {0xD0, 0xCF, 0x13, 0x0F, 0x90, 0x48}; - mac address of second S3U dongle
#define BLE_KBD_MATCH      "Magic Keyboard" - Part of keyboard name from bluetooth settings (do not use ' or other symbols, try simple search). For example i have Kyrylo's Magic Keyboard - I'm using just Magic Keyboard to look for keyboard.
MOUSE_SEND_HZ     750 - Hz of mouse (1000, 500, 750, etc). Lower this if you experiece lags or set to 1000hz if signal is good in your room
bool m4 = (m.buttons & 0x08); - this sets switch button to Mouse4 
```ino
#include <Arduino.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

uint8_t ATOM_MAC_1[] = {0x3C, 0xDC, 0x75, 0xED, 0xFB, 0x4C};
uint8_t ATOM_MAC_2[] = {0xD0, 0xCF, 0x13, 0x0F, 0x90, 0x48};

#define BLE_KBD_MATCH "Keyboard"
#define ESPNOW_CHAN 1
#define MOUSE_SEND_HZ 750
#define MOUSE_SEND_US (1000000 / MOUSE_SEND_HZ)
#define SWITCH_DEBOUNCE_MS 300
#define HEARTBEAT_MS 500
#define BLE_SCAN_BUDGET_MS 20000
#define SEEN_DEV_MAX 32
#define SEEN_NAME_LEN 40
#define BLE_PROBE_MAX 8
#define BLE_PROBE_MIN_RSSI -80
#define TOP_DISPLAY 12
#define PASSKEY_DISPLAY_MS 30000
#define APPLE_PAIR_TIMEOUT_MS 20000
#define MAX_CONNECT_RETRIES 5

#define PKT_MOUSE 0x01
#define PKT_KEYBOARD 0x02
#define PKT_ACTIVATE 0x03
#define PKT_DEACTIVATE 0x04
#define PKT_HEARTBEAT 0x05

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
static int activeTarget = 0;
static bool mouse4Held = false;
static volatile bool usbMouseReady = false;
static volatile bool bleKbdConnected = false;
static volatile int bleState = 0;
static volatile uint32_t scanElapsedMs = 0;
static char bleFoundName[64] = "";
static char bleStatusMsg[64] = "";
static esp_now_peer_info_t peer1 = {}, peer2 = {};
static mouse_evt_t mAccum = {};
static bool mDirty = false;
static uint32_t mLastSendUs = 0, lastSwitchMs = 0;
static bool needRedraw = false;
static volatile int bleMatchReason = 0;
static volatile bool passkeyActive = false;
static volatile uint32_t passkeyValue = 0, passkeyShownMs = 0;
static volatile bool pairingSuccess = false, pairingDone = false;
static volatile bool bleScanRequested = false;

struct seen_dev_t {
  NimBLEAddress addr;
  char name[SEEN_NAME_LEN];
  int rssi;
  bool used, hasHidUuid, isApple;
};
static seen_dev_t seenDevs[SEEN_DEV_MAX];
static volatile int seenDevCount = 0, bleScanTotal = 0;

static char probeLog[BLE_PROBE_MAX * 2][52];
static volatile int probeLogCount = 0;
static volatile int probeTotalExpected = 0;

static void resetSeenDevs() {
  for (int i = 0; i < SEEN_DEV_MAX; i++) {
    seenDevs[i].used = false;
    seenDevs[i].isApple = false;
  }
  seenDevCount = 0;
  bleScanTotal = 0;
}
static int findSeenDev(const NimBLEAddress &addr) {
  for (int i = 0; i < seenDevCount; i++)
    if (seenDevs[i].used && seenDevs[i].addr == addr) return i;
  return -1;
}
static int addSeenDev(const NimBLEAddress &addr, const char *name, int rssi,
                      bool hasHid, bool apple = false) {
  if (seenDevCount >= SEEN_DEV_MAX) return -1;
  int i = seenDevCount++;
  seenDevs[i].addr = addr;
  seenDevs[i].rssi = rssi;
  seenDevs[i].used = true;
  seenDevs[i].hasHidUuid = hasHid;
  seenDevs[i].isApple = apple;
  if (name && name[0]) {
    strncpy(seenDevs[i].name, name, SEEN_NAME_LEN - 1);
    seenDevs[i].name[SEEN_NAME_LEN - 1] = '\0';
  } else
    seenDevs[i].name[0] = '\0';
  return i;
}
static int getTopByRssi(int *out, int maxOut) {
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
  return c;
}

// ===================== ESP-NOW =====================
static volatile int sendAckResult = -1;
static void onEspNowSendDone(const uint8_t *mac, esp_now_send_status_t s) {
  sendAckResult = (s == ESP_NOW_SEND_SUCCESS) ? 1 : 0;
}
static void sendRaw(uint8_t *mac, const uint8_t *data, size_t len) {
  esp_now_send(mac, data, len);
}
static uint8_t *activeMac() {
  return (activeTarget == 0) ? ATOM_MAC_1 : ATOM_MAC_2;
}
static uint8_t *inactiveMac() {
  return (activeTarget == 0) ? ATOM_MAC_2 : ATOM_MAC_1;
}
static void sendCmd(uint8_t *mac, uint8_t cmd) {
  uint8_t p = cmd;
  sendRaw(mac, &p, 1);
}
static bool sendCmdConfirmed(uint8_t *mac, uint8_t cmd) {
  uint8_t p = cmd;
  for (int i = 0; i < 3; i++) {
    sendAckResult = -1;
    if (esp_now_send(mac, &p, 1) != ESP_OK) {
      delay(3);
      continue;
    }
    uint32_t t = millis();
    while (sendAckResult == -1 && millis() - t < 30) delay(1);
    if (sendAckResult == 1) return true;
    delay(3);
  }
  return false;
}
static void switchTarget() {
  mAccum = {};
  mDirty = false;
  xQueueReset(mouseQueue);
  delay(15);
  sendCmdConfirmed(activeMac(), PKT_DEACTIVATE);
  activeTarget = 1 - activeTarget;
  sendCmdConfirmed(activeMac(), PKT_ACTIVATE);
  xQueueReset(mouseQueue);
  lastSwitchMs = millis();
  needRedraw = true;
}
static void txMouse(const mouse_evt_t *m) {
  uint8_t p[7];
  p[0] = PKT_MOUSE;
  p[1] = m->buttons & 0x07;
  p[2] = m->x & 0xFF;
  p[3] = (m->x >> 8) & 0xFF;
  p[4] = m->y & 0xFF;
  p[5] = (m->y >> 8) & 0xFF;
  p[6] = (uint8_t)m->wheel;
  sendRaw(activeMac(), p, 7);
}
static void txKbd(const kbd_evt_t *k) {
  uint8_t p[8];
  p[0] = PKT_KEYBOARD;
  p[1] = k->modifiers;
  memcpy(&p[2], k->keys, 6);
  sendRaw(activeMac(), p, 8);
}

// ===================== USB HOST =====================
static usb_host_client_handle_t usbClient = NULL;
static usb_device_handle_t usbDev = NULL;
static usb_transfer_t *xferIn = NULL;
static uint8_t epAddr = 0, hidIfNum = 0;
static volatile bool devGone = false, xferDead = false;
static uint32_t xferDeadMs = 0;

static void mouseXferCb(usb_transfer_t *xfer) {
  if (xfer->status == USB_TRANSFER_STATUS_COMPLETED &&
      xfer->actual_num_bytes > 0) {
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
  if (xfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      xfer->status == USB_TRANSFER_STATUS_CANCELED) {
    xferDead = true;
    return;
  }
  if (usbDev && xferIn) {
    if (usb_host_transfer_submit(xferIn) != ESP_OK) xferDead = true;
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
  if (xferIn) {
    usb_host_transfer_free(xferIn);
    xferIn = NULL;
  }
  if (usbDev) {
    if (epAddr) usb_host_interface_release(usbClient, usbDev, hidIfNum);
    usb_host_device_close(usbClient, usbDev);
    usbDev = NULL;
  }
  epAddr = 0;
  usbMouseReady = false;
  xferDead = false;
  xferDeadMs = 0;
  usb_host_lib_handle_events(10, NULL);
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
    uint8_t dL = p[off], dT = p[off + 1];
    if (dL == 0) break;
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
        if (usb_host_transfer_alloc(mps, 0, &xferIn) != ESP_OK) return false;
        xferIn->device_handle = usbDev;
        xferIn->bEndpointAddress = a;
        xferIn->callback = mouseXferCb;
        xferIn->num_bytes = mps;
        xferIn->timeout_ms = 0;
        if (usb_host_transfer_submit(xferIn) != ESP_OK) {
          usb_host_transfer_free(xferIn);
          xferIn = NULL;
          return false;
        }
        epAddr = a;
        xferDead = false;
        xferDeadMs = 0;
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
  hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
  usb_host_install(&hc);
  usb_host_client_config_t cc = {};
  cc.is_synchronous = false;
  cc.max_num_event_msg = 5;
  cc.async.client_event_callback = usbEventCb;
  cc.async.callback_arg = NULL;
  usb_host_client_register(&cc, &usbClient);
  while (true) {
    usb_host_lib_handle_events(1, NULL);
    usb_host_client_handle_events(usbClient, 0);
    if (devGone) {
      devGone = false;
      cleanupUsb();
      needRedraw = true;
    }
    if (xferDead && usbDev && xferIn) {
      if (xferDeadMs == 0) xferDeadMs = millis();
      if (millis() - xferDeadMs > 3000) {
        cleanupUsb();
        needRedraw = true;
      } else {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (usb_host_transfer_submit(xferIn) == ESP_OK) {
          xferDead = false;
          xferDeadMs = 0;
        }
      }
    }
    if (usbDev && !epAddr) {
      vTaskDelay(pdMS_TO_TICKS(200));
      if (findHidEp()) {
        usbMouseReady = true;
        needRedraw = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ===================== BLE SECURITY =====================
class SecurityCallbacks : public NimBLEClientCallbacks {
  void onPassKeyEntry(NimBLEConnInfo &ci) override {
    uint32_t pk = esp_random() % 1000000;
    Serial.printf("[SEC] passkey=%06d\n", pk);
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
    Serial.printf("[SEC] auth: %s\n", pairingSuccess ? "OK" : "FAIL");
    needRedraw = true;
  }
  void onDisconnect(NimBLEClient *, int reason) override {
    Serial.printf("[SEC] disconnect %d\n", reason);
  }
};
static SecurityCallbacks secCb;
static NimBLEAddress bleAddr;
static volatile bool bleFound = false, bleScanDone = false;
static uint32_t lastScanRedraw = 0;

static void setProbeSecurityMode() {
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}
static void setKeyboardSecurityMode() {
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
}

static bool nameContains(const std::string &hay, const char *needle) {
  if (hay.empty()) return false;
  std::string h = hay, n = needle;
  for (auto &c : h) c = tolower(c);
  for (auto &c : n) c = tolower(c);
  return h.find(n) != std::string::npos;
}

// ===== wait for disconnect to actually complete =====
static void waitDisconnect(NimBLEClient *c, int timeoutMs = 2000) {
  if (!c->isConnected()) return;
  c->disconnect();
  uint32_t t0 = millis();
  while (c->isConnected() && (int)(millis() - t0) < timeoutMs)
    vTaskDelay(pdMS_TO_TICKS(50));
  vTaskDelay(pdMS_TO_TICKS(200));  // controller settle
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    bleScanTotal++;
    NimBLEAddress addr = dev->getAddress();
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
    int idx = findSeenDev(addr);
    if (idx == -1) {
      idx = addSeenDev(addr, name.c_str(), rssi, hasHid, isApple);
    } else {
      if (name.length() > 0 && seenDevs[idx].name[0] == '\0') {
        strncpy(seenDevs[idx].name, name.c_str(), SEEN_NAME_LEN - 1);
        seenDevs[idx].name[SEEN_NAME_LEN - 1] = '\0';
      }
      if (isApple) seenDevs[idx].isApple = true;
      if (hasHid) seenDevs[idx].hasHidUuid = true;
      if (rssi > seenDevs[idx].rssi) seenDevs[idx].rssi = rssi;
    }
    if (nameContains(name, BLE_KBD_MATCH)) {
      strncpy(bleFoundName, name.c_str(), sizeof(bleFoundName) - 1);
      bleFoundName[sizeof(bleFoundName) - 1] = '\0';
      bleAddr = addr;
      bleFound = true;
      bleMatchReason = 1;
      bleState = 2;
      needRedraw = true;
      NimBLEDevice::getScan()->stop();
      return;
    }
    if (hasHid) {
      const char *l = name.empty() ? "(HID no name)" : name.c_str();
      strncpy(bleFoundName, l, sizeof(bleFoundName) - 1);
      bleAddr = addr;
      bleFound = true;
      bleMatchReason = 2;
      bleState = 2;
      needRedraw = true;
      NimBLEDevice::getScan()->stop();
      return;
    }
    if (millis() - lastScanRedraw > 500) {
      needRedraw = true;
      lastScanRedraw = millis();
    }
  }
  void onScanEnd(const NimBLEScanResults &, int) override {
    bleScanDone = true;
  }
};
static ScanCb scanCbInstance;

static void bleReportCb(NimBLERemoteCharacteristic *, uint8_t *data, size_t len,
                        bool) {
  if (len < 8) return;
  kbd_evt_t evt;
  evt.modifiers = data[0];
  memcpy(evt.keys, &data[2], 6);
  xQueueSend(kbdQueue, &evt, 0);
}

// ===== Phase 2: quick probe, REUSES client =====
// returns -1=fail, 0=no HID, 1=HID
static int simpleProbeHid(NimBLEClient *c, const NimBLEAddress &addr,
                          char *outName, size_t nameLen) {
  outName[0] = '\0';
  int result = -1;
  if (c->connect(addr)) {
    result = 0;
    if (c->isConnected()) {
      NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
      if (gap && c->isConnected()) {
        NimBLERemoteCharacteristic *nm =
            gap->getCharacteristic(NimBLEUUID("2A00"));
        if (nm && nm->canRead() && c->isConnected()) {
          std::string gn = nm->readValue();
          if (!gn.empty()) {
            strncpy(outName, gn.c_str(), nameLen - 1);
            outName[nameLen - 1] = '\0';
          }
        }
      }
      if (c->isConnected()) {
        NimBLERemoteService *hid = c->getService(NimBLEUUID("1812"));
        if (hid) result = 1;
      }
    }
    waitDisconnect(c);
  }
  return result;
}

static void doQuickProbe(NimBLEClient *c, int seenIdx, int probeNum,
                         int total) {
  seen_dev_t &dev = seenDevs[seenIdx];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "probe %d/%d rssi=%d", probeNum,
           total, dev.rssi);
  bleState = 6;
  needRedraw = true;
  Serial.printf("[BLE] probe %d/%d: %s rssi=%d heap=%d\n", probeNum, total,
                dev.addr.toString().c_str(), dev.rssi,
                esp_get_free_heap_size());

  int logIdx = -1;
  if (probeLogCount < BLE_PROBE_MAX * 2) {
    logIdx = probeLogCount++;
    snprintf(probeLog[logIdx], sizeof(probeLog[0]), "%d/%d %s [%d] ...",
             probeNum, total, dev.addr.toString().c_str() + 9, dev.rssi);
    needRedraw = true;
  }

  char probeName[SEEN_NAME_LEN] = "";
  int r = simpleProbeHid(c, dev.addr, probeName, sizeof(probeName));
  const char *res = (r == 1) ? "HID!" : (r == 0) ? "no HID" : "fail";

  if (logIdx >= 0) {
    if (probeName[0])
      snprintf(probeLog[logIdx], sizeof(probeLog[0]), "%d/%d %s [%d] %s \"%s\"",
               probeNum, total, dev.addr.toString().c_str() + 9, dev.rssi, res,
               probeName);
    else
      snprintf(probeLog[logIdx], sizeof(probeLog[0]), "%d/%d %s [%d] %s",
               probeNum, total, dev.addr.toString().c_str() + 9, dev.rssi, res);
    needRedraw = true;
  }

  if (r == 1) {
    bleAddr = dev.addr;
    bleFound = true;
    if (probeName[0]) {
      strncpy(dev.name, probeName, SEEN_NAME_LEN - 1);
      dev.name[SEEN_NAME_LEN - 1] = '\0';
    }
    bleMatchReason =
        (probeName[0] && nameContains(std::string(probeName), BLE_KBD_MATCH))
            ? 3
            : 4;
    const char *label = probeName[0] ? probeName : "(HID device)";
    strncpy(bleFoundName, label, sizeof(bleFoundName) - 1);
    bleFoundName[sizeof(bleFoundName) - 1] = '\0';
    bleState = 2;
    needRedraw = true;
  }
}

// ===== Phase 3: Apple full-security probe, REUSES client =====
static void doAppleSecurityProbe(NimBLEClient *c, int seenIdx, int probeNum,
                                 int total) {
  seen_dev_t &dev = seenDevs[seenIdx];
  snprintf(bleStatusMsg, sizeof(bleStatusMsg), "APL %d/%d rssi=%d", probeNum,
           total, dev.rssi);
  bleState = 6;
  needRedraw = true;
  Serial.printf("[BLE] Apple probe %d/%d: %s rssi=%d heap=%d\n", probeNum,
                total, dev.addr.toString().c_str(), dev.rssi,
                esp_get_free_heap_size());

  int logIdx = -1;
  if (probeLogCount < BLE_PROBE_MAX * 2) {
    logIdx = probeLogCount++;
    snprintf(probeLog[logIdx], sizeof(probeLog[0]),
             "APL %d/%d %s [%d] pairing...", probeNum, total,
             dev.addr.toString().c_str() + 9, dev.rssi);
    needRedraw = true;
  }

  passkeyActive = false;
  pairingDone = false;
  pairingSuccess = false;

  if (!c->connect(dev.addr)) {
    if (logIdx >= 0)
      snprintf(probeLog[logIdx], sizeof(probeLog[0]),
               "APL %d/%d %s [%d] no conn", probeNum, total,
               dev.addr.toString().c_str() + 9, dev.rssi);
    needRedraw = true;
    passkeyActive = false;
    return;
  }

  NimBLERemoteService *hid = nullptr;
  if (c->isConnected()) hid = c->getService(NimBLEUUID("1812"));

  // wait for passkey if triggered
  if (!hid && passkeyActive && !pairingDone && c->isConnected()) {
    Serial.println("[BLE] Apple passkey triggered");
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "type PIN on keyboard!");
    needRedraw = true;
    uint32_t t0 = millis();
    while (!pairingDone && c->isConnected() &&
           (millis() - t0 < APPLE_PAIR_TIMEOUT_MS))
      vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (!hid && pairingDone && pairingSuccess && c->isConnected()) {
    Serial.println("[BLE] Apple paired, checking HID...");
    hid = c->getService(NimBLEUUID("1812"));
  }

  char probeName[SEEN_NAME_LEN] = "";
  if (c->isConnected()) {
    NimBLERemoteService *gap = c->getService(NimBLEUUID("1800"));
    if (gap && c->isConnected()) {
      NimBLERemoteCharacteristic *nm =
          gap->getCharacteristic(NimBLEUUID("2A00"));
      if (nm && nm->canRead() && c->isConnected()) {
        std::string gn = nm->readValue();
        if (!gn.empty()) {
          strncpy(probeName, gn.c_str(), sizeof(probeName) - 1);
          probeName[sizeof(probeName) - 1] = '\0';
        }
      }
    }
  }

  bool hidFound = (hid && c->isConnected());
  const char *res = hidFound                           ? "HID!"
                    : (pairingDone && !pairingSuccess) ? "pair fail"
                    : (passkeyActive && !pairingDone)  ? "timeout"
                                                       : "no HID";

  if (logIdx >= 0) {
    if (probeName[0])
      snprintf(probeLog[logIdx], sizeof(probeLog[0]),
               "APL %d/%d %s [%d] %s \"%s\"", probeNum, total,
               dev.addr.toString().c_str() + 9, dev.rssi, res, probeName);
    else
      snprintf(probeLog[logIdx], sizeof(probeLog[0]), "APL %d/%d %s [%d] %s",
               probeNum, total, dev.addr.toString().c_str() + 9, dev.rssi, res);
    needRedraw = true;
  }

  if (hidFound) {
    bleAddr = dev.addr;
    bleFound = true;
    bleMatchReason = 5;
    if (probeName[0]) {
      strncpy(bleFoundName, probeName, sizeof(bleFoundName) - 1);
      strncpy(dev.name, probeName, SEEN_NAME_LEN - 1);
    } else
      strncpy(bleFoundName, "(Apple HID)", sizeof(bleFoundName) - 1);
    bleFoundName[sizeof(bleFoundName) - 1] = '\0';
    bleState = 2;
    needRedraw = true;
  }

  waitDisconnect(c);
  passkeyActive = false;
}

// ===================== BLE TASK =====================
void bleTask(void *) {
  NimBLEDevice::init("CoreS3-KVM");
  setKeyboardSecurityMode();

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCbInstance, true);
  scan->setActiveScan(true);
  scan->setInterval(320);
  scan->setWindow(160);

  while (true) {
    if (!bleFound) {
      bleState = 1;
      bleMatchReason = 0;
      resetSeenDevs();
      probeLogCount = 0;
      probeTotalExpected = 0;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "scanning...");
      needRedraw = true;

      // ====== PHASE 1: Passive scan ======
      uint32_t budgetStart = millis();
      while (!bleFound && (millis() - budgetStart < BLE_SCAN_BUDGET_MS)) {
        bleScanDone = false;
        if (!scan->start(0, false)) {
          vTaskDelay(pdMS_TO_TICKS(500));
          continue;
        }
        while (!bleScanDone && !bleFound &&
               (millis() - budgetStart < BLE_SCAN_BUDGET_MS))
          vTaskDelay(pdMS_TO_TICKS(50));
        if (bleFound) break;
        scan->stop();
        vTaskDelay(pdMS_TO_TICKS(200));
      }
      scan->stop();
      scanElapsedMs = millis() - budgetStart;

      if (!bleFound) {
        int sorted[SEEN_DEV_MAX];
        int sortedN = getTopByRssi(sorted, SEEN_DEV_MAX);

        int probeIdx[BLE_PROBE_MAX], probeCount = 0;
        for (int s = 0; s < sortedN && probeCount < BLE_PROBE_MAX; s++) {
          int i = sorted[s];
          if (seenDevs[i].rssi < BLE_PROBE_MIN_RSSI ||
              seenDevs[i].name[0] != '\0')
            continue;
          if (!seenDevs[i].isApple) probeIdx[probeCount++] = i;
        }
        int appleIdx[BLE_PROBE_MAX], appleCount = 0;
        for (int s = 0; s < sortedN && appleCount < BLE_PROBE_MAX; s++) {
          int i = sorted[s];
          if (seenDevs[i].rssi < BLE_PROBE_MIN_RSSI ||
              seenDevs[i].name[0] != '\0')
            continue;
          if (seenDevs[i].isApple) appleIdx[appleCount++] = i;
        }

        probeTotalExpected = probeCount + appleCount;
        probeLogCount = 0;
        Serial.printf("[BLE] Phase 2+3: %d non-Apple + %d Apple, heap=%d\n",
                      probeCount, appleCount, esp_get_free_heap_size());

        // ====== PHASE 2: non-Apple — ONE client for all ======
        if (probeCount > 0 && !bleFound) {
          setProbeSecurityMode();
          NimBLEClient *pc = NimBLEDevice::createClient();
          if (pc) {
            pc->setConnectionParams(12, 24, 0, 200);
            for (int p = 0; p < probeCount && !bleFound; p++) {
              doQuickProbe(pc, probeIdx[p], p + 1, probeTotalExpected);
              vTaskDelay(pdMS_TO_TICKS(400));
            }
            if (pc->isConnected()) waitDisconnect(pc);
            NimBLEDevice::deleteClient(pc);
            vTaskDelay(pdMS_TO_TICKS(300));
            Serial.printf("[BLE] Phase 2 done, heap=%d\n",
                          esp_get_free_heap_size());
          }
        }

        // ====== PHASE 3: Apple — ONE client for all ======
        if (appleCount > 0 && !bleFound) {
          setKeyboardSecurityMode();
          NimBLEClient *ac = NimBLEDevice::createClient();
          if (ac) {
            ac->setClientCallbacks(&secCb);
            ac->setConnectionParams(6, 12, 0, 600);
            for (int a = 0; a < appleCount && !bleFound; a++) {
              doAppleSecurityProbe(ac, appleIdx[a], probeCount + a + 1,
                                   probeTotalExpected);
              vTaskDelay(pdMS_TO_TICKS(400));
            }
            if (ac->isConnected()) waitDisconnect(ac);
            NimBLEDevice::deleteClient(ac);
            vTaskDelay(pdMS_TO_TICKS(300));
            Serial.printf("[BLE] Phase 3 done, heap=%d\n",
                          esp_get_free_heap_size());
          }
        }

        setKeyboardSecurityMode();
      }

      // ====== ALL DONE — idle if not found ======
      if (!bleFound) {
        bleState = 7;
        snprintf(bleStatusMsg, sizeof(bleStatusMsg), "no kbd found");
        needRedraw = true;
        Serial.println("[BLE] idle mode (hold btn 2s = rescan)");
        bleScanRequested = false;
        while (!bleScanRequested) vTaskDelay(pdMS_TO_TICKS(500));
        bleScanRequested = false;
        continue;
      }
      needRedraw = true;
    }

    // ====== CONNECT ======
    bleState = 3;
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "connecting...");
    needRedraw = true;
    passkeyActive = false;
    pairingDone = false;
    pairingSuccess = false;

    NimBLEClient *c = nullptr;
    bool connected = false;
    for (int attempt = 0; attempt < MAX_CONNECT_RETRIES && !connected;
         attempt++) {
      if (attempt > 0) {
        snprintf(bleStatusMsg, sizeof(bleStatusMsg), "retry %d/%d", attempt + 1,
                 MAX_CONNECT_RETRIES);
        needRedraw = true;
        vTaskDelay(pdMS_TO_TICKS(1000 + attempt * 500));
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
      bleFound = false;
      bleState = 0;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "connect failed");
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
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

    NimBLERemoteService *hid = nullptr;
    if (c->isConnected()) hid = c->getService(NimBLEUUID("1812"));
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
      bleFound = false;
      bleState = 0;
      passkeyActive = false;
      snprintf(bleStatusMsg, sizeof(bleStatusMsg), "no HID service");
      needRedraw = true;
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    auto chars = hid->getCharacteristics(true);
    int subscribed = 0;
    for (auto &ch : chars)
      if (ch->getUUID() == NimBLEUUID("2A4D") && ch->canNotify())
        if (ch->subscribe(true, bleReportCb)) subscribed++;

    bleKbdConnected = true;
    bleState = 4;
    passkeyActive = false;
    needRedraw = true;
    Serial.printf("[BLE] CONNECTED: %s (%d reports)\n", bleFoundName,
                  subscribed);

    while (c->isConnected()) vTaskDelay(pdMS_TO_TICKS(500));

    bleKbdConnected = false;
    bleState = 5;
    snprintf(bleStatusMsg, sizeof(bleStatusMsg), "disconnected");
    needRedraw = true;
    NimBLEDevice::deleteClient(c);
    bleFound = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ===================== DISPLAY =====================
static const char *matchReasonStr(int r) {
  switch (r) {
    case 1:
      return "adv name";
    case 2:
      return "adv HID";
    case 3:
      return "probe name";
    case 4:
      return "probe HID";
    case 5:
      return "Apple";
    default:
      return "";
  }
}
static void drawPasskeyScreen() {
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
  M5.Display.setCursor((320 - 180) / 2, 110);
  M5.Display.print(buf);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(20, 180);
  if (pairingDone) {
    M5.Display.setTextColor(pairingSuccess ? GREEN : RED);
    M5.Display.print(pairingSuccess ? "Paired OK!" : "FAILED");
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
static void drawUI() {
  if (passkeyActive) {
    drawPasskeyScreen();
    return;
  }
  M5.Display.startWrite();
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 5);
  M5.Display.setTextColor(WHITE);
  M5.Display.print("PC ");
  M5.Display.setTextColor(activeTarget == 0 ? GREEN : CYAN);
  M5.Display.print(activeTarget + 1);
  M5.Display.setCursor(120, 5);
  M5.Display.setTextColor(usbMouseReady ? GREEN : RED);
  M5.Display.printf("Mouse:%s", usbMouseReady ? "OK" : "--");
  M5.Display.setCursor(10, 30);
  switch (bleState) {
    case 0:
      M5.Display.setTextColor(RED);
      M5.Display.printf("K: %s", bleStatusMsg);
      break;
    case 1:
      M5.Display.setTextColor(YELLOW);
      M5.Display.printf("K: scan %d dev", seenDevCount);
      break;
    case 2:
      M5.Display.setTextColor(GREEN);
      M5.Display.printf("K: FOUND (%s)", matchReasonStr(bleMatchReason));
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
      M5.Display.print("K: no kbd found");
      break;
  }
  if (bleFoundName[0]) {
    M5.Display.setCursor(10, 52);
    M5.Display.setTextColor(GREEN);
    M5.Display.setTextSize(1);
    M5.Display.printf(">> %s", bleFoundName);
  }
  M5.Display.setTextSize(1);
  int y = 68;
  if (bleState == 1) {
    int top[TOP_DISPLAY];
    int topN = getTopByRssi(top, TOP_DISPLAY);
    for (int t = 0; t < topN && y < 230; t++) {
      int i = top[t];
      M5.Display.setCursor(5, y);
      if (seenDevs[i].name[0]) {
        M5.Display.setTextColor(WHITE);
        M5.Display.printf("%d: %s [%d]%s%s", t + 1, seenDevs[i].name,
                          seenDevs[i].rssi,
                          seenDevs[i].hasHidUuid ? " HID" : "",
                          seenDevs[i].isApple ? " APL" : "");
      } else {
        M5.Display.setTextColor(seenDevs[i].isApple ? YELLOW
                                                    : (uint16_t)0x7BEF);
        M5.Display.printf("%d: (%s) [%d]%s%s", t + 1,
                          seenDevs[i].addr.toString().c_str(), seenDevs[i].rssi,
                          seenDevs[i].hasHidUuid ? " HID" : "",
                          seenDevs[i].isApple ? " APL" : "");
      }
      y += 12;
    }
    if (seenDevCount > TOP_DISPLAY) {
      M5.Display.setCursor(5, y);
      M5.Display.setTextColor(DARKGREY);
      M5.Display.printf("+%d more", seenDevCount - TOP_DISPLAY);
    }
  } else if (bleState == 6) {
    M5.Display.setCursor(5, y);
    M5.Display.setTextColor(CYAN);
    M5.Display.printf("Probing %d devices...", probeTotalExpected);
    y += 14;
    for (int i = 0; i < probeLogCount && y < 230; i++) {
      M5.Display.setCursor(5, y);
      if (strstr(probeLog[i], "HID!"))
        M5.Display.setTextColor(GREEN);
      else if (strstr(probeLog[i], "no HID") || strstr(probeLog[i], "fail") ||
               strstr(probeLog[i], "no conn") ||
               strstr(probeLog[i], "timeout") ||
               strstr(probeLog[i], "pair fail"))
        M5.Display.setTextColor(0xFB20);
      else if (strstr(probeLog[i], "pairing"))
        M5.Display.setTextColor(CYAN);
      else
        M5.Display.setTextColor(YELLOW);
      M5.Display.print(probeLog[i]);
      y += 12;
    }
  } else if (bleState == 7) {
    M5.Display.setCursor(5, y);
    M5.Display.setTextColor(WHITE);
    M5.Display.print("Working as mouse-only KVM");
    y += 14;
    M5.Display.setCursor(5, y);
    M5.Display.setTextColor(YELLOW);
    M5.Display.print("Hold button 2s = rescan BLE");
    y += 18;
    if (probeLogCount > 0) {
      M5.Display.setCursor(5, y);
      M5.Display.setTextColor(DARKGREY);
      M5.Display.print("Last probe results:");
      y += 12;
      for (int i = 0; i < probeLogCount && y < 230; i++) {
        M5.Display.setCursor(5, y);
        M5.Display.setTextColor(DARKGREY);
        M5.Display.print(probeLog[i]);
        y += 12;
      }
    }
  }
  M5.Display.endWrite();
}

// ===================== SETUP =====================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.begin();
  M5.Power.setUsbOutput(true);
  Serial.begin(115200);
  delay(100);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.println("KVM Init...");
  mouseQueue = xQueueCreate(128, sizeof(mouse_evt_t));
  kbdQueue = xQueueCreate(32, sizeof(kbd_evt_t));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHAN, WIFI_SECOND_CHAN_NONE);
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
  xTaskCreatePinnedToCore(usbHostTask, "USB", 8192, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(bleTask, "BLE", 64 * 1024, NULL, 3, NULL, 0);
  delay(300);
  drawUI();
}

// ===================== LOOP =====================
static uint32_t lastBeat = 0;
static bool prevUsb = false, prevBle = false, prevPasskey = false;
static int prevBleS = -1;
static uint8_t prevButtons = 0;
static uint32_t btnDownStart = 0;
static bool btnLongHandled = false;

void loop() {
  M5.update();

  // button: short=switch, long 2s=rescan
  if (M5.BtnA.isPressed()) {
    if (btnDownStart == 0) btnDownStart = millis();
    if (!btnLongHandled && (millis() - btnDownStart > 2000)) {
      btnLongHandled = true;
      bleScanRequested = true;
      needRedraw = true;
    }
  } else {
    if (btnDownStart > 0 && !btnLongHandled)
      if (millis() - lastSwitchMs > SWITCH_DEBOUNCE_MS) switchTarget();
    btnDownStart = 0;
    btnLongHandled = false;
  }

  mouse_evt_t m;
  while (xQueueReceive(mouseQueue, &m, 0) == pdTRUE) {
    bool m4 = (m.buttons & 0x08);
    if (m4 && !mouse4Held && (millis() - lastSwitchMs > SWITCH_DEBOUNCE_MS))
      switchTarget();
    mouse4Held = m4;
    uint8_t btns3 = m.buttons & 0x07;
    bool btnChanged = (btns3 != prevButtons);
    mAccum.buttons = m.buttons;
    mAccum.x += m.x;
    mAccum.y += m.y;
    mAccum.wheel += m.wheel;
    mDirty = true;
    if (btnChanged) {
      txMouse(&mAccum);
      prevButtons = btns3;
      mAccum = {};
      mAccum.buttons = m.buttons;
      mDirty = false;
      mLastSendUs = micros();
    }
  }
  if (mDirty) {
    uint32_t now = micros();
    if (now - mLastSendUs >= MOUSE_SEND_US) {
      txMouse(&mAccum);
      mAccum = {};
      mDirty = false;
      mLastSendUs = now;
    }
  }

  kbd_evt_t k;
  while (xQueueReceive(kbdQueue, &k, 0) == pdTRUE) txKbd(&k);

  if (millis() - lastBeat > HEARTBEAT_MS) {
    sendCmd(activeMac(), PKT_ACTIVATE);
    delay(2);
    sendCmd(inactiveMac(), PKT_DEACTIVATE);
    lastBeat = millis();
  }

  bool force = false;
  if (passkeyActive) {
    static uint32_t lp = 0;
    if (millis() - lp > 1000) {
      force = true;
      lp = millis();
    }
  }
  if (force || needRedraw || usbMouseReady != prevUsb ||
      bleKbdConnected != prevBle || bleState != prevBleS ||
      passkeyActive != prevPasskey) {
    prevUsb = usbMouseReady;
    prevBle = bleKbdConnected;
    prevBleS = bleState;
    prevPasskey = passkeyActive;
    needRedraw = false;
    drawUI();
  }
  delay(1);
}
```

- Click reset button once (DO NOT HOLD) on M5Stack CoreS3 se and you'll see new screen. Now plug-in mouse dongle and connect keyboard via bluetooth (Pairing starts automatically as soon as you enable the device). Don't forget to click forget in macos settings if you already connected keyboard to macos via bluetooth. Sometimes keyboard might ask you to input security code on it which will be displayed on device screen.
