# 🔧 Troubleshooting Guide — Smart Helmet Collision Warning System

Common issues and their resolutions, organised by symptom.

---

## 1. Camera Init Failed

### Problem: "Camera init failed" on Serial Monitor

**Symptom:**  
Serial output shows:
```
[CAM] Camera init FAILED (0x20001)
```
or a similar error code. The AI inference never runs.

**Cause:**  
- The camera ribbon cable is loose or not properly seated
- PSRAM is not enabled in Arduino IDE settings
- Wrong board selected (must be AI Thinker ESP32-CAM)
- GPIO 0 is still grounded (flash mode), preventing normal boot
- Insufficient power supply (camera needs ~300 mA during init)

**Fix:**
1. **Check ribbon cable:** Power off, carefully reseat the OV2640 camera ribbon cable. The gold contacts should face the board's connector pads. Push the latch down firmly.
2. **Enable PSRAM:** In Arduino IDE, go to `Tools → PSRAM → Enabled`. The ESP32-CAM uses PSRAM for the frame buffer.
3. **Correct board:** Ensure `Tools → Board` is set to `AI Thinker ESP32-CAM` (not generic ESP32 Dev Module).
4. **Exit flash mode:** Disconnect GPIO 0 from GND. Press the RST button.
5. **Power supply:** Use a stable 5V supply capable of at least 500 mA. USB ports on some laptops may not provide enough current.

---

## 2. Ultrasonic Distance Always Reads 0 or Very Large Values

### Problem: Distance always reads 0 or 38000+

**Symptom:**  
Serial output shows:
```
[US] No echo (timeout)
```
or distance values like `38000 cm` every reading.

**Cause:**  
- HC-SR04 wiring is incorrect (TRIG/ECHO swapped)
- HC-SR04 is not receiving 5V power
- Echo pin is not connected or has a loose jumper wire
- GPIO 12 is being pulled HIGH on boot (causing boot failure on some ESP32-CAMs)
- The sensor is pointed at a surface too far away or too absorptive

**Fix:**
1. **Verify wiring:** Double-check that:
   - TRIG → GPIO 12
   - ECHO → GPIO 13
   - VCC → 5V (not 3.3V!)
   - GND → GND
2. **Test the sensor independently:** Use a simple Arduino sketch that only reads the HC-SR04 (no camera, no ESP-NOW) to isolate the problem.
3. **Check GPIO 12 boot issue:** GPIO 12 controls the flash voltage on ESP32. If it's pulled HIGH at boot, the ESP32 may not start properly. **Fix: Disconnect the TRIG wire from GPIO 12 during upload, reconnect after boot.**
4. **Verify power:** Measure voltage at the HC-SR04 VCC pin with a multimeter — it should be ~5V.
5. **Check sensor range:** Point the sensor at a flat surface within 30 cm. If it works close but not far, the sensor may be faulty.

---

## 3. ESP-NOW Send Always Shows "FAILED"

### Problem: ESP-NOW send callback reports failure

**Symptom:**  
Serial output shows:
```
[NOW] Send FAILED
```
on every alert attempt. Unit B never receives anything.

**Cause:**  
- Unit B is not powered on or has not initialised ESP-NOW
- The MAC address in Unit A's `config.h` is incorrect
- Both boards are not on the same WiFi channel
- ESP-NOW was not initialised before attempting to send
- The peer was not added successfully

**Fix:**
1. **Verify MAC address:** On Unit B, check the MAC printed at startup. Compare it character by character with `RECEIVER_MAC` in `unit_a_helmet/config.h`. A single wrong byte will cause every send to fail.
   ```
   Unit B says: 24:6F:28:AA:BB:CC
   Config must be: {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}
   ```
2. **Power both units:** ESP-NOW requires both boards to be powered and running.
3. **Check WiFi channel:** Both boards must be on the same WiFi channel (default: channel 0 = auto). If you're in a WiFi-heavy environment, try setting both to a specific channel (e.g., channel 1):
   ```cpp
   // In both units' initESPNow():
   WiFi.channel(1);
   ```
4. **Check init order:** `esp_now_init()` must be called before `esp_now_add_peer()` and `esp_now_send()`.
5. **Try broadcast first:** Temporarily set `RECEIVER_MAC` to `{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}` (broadcast) to test if ESP-NOW works at all.

---

## 4. Vibrator Never Triggers

### Problem: Vibration motor doesn't activate even when alerts are sent successfully

**Symptom:**  
Unit A shows `[NOW] Send OK`, Unit B shows the alert message and `[MOTOR] Vibrating...`, but the motor doesn't actually spin.

**Cause:**  
- Transistor wiring is incorrect (Base/Collector/Emitter pins wrong)
- 1 kΩ resistor is missing or too large
- Motor is dead or not powered
- GPIO 4 is not providing enough current
- Flyback diode is installed backwards

**Fix:**
1. **Verify transistor pinout:** Look up the datasheet for your specific transistor:
   - **2N2222 (TO-92):** Looking at the flat side — Emitter, Base, Collector (left to right)
   - **BC547 (TO-92):** Looking at the flat side — Collector, Base, Emitter (left to right)
   - **⚠ They have DIFFERENT pinouts!** Double-check yours.
2. **Test GPIO directly:** Temporarily connect the motor between GPIO 4 and GND (without transistor) with a 100Ω resistor. If it vibrates weakly, the GPIO works. (Don't leave it like this — the transistor is needed for proper current.)
3. **Check the 1 kΩ resistor:** Measure it with a multimeter. It should be 1000Ω ± 5%.
4. **Check motor polarity:** The (+) terminal goes to 5V, (−) terminal goes to the transistor's Collector.
5. **Check diode direction:** The flyback diode's cathode (the end with the stripe) must face the 5V rail. If it's backwards, it creates a short circuit.
6. **Measure voltages:**
   - With motor OFF: GPIO 4 should be ~0V, Collector should be ~5V
   - With motor ON: GPIO 4 should be ~3.3V, Collector should be near 0V

---

## 5. Model Runs But Never Detects Vehicles

### Problem: AI inference runs but never reports a vehicle detection

**Symptom:**  
Serial output always shows:
```
[AI] Top: background    Conf: 95.40%  | Vehicle: no
```
even when a car or vehicle is clearly in front of the camera.

**Cause:**  
- Camera is not aimed at the road / approaching vehicles
- Image is too dark or too bright (poor lighting)
- The Edge Impulse model was trained on different image dimensions
- The model's labels don't match the expected strings
- Frame is blurry due to vibration

**Fix:**
1. **Check camera aim:** The ESP32-CAM should face **rearward** (behind the cyclist) to detect approaching vehicles.
2. **Check lighting:** The OV2640 performs poorly in very low light. Test in daylight or well-lit conditions first.
3. **Verify model labels:** Open the Edge Impulse project dashboard and check the exact label names. They must match one of: `"vehicle"`, `"car"`, `"truck"`, `"motorcycle"`, `"bus"`. If your model uses different labels (e.g., `"Car"` with capital C), update the `strcmp()` calls in `runInference()`:
   ```cpp
   if (strcmp(label, "Car") == 0 ||   // match your actual label
   ```
4. **Verify image size:** The model must expect **96×96** input. Check this in your Edge Impulse project under **Impulse design → Image data → Input resolution**.
5. **Lower the threshold:** Temporarily set `CONFIDENCE_THRESHOLD` to `0.30f` to see if the model detects anything at all. If it does, gradually raise it.
6. **Test with a photo:** Hold a printed photo of a car in front of the camera at close range to verify the model works in ideal conditions.

---

## 6. GPIO 12/13 Conflict Causing Boot Loop

### Problem: ESP32-CAM enters a boot loop or fails to start

**Symptom:**  
Serial Monitor shows:
```
rst:0x10 (RTCWDT_RTC_RESET),boot:0x33 (SPI_FAST_FLASH_BOOT)
flash read err, 1000
```
or the board repeatedly resets every few seconds.

**Cause:**  
GPIO 12 controls the voltage of the internal flash memory on ESP32. If GPIO 12 is pulled HIGH at boot (e.g., by the HC-SR04's TRIG connection), the flash may switch to 1.8V mode and fail to read.

**Fix:**
1. **Option A — Disconnect during boot:** Unplug the TRIG wire from GPIO 12 before powering on. Reconnect it after the board has booted (you'll see the startup messages in Serial Monitor).
2. **Option B — Use efuse to fix flash voltage (permanent):**
   ```bash
   # Install esptool.py
   pip install esptool

   # Set the flash voltage to 3.3V permanently
   # (Run this ONCE — it burns an e-fuse and cannot be undone)
   python -m esptool --port COM3 set_flash_voltage 3.3V
   ```
   After this, GPIO 12 can be used freely without affecting boot.
3. **Option C — Add a pull-down resistor:** Place a 10 kΩ resistor between GPIO 12 and GND. This ensures GPIO 12 is LOW during boot, then the TRIG output overrides it during normal operation.
4. **Option D — Use different pins:** If none of the above work, remap TRIG/ECHO to GPIO 14 and GPIO 15 (also freed by disabling SD card). Update `config.h`:
   ```cpp
   #define TRIG_PIN  14
   #define ECHO_PIN  15
   ```

---

## 7. Board Not Showing Up in Arduino IDE Port List

### Problem: No COM port appears when the board is connected

**Symptom:**  
`Tools → Port` is greyed out or doesn't show the expected COM port.

**Cause:**  
- USB cable is charge-only (no data lines)
- USB-to-Serial driver is not installed
- The ESP32-CAM doesn't have a built-in USB-to-Serial chip (needs FTDI adapter)
- USB port or cable is faulty

**Fix:**
1. **Try a different cable:** Many USB cables are charge-only. Use a cable that you know supports data transfer (e.g., the one that came with a phone that syncs with a computer).
2. **Install the correct driver:**
   - **CP2102 chip** (most ESP32 DevKits): Download driver from [Silicon Labs](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
   - **CH340 chip** (some clones): Download driver from [WCH](http://www.wch-ic.com/downloads/CH341SER_EXE.html)
   - **FTDI FT232R** (external programmer): Download from [FTDI](https://ftdichip.com/drivers/vcp-drivers/)
3. **For ESP32-CAM without USB port:** You need an FTDI adapter (USB-to-Serial). Connect:
   - FTDI TX → ESP32-CAM RX (GPIO 3 / U0R)
   - FTDI RX → ESP32-CAM TX (GPIO 1 / U0T)
   - FTDI GND → ESP32-CAM GND
   - FTDI 5V → ESP32-CAM 5V
4. **Check Device Manager** (Windows):
   - Press `Win + X` → Device Manager
   - Look under "Ports (COM & LPT)"
   - If you see a device with a yellow warning icon, the driver needs updating
   - If nothing appears at all when plugging in, it's a cable issue
5. **Try a different USB port:** Avoid USB hubs. Connect directly to the computer.
6. **Restart Arduino IDE:** Sometimes the port list doesn't refresh. Close and reopen IDE after connecting the board.

---

## Quick Diagnostic Checklist

| # | Check                                    | How                                           |
|:--|:-----------------------------------------|:-----------------------------------------------|
| 1 | Board gets power                         | LED on the board lights up                     |
| 2 | COM port visible                         | Check Device Manager / Arduino IDE Ports       |
| 3 | Serial output appears                    | Open Serial Monitor at 115200 baud             |
| 4 | Camera initialises                       | Look for `[CAM] Camera OK`                     |
| 5 | ESP-NOW initialises                      | Look for `[NOW] ESP-NOW init OK`               |
| 6 | Peer is added                            | Look for `[NOW] Peer added`                    |
| 7 | AI runs                                  | Look for `[AI] Top: ...` lines                 |
| 8 | Distance reads correctly                 | Look for `[US] Distance: XX cm`                |
| 9 | Alert triggers                           | Look for `⚠ ALERT` message                    |
| 10| ESP-NOW send succeeds                    | Look for `[NOW] Send OK`                       |
| 11| Unit B receives alert                    | Look for `[NOW] Alert from ...`                |
| 12| Motor vibrates                           | Feel the vibration motor                       |

---

## Still Stuck?

1. **Simplify:** Test each component in isolation:
   - Camera only (use CameraWebServer example)
   - HC-SR04 only (use basic ultrasonic sketch)
   - ESP-NOW only (use simple sender/receiver example)
   - Motor only (use basic GPIO blink sketch)
2. **Check power:** Use a multimeter to verify 5V and 3.3V rails
3. **Check wiring:** Take a photo of your wiring and compare against the wiring diagram
4. **Update libraries:** Ensure `esp32` board package is version 2.0.x (not 3.x, which may have breaking changes)
