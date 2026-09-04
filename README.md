# 🖱️⌨️ M5Stack Wireless KVM Switch

> Wireless keyboard & mouse KVM switch (**both IN and OUT**) based on M5Stack CoreS3 SE and Atom S3U — capable of handling a **1000 Hz wireless mouse** and **Bluetooth keyboard**.

Switch between multiple PCs (2+) with a single mouse button press. No cables for peripherals, no lag.

---

## 📦 Hardware

| Component | Qty |
|---|:-:|
| [M5Stack CoreS3 SE IoT Controller](https://shop.m5stack.com/products/m5stack-cores3-se-iot-controller-w-o-battery-bottom) (without Battery Bottom) | 1 |
| [AtomS3U](https://shop.m5stack.com/products/atoms3u) ESP32S3 Dev Kit with USB-A | 2 |
| [M5GO Battery Bottom3](https://shop.m5stack.com/products/m5go-battery-bottom3-for-cores3-only) (for CoreS3 only) | 1 |
| [M5GO / FIRE Battery Bottom](https://shop.m5stack.com/products/battery-bottom-charging-base) Charging Base | 1 |
| [M5Stack USB Module V1.2 — for M5Core](https://shop.m5stack.com/products/usb-module-with-max3421e-v1-2?variant=44512358793473) *(For legacy BT only)* | 1 |
| [DFRobot Bluetooth 4.0 USB Adapter (TEL0002)](https://wiki.dfrobot.com/Bluetooth_Adapter__SKU_TEL0002_) *(For classic BT keyboard)* | 1 |
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

4. **`BTD.h`** — add event defines (after the existing `EV_` defines, around line 110):
   ```cpp
   #define EV_MODE_CHANGE                                  0x14
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

10. **`BTHID.cpp`** — also handle Input report responses (0xA1) on the L2CAP control channel, not just Feature (0xA3). Around line 376, change:
    ```cpp
    // WAS:  if(l2capinbuf[8] == 0xA3) {
    if(l2capinbuf[8] == 0xA3 || l2capinbuf[8] == 0xA1) {
    ```

11. **`BTD.h`** and **`BTD.cpp`** — added `hci_write_link_policy()`, `hci_sniff_subrating()`, `hci_sniff_mode()`, and `hci_exit_sniff_mode()`. Only `hci_write_link_policy` is called from the firmware — it sets the link policy to allow sniff + role switch (0x05) so the keyboard can negotiate its own power saving, just like macOS. The keyboard enters sniff mode automatically (~15ms interval = 24 slots × 0.625ms). `hci_sniff_subrating()` (SSR) is available for dongles that support it — the DFRobot TEL0002 does not (returns `0x1A` Unsupported Remote Feature). `hci_sniff_mode()` is NOT called because it returns `Command Status` (not `Command Complete`), which breaks the BTD state machine.

12. **`BTD.cpp`** — added `EV_MODE_CHANGE` (0x14) and `EV_SNIFF_SUBRATING` (0x2E) event handlers. Logs mode transitions, sniff intervals, and SSR negotiation results under `DEBUG_USB_HOST`. Event mask updated (`0x1F` → `0x3F`) to enable bit 45 for `EV_SNIFF_SUBRATING`. Also added logging for failed HCI commands in `EV_COMMAND_COMPLETE` handler.

13. **`BTD.h`** and **`BTD.cpp`** — added `hci_set_afh_classification()` (OGF `0x03` / OCF `0x3F`, *Set_AFH_Host_Channel_Classification*). Tells the controller which of the 79 Bluetooth channels the host considers bad so adaptive frequency hopping steers around them; the firmware builds the map from `ESPNOW_CHAN` and marks the ±11MHz around the ESP-NOW channel.

    Why it matters: the BT dongle sits centimetres from the ESP32 antenna and hops across the whole 2.4GHz band 1600×/sec, so it lands on the ESP-NOW channel no matter which one you pick. Measured on a CoreS3 with an Apple Magic Keyboard connected:

    | | gaps ≥20ms | gaps ≥100ms | median worst gap |
    |---|---|---|---|
    | keyboard on, no AFH | 2.49/s | 0.49/s | 41ms |
    | keyboard on, **AFH** | 2.02/s | 0.31/s | **17ms** |
    | keyboard off (floor) | 0.24/s | 0.14/s | 12ms |

    AFH is **advisory** — the controller may ignore it, and at least 20 of the 79 channels must stay marked good or the command is rejected. The DFRobot TEL0002 accepts it (unlike SSR). Two status fields report the outcome: `afhAccepted` and `afhRejected` (the HCI status byte). The firmware shows this next to the channel in the debug overlay as `C:13+` (accepted), `-` (rejected) or `.` (no answer yet).

14. **`BTD.h`** — added `hciCmdComplete()`, a public accessor for the `HCI_FLAG_CMD_COMPLETE` flag. The library keeps a single completion flag and does **not** honour `Num_HCI_Command_Packets`, so sending several HCI commands back to back silently loses all but the first on controllers that accept only one outstanding command. The firmware now waits on this between `hci_write_link_policy()`, `hci_sniff_subrating()` and `hci_set_afh_classification()` (bounded wait, on connect only).

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

> ⚠️ **Important:** Before uploading, set these Arduino IDE options:
>
> | Setting | Value |
> |---|---|
> | **USB Mode** | USB-OTG (TinyUSB) |
> | **USB Upload Mode** | USB-OTG (TinyUSB) |
> | **USB CDC On Boot** | **Enabled** (required — see [Linux setup](#-linux-host-setup-required) below) |

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
| `targets[]` | Array of PC targets: MAC, AES-128 key, bind button | See below |
| `BLE_KBD_MATCH` | Part of your keyboard's BT name (no special chars) | `"Keyboard"` |
| `USE_MAX_MODULE` | Set `true` if using the classic USB module | `false` |
| `WITH_KEYBOARD` | Set `false` for mouse-only mode | `true` |
| `BLE_PROBE_MIN_RSSI` | Raise to `-80` if keyboard is far away | `-55` |
| `ESPNOW_CHAN` | 2.4GHz channel for ESP-NOW. **Must match on the CoreS3 and every AtomS3U.** Pick one clear of nearby Wi-Fi | `13` |
| `ESPNOW_COUNTRY` | Country code declaring the legal channel range. Only matters for channels 12-13 — see the comment on `applyEspNowChannel()` | `"PL"` |
| `DEBUG_DEFAULT_ON` | Power-on state of debug mode. It is a **runtime** toggle now — see [Debug Mode](#-debug-mode) | `false` |

#### PC Targets Configuration

Each PC is defined as a `PCTarget` struct with its AtomS3U MAC address, AES-128 key, and switch bind button:

```cpp
static const PCTarget targets[] = {
  // PC 1: AtomS3U MAC, AES-128 key, mouse button 4 (0x08) to switch
  {{0x3C,0xDC,0x75,0xED,0xFB,0x4C}, {0x4B,0x56,0x4D,0x53,0x77,0x31,0x7A,0xDE,0xAD,0xBE,0xEF,0x42,0x13,0x37,0xCA,0xFE}, 0x08},
  // PC 2: AtomS3U MAC, AES-128 key, mouse button 5 (0x10) to switch
  {{0xD0,0xCF,0x13,0x0F,0x90,0x48}, {0x4B,0x56,0x4D,0x53,0x77,0x31,0x7A,0xDE,0xAD,0xBE,0xEF,0x42,0x13,0x37,0xCA,0xFE}, 0x10},
};
```

To add more PCs, just add another line to the array.

#### Bind Behavior

Each PC has its own switch bind (mouse button):
- Press the bind for **another** PC → switches to that PC directly
- Press the bind for the **current** PC → cycles to next PC in the list
- Example: on PC1 (bind=mouse4), press mouse5 → go to PC2; on PC2 (bind=mouse5), press mouse5 → go to PC1
- M5 hardware button A always cycles to the next PC
- Set `bindMask` to `0` for no bind (switch only via M5 button or other PC's bind)

#### Mouse Button Bitmask Reference

| Button | Bitmask | Hex |
|---|---|---|
| Mouse 4 (back) | bit 3 | `0x08` |
| Mouse 5 (forward) | bit 4 | `0x10` |
| Mouse 6 | bit 5 | `0x20` |

> **Security:** Keyboard data is encrypted with AES-128-CTR (hardware-accelerated) over the air. Each PC has its own AES key — the key for each target must match the corresponding AtomS3U receiver. Mouse data is sent as unencrypted broadcast for maximum performance. **Generate your own random 16-byte keys and set them in both `ino_cores3se.ino` (`targets[].aesKey`) and `ino_atoms3u.ino` (`AES_KEY`) — the values must match per PC.** The CTR nonce is kept monotonic in NVS (reserved in blocks of 100000, one flash write per block) so it never repeats across reboots — a repeated counter would reuse the keystream and leak plaintext. The nonce is 4 bytes on the wire, so rotate the keys after roughly 43000 boots. Using the same key for all PCs is fine but means a compromised key on one PC exposes all keyboard traffic.

<details>
<summary>📄 <b>CoreS3 SE Main Controller Sketch</b> — click to expand</summary>

```ino
https://github.com/krll-kov/m5stack-wireless-kvm-switch/blob/main/ino_cores3se.ino
```

</details>

---

## 🔬 Debug Mode

Debug mode is a **runtime toggle**, not a build flag — no reflashing to turn it on.

**Hold `BtnC` (right touch button) on the CoreS3 for ~1 second** to switch it on or off. Short presses still just wake the screen. The state is not kept across reboots, so a reset always comes back with debug off.

While it is **off**, nothing is measured, sent or drawn: every collection point sits behind a single boolean check, so normal operation is unaffected.

While it is **on**:

1. **On-screen overlay** appears in the CoreS3 bottom bar:

   ```
   T:47C U:1000 E:1000 Q:2 F:0/0/0 G:3
   ```

   | Field | Meaning |
   |---|---|
   | `T` | CPU temperature, °C |
   | `U` | HID reports read from the mouse dongle, per second |
   | `E` | mouse frames handed to `esp_now_send`, per second |
   | `Q` | deepest the mouse queue got |
   | `F` | send failures: mouse / keyboard / control packets |
   | `G` | longest gap between two mouse frames leaving the box, ms |

2. **Telemetry is broadcast** once a second as a `PKT_DEBUG` frame. Each AtomS3U receiver picks it up, switches its own debug on automatically, and prints both sides to its USB CDC serial port:

   ```
   TX win=1001ms usb=1000 esp=1000 ms=1000 q=2 fail=0/0/0 txgap=3ms usbgap=3ms send=180us dead=0 idle=0 heap=180244
   RX win=1002ms esp=1002 mouse=1000 hb=2 gapmax=6ms rssi=-39/-33 usb=1000 mrg=0 w=12/50 q=3,0 act=1
      gaps <20=998 20-49=2 50-99=0 100-199=0 200-499=0 500+=0
   ```

   The `gaps` histogram buckets the time between received packets. A worst-case number alone cannot tell a single rare spike from a steady pattern, and the pattern is what points at the source of interference.

   Both lines are **raw counts over the window they name** (`win=`), so they can be compared directly — `TX esp` against `RX mouse` gives the actual loss.

   `TX` is the transmitter's own view, `RX` is what the receiver got. The three fields that matter:

   | Comparison | Meaning |
   |---|---|
   | `fail` non-zero | `esp_now_send` refused the frame — TX queue full or radio busy |
   | `TX esp` high, `RX mouse` low | frames were sent and lost **in the air** |
   | `txgap` spikes but `usbgap` stays small | the transmitter stalled while the mouse kept reporting |
   | `txgap` and `usbgap` spike together | the mouse simply was not moving — not a fault |

   `usbgap` exists precisely to keep `txgap` honest: no mouse movement means no sends, which would otherwise read as a stall.

   The receiver turns its debug off again ~5 seconds after telemetry stops arriving.

To read it, open the AtomS3U's serial port at any baud rate (it is USB CDC, the rate is ignored) — e.g. the Arduino IDE **Serial Monitor**, or `screen /dev/cu.usbmodemXXXX`.

> The receiver decodes telemetry format `ver=1`. If the transmitter ever sends a newer format, the receiver prints it as a hex dump instead of guessing — so the AtomS3U never needs reflashing just to follow a change on the CoreS3 side.

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
| **First input delay after idle** | The device enters power-saving mode after inactivity (10 sec = 1ms delay, 1 minute = 20ms delay, 5 min = 50ms delay, 15 min = 100ms delay). The first mouse movement after wake may feel slightly delayed. Mouse USB polling remains active at all idle levels (USB SOF keeps the dongle awake, and submitted transfers cost nothing when idle — NAK is handled in hardware). Classic BT keyboards negotiate sniff mode automatically with the dongle (enabled by setting link policy to allow sniff + role switch after connection) — the Apple Magic Keyboard enters sniff at ~15ms interval. Sniff Subrating (SSR) for deeper idle sleep is supported in code but the DFRobot TEL0002 dongle does not support it; a dongle with SSR support would allow the keyboard to skip sniff slots during extended idle. BLE keyboards use relaxed connection parameters at higher idle levels. All power-saving is transparent and reverts to full speed on activity. |
| **Mouse rate** | Mouse events are forwarded 1:1 at the native poll rate of your mouse (tested up to 1000Hz). ESP-NOW uses 6.5 Mbps HT20 PHY for sufficient wireless throughput, and the AtomS3U uses non-blocking USB sends to avoid frame-alignment bottlenecks. On Linux, the CDC serial port must be kept open for full speed — see [Linux setup](#-linux-host-setup-required). |
| **Apple fn/Globe key — dictation popup** | The fn (Globe) key is forwarded as a consumer control key (usage 0x029D). A quick tap while typing may trigger the macOS "Enable Dictation?" dialog. To fix: go to **System Settings → Keyboard → Dictation** and turn it off, or change **"Press fn key to"** to "Change Input Source". You also have to select "Start Dictation - Press twice" and in bottom section change the shortcut to microphone, then switch back to "Change Input Source" |
| **`loop()` starvation on the receiver** | Fixed. `usbTask` runs at priority 10 on the same core as `loop()` (priority 1, `LoopCore=1`), and `taskYIELD()` only hands off to equal-or-higher priority tasks — so while the mouse was moving `loop()` never ran, stopping all USB mount/suspend/replug handling. It now gets a real scheduling slot ~20×/sec. |
| **Battery status does not update** | If you don't use debug mode, the only way to update the screen is to press mouse4 to switch pc or to plug-out/in mouse dongle, this is made for performance reasons. Also if you charge with battery base - we can't get the voltage and other info directly with code so we measure it by taking periodic battery samples. Apple keyboard battery is read from two HID reports: 0xF0 (periodic battery report) and 0x9B (device status report, byte 2 = battery %). |
| **Screen goes black** | This is done because of power efficiency - screen is only displayed during setup/pc switch (10sec here), without it battery will drain faster than it's charing from battery bottom |
| **Unlock to use accessories MacOS lock-screen** | One of the most recent MacOS updates has changed something and after a night of inactivity mouse and keyboard do not work on lock screen, to fix this you need to go to Settings → Privacy & Security → Accessories and switch it to "Always Allow" |

---

## 🐧 Linux Host Setup (Required)

The AtomS3U uses the ESP32-S3 DWC2 USB controller, which has a hardware quirk: the HID endpoint (mouse/keyboard) only runs at full speed (~1000 Hz) when the CDC serial endpoint is actively polled by the host. Without this, mouse rate drops to ~500 Hz.

**On macOS and Windows** this happens automatically — the OS keeps CDC endpoints active.

**On Linux** you need to keep the CDC port open. Create a systemd service on each Linux PC where an AtomS3U is plugged in:

```bash
# 1. Create the service
sudo tee /etc/systemd/system/kvm-cdc.service << 'EOF'
[Unit]
Description=Keep KVM CDC port active for HID performance
After=dev-ttyACM0.device
BindsTo=dev-ttyACM0.device

[Service]
ExecStart=/bin/bash -c '/bin/cat /dev/ttyACM0 > /dev/null'
Restart=always
RestartSec=1

[Install]
WantedBy=dev-ttyACM0.device
EOF

# 2. Enable and start
sudo systemctl daemon-reload
sudo systemctl enable kvm-cdc.service
sudo systemctl start kvm-cdc.service
```

> If your device appears as `/dev/ttyACM1` instead of `ttyACM0`, replace both occurrences in the service file.
>
> To verify it's working, check `systemctl status kvm-cdc.service` — it should show "active (running)".

---

## 🏗️ Architecture

```
┌──────────────┐      ESP-NOW       ┌──────────────┐
│  Wireless    │  ───────────────►  │  AtomS3U #1  │──── USB ──── PC 1
│  Mouse       │                    └──────────────┘
│  (dongle) ──►│ CoreS3 SE          ┌──────────────┐
│              │  ───────────────►  │  AtomS3U #2  │──── USB ──── PC 2
│  Bluetooth   │      ESP-NOW       └──────────────┘
│  Keyboard ──►│                     ...2+ PCs
└──────────────┘
   [Mouse4] = PC 1 bind    [Mouse5] = PC 2 bind
```

The CoreS3 SE acts as the central hub: it reads the USB mouse dongle and BLE keyboard, then forwards all HID events over ESP-NOW to whichever Atom S3U is currently active. Each Atom S3U appears as a standard USB keyboard + mouse to its host PC.

---

## 📜 License

[GPL-3.0-1](https://github.com/krll-kov/m5stack-wireless-kvm-switch?tab=GPL-3.0-1-ov-file#readme)
