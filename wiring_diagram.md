# 🔌 Wiring Diagram — Smart Helmet v2.0

> **Architecture:** Unit A (ESP32-CAM) handles **AI + camera stream** only.  
> Unit B (ESP32 DevKit) handles **WiFi AP, ultrasonic, alerts, and dashboard**.

---

## Unit A — ESP32-CAM (The Eye)

> **No external sensors!** Just the camera and power.  
> The ESP32-CAM connects to Unit B's WiFi and streams video.

| Component       | Component Pin   | ESP32-CAM Pin | Notes                              |
|:----------------|:----------------|:--------------|:-----------------------------------|
| **OV2640 Cam**  | Ribbon cable    | Built-in      | Pre-connected on module            |
| **Power**       | 5V              | 5V            | USB or power bank                  |
| **Power**       | GND             | GND           | Common ground                      |
| **FTDI (prog)** | TX              | U0R (GPIO 3)  | For programming only               |
| **FTDI (prog)** | RX              | U0T (GPIO 1)  | For programming only               |
| **FTDI (prog)** | GND             | GND           | Common ground with programmer      |

### ESP32-CAM Pinout Reference

```
              ┌─────────────────┐
              │    ┌───────┐    │
              │    │OV2640 │    │
              │    │CAMERA │    │
              │    └───────┘    │
              │                 │
        5V ──┤ 5V         GND ├── GND
       3V3 ──┤ 3V3      GPIO0 ├── (LOW=flash mode)
  (prog)TX ──┤ U0R      GPIO4 ├── (built-in flash LED)
  (prog)RX ──┤ U0T     GPIO16 ├──
              │                 │
              └─────────────────┘
```

> ⚠ **GPIO 0** must be connected to **GND** during upload, then **disconnected** for normal boot.

---

## Unit B — ESP32 DevKit (The Brain)

> **Everything else lives here:** ultrasonic sensor, vibration motor, buzzer, WiFi AP.

| Component          | Component Pin  | ESP32 Pin  | Notes                                     |
|:-------------------|:---------------|:-----------|:------------------------------------------|
| **HC-SR04**        | VCC            | 5V (VIN)   | Sensor needs 5V supply                    |
| **HC-SR04**        | GND            | GND        | Common ground                             |
| **HC-SR04**        | TRIG           | GPIO 12    | Trigger pulse output                      |
| **HC-SR04**        | ECHO           | GPIO 13    | Echo return input                         |
| **1kΩ Resistor**   | End A          | GPIO 4     | Current limiter for transistor base       |
| **1kΩ Resistor**   | End B          | NPN Base   | Connects to transistor                    |
| **NPN Transistor** | Base (B)       | ← 1kΩ     | Driven by GPIO 4                          |
| **NPN Transistor** | Emitter (E)    | GND        | Common ground                             |
| **NPN Transistor** | Collector (C)  | Motor (−)  | Switches motor ground path                |
| **Vibration Motor**| (+) Positive   | 5V (VIN)   | Motor power from 5V rail                  |
| **Vibration Motor**| (−) Negative   | Collector  | Switched by transistor                    |
| **Flyback Diode**  | Anode (A)      | Motor (−)  | Protects transistor from back-EMF         |
| **Flyback Diode**  | Cathode (K)    | Motor (+)  | Stripe side toward 5V                     |
| **Buzzer (opt.)**  | (+) Signal     | GPIO 5     | Optional passive buzzer                   |
| **Buzzer (opt.)**  | (−) GND        | GND        | Common ground                             |
| **Power**          | 5V / USB       | VIN / USB  | USB cable or power bank                   |
| **Power**          | GND            | GND        | Common ground                             |

---

## Full Circuit Schematic — Unit B

```
                              ╔══════════════════════════════╗
                              ║     ESP32 DevKit (38-pin)    ║
                              ║                              ║
                              ║   GPIO 12 ──── HC-SR04 TRIG  ║
                              ║   GPIO 13 ──── HC-SR04 ECHO  ║
                              ║   GPIO  4 ──── Motor Drive   ║
                              ║   GPIO  5 ──── Buzzer (+)    ║
                              ║   5V (VIN) ─── Power Rail    ║
                              ║   GND ──────── Ground Rail   ║
                              ╚══════════════════════════════╝


  ┌──────────── ULTRASONIC SENSOR ────────────┐
  │                                           │
  │    ┌─────────────────┐                    │
  │    │     HC-SR04      │                    │
  │    │                 │                    │
  │    │  VCC ──── 5V    │                    │
  │    │  TRIG ─── GPIO 12                    │
  │    │  ECHO ─── GPIO 13                    │
  │    │  GND ──── GND   │                    │
  │    └─────────────────┘                    │
  └───────────────────────────────────────────┘


  ┌──────────── VIBRATION MOTOR CIRCUIT ──────┐
  │                                           │
  │         +5V Rail                          │
  │           │                               │
  │     ┌─────┼──────────────┐                │
  │     │     │              │                │
  │     │  Motor (+)     Diode (K)            │
  │     │     │          cathode              │
  │     │  Motor (−)     Diode (A)            │
  │     │     │              │                │
  │     │     └──────┬───────┘                │
  │     │            │                        │
  │     │       Collector (C)                 │
  │     │            │                        │
  │     │        ╔═══╧═══╗                    │
  │     │        ║  NPN  ║                    │
  │     │        ║2N2222 ║                    │
  │     │        ║BC547  ║                    │
  │     │        ╚═══╤═══╝                    │
  │     │        B   │   E                    │
  │     │        │   │   │                    │
  │     │   [1kΩ]│   │   │                    │
  │     │        │   │   │                    │
  │     │   GPIO 4   │  GND                   │
  │     │            │                        │
  └─────┼────────────┼────────────────────────┘
        │            │
        │            │
  ┌─────┼────────────┼── OPTIONAL BUZZER ─────┐
  │     │            │                        │
  │     │   GPIO 5 ──┤── Buzzer (+)           │
  │     │            │                        │
  │     │      GND ──┤── Buzzer (−)           │
  │     │            │                        │
  └─────┼────────────┼────────────────────────┘
        │            │
```

---

## Detailed Transistor Circuit

```
                    +5V
                     │
              ┌──────┤
              │      │
             ═╤═   ──┤──
         D1  ═║═   │    │    Vibration
        1N4001║    │    │    Motor
             ═╧═   ──┤──
              │      │
              └──────┤
                     │
                Collector (C)
                     │
                 ┌───┤
                 │ ╔═╧═╗
   GPIO 4 ─[1kΩ]┤ ║ B ║  2N2222 / BC547
                 │ ╚═╤═╝
                 │   │
                 │ Emitter (E)
                 │   │
                 └───┤
                     │
                    GND
```

### Transistor Pinout (IMPORTANT — varies by model!)

```
  Looking at the FLAT side of the transistor:

  2N2222 (TO-92):         BC547 (TO-92):
  ┌─────────────┐         ┌─────────────┐
  │  E   B   C  │         │  C   B   E  │
  └──┤──┤──┤───┘         └──┤──┤──┤───┘
     │  │  │                │  │  │
     1  2  3                1  2  3

  ⚠ THEY ARE MIRRORED! Check your exact model's datasheet.
```

---

## Communication Diagram

```
  ┌──────────────────┐                    ┌──────────────────┐
  │   UNIT A          │                    │   UNIT B          │
  │   ESP32-CAM       │                    │   ESP32 DevKit    │
  │                   │                    │                   │
  │  ┌─────────────┐ │    ESP-NOW          │ ┌──────────────┐ │
  │  │ AI Inference │─┼──────────────────►─┼─│ Alert Logic   │ │
  │  └─────────────┘ │  Detection Packet   │ └──────┬───────┘ │
  │                   │                    │        │         │
  │  ┌─────────────┐ │    WiFi (STA)       │ ┌──────┴───────┐ │
  │  │ MJPEG Stream │─┼────────────────────┼─│ Dashboard    │ │
  │  └─────────────┘ │  192.168.4.100:81   │ │ 192.168.4.1  │ │
  │                   │                    │ └──────────────┘ │
  │  Power: 5V USB    │                    │                   │
  │  No sensors!      │                    │  HC-SR04 ─── GPIO 12,13 │
  │                   │                    │  Motor ───── GPIO 4     │
  │                   │                    │  Buzzer ──── GPIO 5     │
  │                   │                    │  WiFi AP: SmartHelmet   │
  └──────────────────┘                    └──────────────────┘

         📱 Phone / Laptop
         Connect to "SmartHelmet" WiFi
         Open http://192.168.4.1/
         See stream + detections + alerts
```

---

## Pin Summary

### Unit A (ESP32-CAM) — Minimal wiring
| Pin      | Function        |
|:---------|:----------------|
| 5V       | Power input     |
| GND      | Ground          |
| GPIO 0   | LOW = flash mode|

### Unit B (ESP32 DevKit) — All peripherals
| Pin      | Function                    |
|:---------|:----------------------------|
| GPIO 4   | Vibration motor (via NPN)   |
| GPIO 5   | Buzzer (optional)           |
| GPIO 12  | HC-SR04 TRIG                |
| GPIO 13  | HC-SR04 ECHO                |
| 5V / VIN | Power for motor + sensor    |
| GND      | Common ground               |

---

## Power Supply

| Unit   | Current (peak) | Recommended                    |
|:-------|:---------------|:-------------------------------|
| Unit A | ~250 mA        | USB power bank (separate)      |
| Unit B | ~150 mA + motor| USB power bank (separate)      |

> 💡 Use **separate USB power** for each unit to avoid voltage drops when the motor activates.
