# 🚴‍♂️ Smart Helmet Collision Warning System

![Smart Helmet Architecture Circuit Diagram](assets/poster.png)

An advanced, distributed smart helmet system designed to enhance cyclist and rider safety. The project utilizes edge AI and sensor fusion to detect approaching vehicles and alert the rider through haptic feedback before a potential collision occurs.

## 🌟 Project Overview

The Smart Helmet system uses a dual-microcontroller architecture to balance high-intensity AI processing with real-time sensor polling and user interaction. It effectively combines computer vision (via an Edge Impulse FOMO model) with ultrasonic distance sensing.

The system is split into two independent but interconnected units:
- **Unit A (The Eye)**: An ESP32-CAM module dedicated to visual processing.
- **Unit B (The Brain)**: An ESP32 DevKit module responsible for sensor inputs, haptic feedback, and network hosting.

By isolating the heavy computer vision task to Unit A and the hardware control and networking to Unit B, the system ensures maximum performance, preventing UI lag and missed alerts.

---

## 🏗️ System Architecture

### 1. Unit A — The Eye (ESP32-CAM)
This unit is mounted on the rear of the helmet, facing backward to monitor approaching traffic.

**Core Responsibilities:**
- **Camera Capture:** Captures video frames using the built-in OV2640 camera.
- **Edge AI Inference:** Runs a custom Edge Impulse FOMO (Faster Objects, More Objects) machine learning model (`ei-drwhitefusion-project-1-arduino` / `ei-car-detection`) to detect vehicles (cars, trucks, motorcycles, buses) in real-time.
- **MJPEG Streaming:** Connects to Unit B's WiFi Access Point and streams the live video feed (default IP: `192.168.4.100:81/stream`).
- **ESP-NOW Communication:** When a vehicle is detected with a confidence level exceeding the set threshold (e.g., `0.70`), it broadcasts an alert packet directly to Unit B using the low-latency ESP-NOW protocol.

**Hardware Setup:**
- ESP32-CAM module with OV2640 camera.
- Powered independently via a 5V USB power bank.
- Requires an FTDI adapter to flash code initially.

### 2. Unit B — The Brain (ESP32 DevKit)
This unit is mounted either inside the helmet or somewhere easily accessible to the rider, handling logic, alerts, and the dashboard.

**Core Responsibilities:**
- **Distance Sensing:** Constantly polls an HC-SR04 Ultrasonic Sensor to calculate the exact distance of objects behind the rider.
- **Sensor Fusion Logic:** An alert is only fired if both conditions are met:
  1. The AI (Unit A) confirms a vehicle is in the frame.
  2. The Ultrasonic Sensor confirms the object is within the `DANGER_DISTANCE_CM` threshold (e.g., < 150 cm).
- **Haptic & Audio Alerts:** Triggers a vibration motor via an NPN transistor circuit (and an optional passive buzzer) to warn the rider.
- **WiFi Access Point & Dashboard:** Hosts a WiFi AP (SSID: `SmartHelmet`) and serves a web dashboard (at `http://192.168.4.1/`) where the user can view the live camera stream and active alerts on their smartphone.

**Hardware Setup:**
- ESP32 DevKit module.
- HC-SR04 Ultrasonic Sensor.
- Vibration Motor (driven by a 2N2222 or BC547 NPN transistor + flyback diode).
- Optional passive buzzer.

---

## 🚀 Key Features

1. **Distributed Computing System:** The ESP32-CAM (Unit A) runs heavy Edge AI workloads while the ESP32 DevKit (Unit B) manages real-time sensors and networking.
2. **Sensor Fusion:** Reduces false positives drastically. A wall or tree might trigger the distance sensor, but the AI won't classify it as a vehicle. A car far away will be detected by AI but won't trigger the distance alert until it gets too close.
3. **Ultra-Low Latency Alerts:** Uses ESP-NOW (a peer-to-peer MAC-layer protocol) to transmit alerts from Unit A to Unit B in milliseconds, without relying on standard WiFi overhead.
4. **Thermal & Performance Optimization:** Overheating is avoided by tuning the camera XCLK to 10 MHz, decreasing target FPS, and reducing JPEG quality during inference tasks.
5. **Real-time Mobile Dashboard:** Riders can connect their phones to the helmet's WiFi network to check the camera feed, review detections, and adjust positioning.

---

## 📂 Codebase Structure

The project is structured into specific firmware directories for each microcontroller:

- **`unit_a_helmet/`**: Firmware for the ESP32-CAM.
  - `config.h`: Contains AI thresholds, WiFi settings, static IP config, thermal tuning (XCLK, FPS), and the target MAC address of Unit B.
  - `unit_a_helmet.ino`: The main Arduino sketch initializing the camera, connecting to WiFi, running the Edge Impulse model, and broadcasting ESP-NOW packets.

- **`unit_b_alert/`**: Firmware for the ESP32 DevKit.
  - `config.h`: Contains AP settings, ultrasonic sensor pins (`TRIG_PIN`, `ECHO_PIN`), motor/buzzer pins, and distance tuning logic (`DANGER_DISTANCE_CM`).
  - `unit_b_alert.ino`: The main logic for handling ultrasonic sensor reading, receiving ESP-NOW packets, firing hardware alerts (motors), and hosting the web dashboard.

- **`ei-drwhitefusion-project-1-arduino-1.0.2-impulse-#1.zip`**: The exported Edge Impulse Arduino library containing the trained neural network.

- **Documentation**:
  - `setup_guide.md`: Step-by-step flashing, configuration, and tuning instructions.
  - `wiring_diagram.md`: Comprehensive ASCII schematics for pinning out both microcontrollers, including the transistor motor circuit.
  - `troubleshooting.md`: Diagnoses common ESP32-CAM initialization errors, ESP-NOW failures, and sensor tuning issues.

---

## ⚙️ Configuration & Tuning

The system can be easily adapted to different riding environments using the `config.h` files:

- **Sensitivity:** Adjust `CONFIDENCE_THRESHOLD` (Unit A) to make the AI more or less strict about what counts as a vehicle. (Default: 0.70)
- **Warning Distance:** Adjust `DANGER_DISTANCE_CM` (Unit B) to change how early the system warns you. (Default: 150 cm / 5 feet)
- **Feedback Duration:** Modify `VIBRATE_DURATION_MS` (Unit B) to make the haptic buzz shorter or longer.

## 🏁 Summary

By offloading the Edge ML pipeline to an independent camera module and utilizing ESP-NOW for sub-millisecond inter-process communication, the Smart Helmet achieves what would typically require a much more expensive single-board computer (like a Raspberry Pi). It offers a robust, highly modular, and power-efficient safety system for the modern rider.
