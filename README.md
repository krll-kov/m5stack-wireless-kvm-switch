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
- `USB Host Shield Library 2.0` (by Oleg Mazurov / Circuits At Home) — then apply pin modifications below

<img width="828" alt="Library manager" src="https://github.com/user-attachments/assets/e6a6d9ba-8ea7-4ac3-9e12-511c020b295e" />

### USB Module V1.2 — DIP Switch Configuration

The M5Stack USB Module V1.2 has DIP switches on the back that select which GPIO pins are used for SS (chip select) and INT (interrupt). Set them as follows for CoreS3 SE:

| Switch | Position |
|--------|----------|
| SS G13 | **OFF** |
| SS G5  | **ON** |
| SS G0  | **OFF** |
| INT G35 | **ON** |
| INT G34 | **OFF** |

This selects **SS2** (G5 → remapped to G1 in software) and **INT1** (G35 → remapped to G10 in software).

> The physical DIP switch labels refer to the original M5Stack Core pin mapping. On CoreS3 SE these module connector pins are routed to different GPIOs — the library pin modifications below handle the remapping.

### USB Host Shield 2.0 — Pin Modifications for CoreS3 SE

After installing the library, modify these files in your Arduino libraries folder (typically `~/Documents/Arduino/libraries/USB_Host_Shield_Library_2.0/`):

> **Quick way:** Instead of editing files manually, copy all files from the [`USB_Host_Shield_2.0_Replacements/`](USB_Host_Shield_2.0_Replacements/) folder in this repo and replace the corresponding files in your library folder.

> **Note:** The upstream library (v1.7.0) already has an `ARDUINO_M5STACK_CORES3` section in `avrpins.h`, `usbhost.h`, and `UsbCore.h`, but it uses INT=P14 which doesn't match our wiring (INT=GPIO10). Remove those `#elif defined(ARDUINO_M5STACK_CORES3)` blocks and apply the changes below to the generic `#elif defined(ESP32)` section instead.

**Pin configuration (3 files):**

1. **`avrpins.h`** — remove the `ARDUINO_M5STACK_CORES3` block, then add after `MAKE_PIN(P17, 17);` (in the generic ESP32 section):
   ```cpp
   MAKE_PIN(P35, 35);
   MAKE_PIN(P36, 36);
   MAKE_PIN(P37, 37);
   ```

2. **`usbhost.h`** — remove the `ARDUINO_M5STACK_CORES3` spi typedef, then change the generic ESP32 `spi` typedef:
   ```cpp
   typedef SPi< P36, P37, P35, P1 > spi;  // SCK=36, MOSI=37, MISO=35, SS=1(CS)
   ```
   Also add an ESP32 case in `SPi::init()` so `SPI.begin()` uses the correct pins:
   ```cpp
   #if defined(ESP32)
           USB_SPI.begin(36, 35, 37, -1); // SCK=36, MISO=35, MOSI=37, no HW SS
   #else
           USB_SPI.begin();
   #endif
   ```

3. **`UsbCore.h`** — remove the `ARDUINO_M5STACK_CORES3` typedef, then change the generic ESP32 `MAX3421E` typedef:
   ```cpp
   typedef MAX3421e<P1, P10> MAX3421E;  // SS=GPIO1(CS), INT=GPIO10
   ```

**SSP (Secure Simple Pairing) for Apple keyboards (2 files):**

4. **`BTD.h`** — add two event defines (after the existing `EV_` defines, around line 110):
   ```cpp
   #define EV_USER_PASSKEY_REQUEST                         0x34
   #define EV_USER_PASSKEY_NOTIFICATION                    0x3B
   ```
   Add a passkey variable (after `char remote_name[30];`, around line 492):
   ```cpp
   /** SSP passkey for Passkey Entry (displayed to user, typed on keyboard). */
   uint32_t sspPasskey;
   ```

5. **`BTD.cpp`** — three changes:

   a) In `hci_io_capability_request_reply()` (around line 1401), change IO capability from `0x03` (NoInputNoOutput) to `0x01` (DisplayOnly) and auth requirement to `0x04` (MITM Required):
   ```cpp
   hcibuf[9] = 0x01;  // DisplayOnly (was 0x03)
   hcibuf[10] = 0x00; // OOB authentication data not present
   hcibuf[11] = 0x04; // MITM Required, Dedicated Bonding (was 0x02)
   ```

   b) Add passkey event handlers in `ACL_event_task()` — in the HCI event switch, after the `EV_USER_CONFIRMATION_REQUEST` case (around line 714), add:
   ```cpp
   case EV_USER_PASSKEY_REQUEST:
   #ifdef DEBUG_USB_HOST
           Notify(PSTR("\r\nUser Passkey Request"), 0x80);
   #endif
           {
                   uint8_t buf[9];
                   buf[0] = 0x2F; buf[1] = 0x01 << 2; buf[2] = 0x06;
                   for(uint8_t i = 0; i < 6; i++) buf[3 + i] = disc_bdaddr[i];
                   HCI_Command(buf, 9);
           }
           break;

   case EV_USER_PASSKEY_NOTIFICATION:
           sspPasskey = (uint32_t)hcibuf[3] | ((uint32_t)hcibuf[4] << 8) |
                        ((uint32_t)hcibuf[5] << 16) | ((uint32_t)hcibuf[6] << 24);
   #ifdef DEBUG_USB_HOST
           Notify(PSTR("\r\n>>> PASSKEY NOTIFICATION: "), 0x80);
           USB_HOST_SERIAL.print(sspPasskey);
   #endif
           break;
   ```

   c) In `HCI_DISCONNECT_STATE` (around line 1093), add service reset and L2CAP flag cleanup so BTHID properly detects physical disconnects. **Replace the entire case body** with:
   ```cpp
   case HCI_DISCONNECT_STATE:
           if(hci_check_flag(HCI_FLAG_DISCONNECT_COMPLETE)) {
   #ifdef DEBUG_USB_HOST
                   Notify(PSTR("\r\nHCI Disconnected from Device"), 0x80);
   #endif
                   // Reset all services (BTHID etc.) so connected flags become false
                   for(uint8_t i = 0; i < BTD_NUM_SERVICES; i++)
                           if(btService[i])
                                   btService[i]->Reset();

                   hci_event_flag = 0;
                   memset(hcibuf, 0, BULK_MAXPKTSIZE);
                   memset(l2capinbuf, 0, BULK_MAXPKTSIZE);

                   connectToWii = incomingWii = pairWithWii = false;
                   connectToHIDDevice = incomingHIDDevice = checkRemoteName = false;
                   incomingPSController = false;

                   l2capConnectionClaimed = false;
                   sdpConnectionClaimed = false;
                   rfcommConnectionClaimed = false;

                   hci_state = HCI_SCANNING_STATE;
           }
           break;
   ```

   d) In `EV_INQUIRY_COMPLETE` handler (around line 488), prevent the library from giving up after 5 failed inquiries for HID devices — change the block to only give up for Wii, and keep retrying for HID:
   ```cpp
   case EV_INQUIRY_COMPLETE:
           if(inquiry_counter >= 5 && pairWithWii) {
                   inquiry_counter = 0;
                   connectToWii = false;
                   pairWithWii = false;
                   hci_state = HCI_SCANNING_STATE;
           }
           if(inquiry_counter >= 5 && pairWithHIDDevice) {
                   // Don't give up — reset counter and keep searching
                   inquiry_counter = 0;
           }
           inquiry_counter++;
           break;
   ```

6. **`BTD.h`** — make `hci_state` accessible from the sketch, and add link key storage (in the `private:` section, around line 587):
   ```cpp
   /* Variables used by high level HCI task */
   public:
           uint8_t hci_state; // Current state of Bluetooth HCI connection
   private:
   ```

   Also add link key fields after `sspPasskey` (around line 493):
   ```cpp
   /** Stored link key for reconnection (set from EV_LINK_KEY_NOTIFICATION). */
   uint8_t link_key[16];
   /** BD_ADDR associated with stored link key. */
   uint8_t link_key_bdaddr[6];
   /** True if a valid link key is stored. */
   bool link_key_valid;
   ```

   And add function declarations after `hci_link_key_request_negative_reply()`:
   ```cpp
   /** Reply to a Link Key Request with a stored link key. */
   void hci_link_key_request_reply();
   /** Enable encryption on an existing HCI connection (needed after link key auth). */
   void hci_set_connection_encryption();
   ```

7. **`BTD.cpp`** — link key storage and reconnection support:

   a) In the constructor (around line 41), initialize link key fields:
   ```cpp
   link_key_valid = false;
   memset(link_key, 0, sizeof(link_key));
   memset(link_key_bdaddr, 0, sizeof(link_key_bdaddr));
   ```

   b) Replace `EV_LINK_KEY_REQUEST` handler (around line 649) — reply with stored key when available:
   ```cpp
   case EV_LINK_KEY_REQUEST:
           if(link_key_valid && memcmp(&hcibuf[2], link_key_bdaddr, 6) == 0) {
                   hci_link_key_request_reply();
           } else {
                   hci_link_key_request_negative_reply();
           }
           break;
   ```

   c) Move `EV_LINK_KEY_NOTIFICATION` out of the ignored events list and add a handler to store the key:
   ```cpp
   case EV_LINK_KEY_NOTIFICATION:
           for(uint8_t i = 0; i < 6; i++)
                   link_key_bdaddr[i] = hcibuf[2 + i];
           for(uint8_t i = 0; i < 16; i++)
                   link_key[i] = hcibuf[8 + i];
           link_key_valid = true;
           break;
   ```

   d) Add `hci_link_key_request_reply()` and `hci_set_connection_encryption()` functions after `hci_link_key_request_negative_reply()`:
   ```cpp
   void BTD::hci_link_key_request_reply() {
           hcibuf[0] = 0x0B; // HCI OCF = 0B
           hcibuf[1] = 0x01 << 2; // HCI OGF = 1
           hcibuf[2] = 0x16; // parameter length 22
           hcibuf[3] = disc_bdaddr[0];
           hcibuf[4] = disc_bdaddr[1];
           hcibuf[5] = disc_bdaddr[2];
           hcibuf[6] = disc_bdaddr[3];
           hcibuf[7] = disc_bdaddr[4];
           hcibuf[8] = disc_bdaddr[5];
           for(uint8_t i = 0; i < 16; i++)
                   hcibuf[9 + i] = link_key[i];
           HCI_Command(hcibuf, 25);
   }

   void BTD::hci_set_connection_encryption() {
           hcibuf[0] = 0x13; // HCI OCF = 0x13 (Set_Connection_Encryption)
           hcibuf[1] = 0x01 << 2; // HCI OGF = 1 (Link Control)
           hcibuf[2] = 0x03; // parameter length 3
           hcibuf[3] = (uint8_t)(hci_handle & 0xFF);
           hcibuf[4] = (uint8_t)((hci_handle >> 8) & 0x0F);
           hcibuf[5] = 0x01; // Encryption_Enable = ON
           HCI_Command(hcibuf, 6);
   }
   ```

   e) In `EV_AUTHENTICATION_COMPLETE` handler, when reconnecting with stored link key, enable encryption and wait for `EV_ENCRYPTION_CHANGE` before starting L2CAP:
   ```cpp
   } else if(pairWithHIDDevice && !connectToHIDDevice) {
           Notify(PSTR("\r\nPairing successful with HID device"), 0x80);
           if(link_key_valid) {
                   // Reconnect — enable encryption first, wait for EV_ENCRYPTION_CHANGE
                   hci_set_connection_encryption();
           } else {
                   // First SSP pairing — controller handles encryption automatically
                   connectToHIDDevice = true;
           }
   ```

   f) Move `EV_ENCRYPTION_CHANGE` out of the ignored events list and add a handler — set `connectToHIDDevice` after encryption is established:
   ```cpp
   case EV_ENCRYPTION_CHANGE:
           if(hcibuf[2] == 0x00) { // Status = success
                   Notify(PSTR("\r\nEncryption enabled"), 0x80);
                   if(pairWithHIDDevice && !connectToHIDDevice) {
                           connectToHIDDevice = true;
                   }
           } else {
                   Notify(PSTR("\r\nEncryption failed: "), 0x80);
                   D_PrintHex<uint8_t>(hcibuf[2], 0x80);
                   hci_disconnect(hci_handle);
           }
           break;
   ```

8. **`BTHID.h`** — make `setProtocol()` virtual so the sketch can override it (Apple keyboards break when SET_PROTOCOL is sent on reconnect). In the `private:` section (around line 171), change:
   ```cpp
   // WAS:  void setProtocol();
   virtual void setProtocol();
   ```

9. **`cdcacm.cpp`** — add protocol comparison flag to CDC control parser (around line 139). This fixes enumeration of BT dongles with non-standard CDC protocol values:
    ```cpp
    // WAS:  CP_MASK_COMPARE_SUBCLASS > CdcControlParser(this);
    CP_MASK_COMPARE_SUBCLASS |
    CP_MASK_COMPARE_PROTOCOL > CdcControlParser(this);
    ```

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
| **Apple fn/Globe key — dictation popup** | The fn (Globe) key is forwarded as a consumer control key (usage 0x029D). A quick tap while typing may trigger the macOS "Enable Dictation?" dialog. To fix: go to **System Settings → Keyboard → Dictation** and turn it off, or change **"Press fn key to"** to "Change Input Source". You also have to select "Start Dictation - Press twice" and in bottom section change the shortcut to microphone, then switch back to "Change Input Source" |
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
