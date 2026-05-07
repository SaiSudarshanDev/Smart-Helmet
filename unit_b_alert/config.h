/*
 * ============================================================
 *  config.h — Unit B (ESP32 DevKit) Configuration
 * ============================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Access Point ────────────────────────────────────────
#define AP_SSID               "SmartHelmet"
#define AP_PASS               "helmet123"
#define DASHBOARD_PORT        80

// ── ESP32-CAM stream URL ─────────────────────────────────────
#define CAM_STREAM_URL        "http://192.168.4.100:81/stream"

// ── Ultrasonic Sensor (HC-SR04) ──────────────────────────────
#define TRIG_PIN              12
#define ECHO_PIN              13

// Distance thresholds (in cm)
#define DANGER_DISTANCE_CM    50     // < 50cm  = DANGER (red, vibrate)
#define WARNING_DISTANCE_CM   150    // < 150cm = WARNING (amber)
// > 150cm = SAFE (green)

// ── Vibration Motor ──────────────────────────────────────────
#define VIBRATOR_PIN          4
#define VIBRATE_DURATION_MS   500

// ── Optional Buzzer ──────────────────────────────────────────
#define BUZZER_PIN            5
#define USE_BUZZER            false

// ── Timing ───────────────────────────────────────────────────
#define ALERT_COOLDOWN_MS     1500

// ── Serial ───────────────────────────────────────────────────
#define SERIAL_BAUD           115200

#endif // CONFIG_H
