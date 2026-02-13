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
https://github.com/krll-kov/m5stack-wireless-kvm-switch/blob/main/ino_atoms3u.ino
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
https://github.com/krll-kov/m5stack-wireless-kvm-switch/blob/main/ino_cores3se.ino
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
| **Battery status does not update** | If you don't use debug mode, the only way to update the screen is to press mouse4 to switch pc or to plug-out/in mouse dongle, this is made for performance reasons. Also if you charge with battery base - we can't get the voltage and other info directly with code so we measure it by taking periodic battery samples |
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
