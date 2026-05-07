# 📖 Setup Guide — Smart Helmet Collision Warning System

A step-by-step guide to get both ESP32 boards programmed and communicating.

---

## Prerequisites

- A computer with USB ports
- USB Micro-B cable (for ESP32-CAM) and/or USB-C/Micro-B (for ESP32 DevKit)
- Arduino IDE 2.x installed
- The Edge Impulse library ZIP file:  
  `ei-car-detection-arduino-1.0.11-image-data,-image,-transfer-learning-(images).zip`

---

## Step 1 — Install Arduino IDE 2.x

1. Download Arduino IDE 2.x from [https://www.arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Install and launch it
3. Let it finish the initial setup/indexing

---

## Step 2 — Add ESP32 Board Package

1. Open **File → Preferences** (or `Ctrl + ,`)
2. In the **"Additional boards manager URLs"** field, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   (If you already have other URLs, separate them with commas)
3. Click **OK**
4. Open **Tools → Board → Boards Manager**
5. Search for **"esp32"**
6. Install **"esp32 by Espressif Systems"** version **2.x** (e.g., 2.0.17)
7. Wait for the installation to complete

---

## Step 3 — Add Edge Impulse ZIP Library

1. Open **Sketch → Include Library → Add .ZIP Library...**
2. Navigate to the project folder and select:
   ```
   ei-car-detection-arduino-1.0.11-image-data,-image,-transfer-learning-(images).zip
   ```
3. Arduino IDE will extract it into your libraries folder
4. You should see a confirmation message at the bottom of the IDE
5. **Verify**: Go to **Sketch → Include Library** — you should see the Edge Impulse library listed under "Contributed Libraries"

> ⚠ **Important:** The library might take up significant disk space (50–100 MB after extraction). Make sure you have enough storage.

---

## Step 4 — Flash Unit B First (Alert Board)

**Why Unit B first?** You need Unit B's MAC address to configure Unit A.

1. Connect the **ESP32 DevKit** (Unit B) to your computer via USB

2. In Arduino IDE:
   - **Board:** `Tools → Board → esp32 → ESP32 Dev Module`
   - **Port:** Select the correct COM port (e.g., `COM3`)
   - **Upload Speed:** `921600`
   - **Flash Frequency:** `80 MHz`
   - **Partition Scheme:** `Default 4MB with spiffs`

3. Open the file: `unit_b_alert/unit_b_alert.ino`

4. Click **Upload** (→ arrow button)

5. After upload completes, open **Tools → Serial Monitor**

6. Set baud rate to **115200**

7. Press the **EN** (reset) button on the ESP32

8. You should see output like this:
   ```
   =========================================
     Smart Helmet — Unit B (Alert Board)
   =========================================
   ─────────────────────────────────────────
     📋 This board's MAC: 24:6F:28:AA:BB:CC
     Copy this MAC into Unit A's config.h
   ─────────────────────────────────────────
   [NOW] ESP-NOW init OK
   [NOW] Receive callback registered

   [INFO] Unit B ready — waiting for alerts...
   =========================================
   ```

9. **Copy the MAC address** (e.g., `24:6F:28:AA:BB:CC`) — you'll need it in the next step

---

## Step 5 — Update Unit A's config.h with Unit B's MAC

1. Open `unit_a_helmet/config.h`

2. Find this line:
   ```cpp
   #define RECEIVER_MAC  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
   ```

3. Replace it with Unit B's actual MAC address. Convert each hex pair:
   ```
   MAC: 24:6F:28:AA:BB:CC
   ```
   becomes:
   ```cpp
   #define RECEIVER_MAC  {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}
   ```

4. Save the file

---

## Step 6 — Flash Unit A (ESP32-CAM)

1. Connect the **ESP32-CAM** to your computer
   - If using an FTDI programmer:
     - FTDI TX → ESP32-CAM U0R (GPIO 3)
     - FTDI RX → ESP32-CAM U0T (GPIO 1)
     - FTDI GND → ESP32-CAM GND
     - FTDI 5V → ESP32-CAM 5V
   - Connect **GPIO 0 → GND** (enables flash mode)

2. In Arduino IDE:
   - **Board:** `Tools → Board → esp32 → AI Thinker ESP32-CAM`
   - **Port:** Select the correct COM port
   - **Upload Speed:** `460800` (lower than DevKit due to FTDI)
   - **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`
     — the Edge Impulse model needs extra program space
   - **PSRAM:** `Enabled`

3. Open the file: `unit_a_helmet/unit_a_helmet.ino`

4. Click **Upload**

5. **After upload completes:**
   - **Disconnect GPIO 0 from GND** (exit flash mode)
   - Press the **RST** button on the ESP32-CAM

6. Open **Serial Monitor** at **115200** baud

7. Expected output:
   ```
   =========================================
     Smart Helmet — Unit A (ESP32-CAM)
   =========================================
   [INFO] MAC Address: 30:AE:A4:12:34:56
   [CAM] Camera OK
   [NOW] ESP-NOW init OK
   [NOW] Peer added
   [INFO] Unit A ready — entering main loop
   =========================================

   [AI] Top: background    Conf: 92.30%  | Vehicle: no
   [US] Distance: 245 cm
   [AI] Top: car           Conf: 85.10%  | Vehicle: YES
   [US] Distance: 120 cm
   ============================================
     ⚠ ALERT: Vehicle detected at 120 cm!
   ============================================
   [NOW] Send OK
   ```

---

## Step 7 — Verify Both Units Are Working

1. Keep **both boards powered** and both Serial Monitors open (use two IDE windows)

2. **Unit A** should print AI inference results and distance readings continuously

3. When a vehicle is detected close enough, Unit A sends an alert

4. **Unit B** should print:
   ```
   [NOW] Alert from 30:AE:A4:12:34:56
   [NOW] Type: 1  |  Distance: 120 cm
   ============================================
     ⚠ VEHICLE ALERT — 120 cm away!
   ============================================
   [MOTOR] Vibrating...
   [MOTOR] Done
   ```

5. The vibration motor should buzz for 600 ms

---

## Step 8 — Tuning CONFIDENCE_THRESHOLD

The `CONFIDENCE_THRESHOLD` in `unit_a_helmet/config.h` controls how sure the AI model must be before classifying something as a vehicle.

| Value | Behavior                                         | When to Use                     |
|:------|:-------------------------------------------------|:--------------------------------|
| 0.50  | More sensitive — catches more, more false alarms | Testing / low-traffic areas     |
| 0.70  | **Default** — good balance                       | Normal riding conditions        |
| 0.85  | Very strict — fewer alerts, may miss some        | High false-positive environment |
| 0.95  | Extremely strict — only very clear detections    | Controlled testing only         |

**How to tune:**
1. Ride with Serial Monitor logging (use Bluetooth serial module or log to SD card)
2. If you get **too many false alerts** → raise the threshold (e.g., 0.80)
3. If you're **missing real vehicles** → lower the threshold (e.g., 0.60)
4. Update the value in `config.h` and re-flash Unit A

---

## Step 9 — Tuning DANGER_DISTANCE_CM

The `DANGER_DISTANCE_CM` in `unit_a_helmet/config.h` sets the maximum distance at which an alert triggers.

| Value   | Range             | Scenario                            |
|:--------|:------------------|:------------------------------------|
| 100 cm  | ~3.3 feet         | Very close range only (tight turns) |
| 150 cm  | ~5 feet           | **Default** — urban cycling         |
| 200 cm  | ~6.5 feet         | Fast roads, more reaction time      |
| 300 cm  | ~10 feet          | Highway / high-speed scenarios      |

**How to tune:**
1. Start with the default **150 cm**
2. If alerts trigger **too late** (vehicle is very close before alert) → increase the distance
3. If alerts trigger **too early** (vibrator goes off for far-away vehicles) → decrease the distance
4. The HC-SR04 has a maximum reliable range of about **400 cm** — don't set it above that

---

## Quick Reference

| Setting               | File                     | Default | What It Does                     |
|:----------------------|:-------------------------|:--------|:---------------------------------|
| `CONFIDENCE_THRESHOLD`| `unit_a_helmet/config.h` | 0.70    | Min AI confidence for detection  |
| `DANGER_DISTANCE_CM`  | `unit_a_helmet/config.h` | 150     | Max alert distance (cm)          |
| `ALERT_COOLDOWN_MS`   | `unit_a_helmet/config.h` | 2000    | Pause between alerts (ms)        |
| `VIBRATE_DURATION_MS` | `unit_b_alert/config.h`  | 600     | Vibration buzz length (ms)       |
| `USE_BUZZER`          | `unit_b_alert/config.h`  | false   | Enable/disable buzzer            |
| `TRIG_PIN`            | `unit_a_helmet/config.h` | 12      | Ultrasonic trigger GPIO          |
| `ECHO_PIN`            | `unit_a_helmet/config.h` | 13      | Ultrasonic echo GPIO             |
| `VIBRATOR_PIN`        | `unit_b_alert/config.h`  | 4       | Motor transistor GPIO            |
| `BUZZER_PIN`          | `unit_b_alert/config.h`  | 5       | Passive buzzer GPIO              |
