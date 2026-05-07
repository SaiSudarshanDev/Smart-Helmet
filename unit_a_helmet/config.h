/*
 * ============================================================
 *  config.h — Unit A (ESP32-CAM) Configuration
 * ============================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// ── AI Model ─────────────────────────────────────────────────
#define CONFIDENCE_THRESHOLD  0.30f
#define INFERENCE_INTERVAL_MS 3000   // 3s between inferences (was 2s)

// ── Thermal Tuning ──────────────────────────────────────────
#define XCLK_MHZ              10    // sensor clock MHz (was 16 — big thermal win)
#define JPEG_QUALITY           15    // 10=best 63=worst (was 10 — less encoder work)
#define STREAM_FPS             12    // target stream fps (was 30 — huge heat savings)

// ── Pixel byte ordering ──────────────────────────────────────
// fmt2rgb888 output varies by ESP32 camera library version.
// Set to 1 to swap R↔B channels. Try both! (0 first, then 1)
// 0 = assume fmt2rgb888 outputs R,G,B bytes (standard)
// 1 = assume fmt2rgb888 outputs B,G,R bytes (some versions)
#define SWAP_RED_BLUE         1

// ── WiFi (connect to Unit B's AP) ────────────────────────────
#define AP_SSID               "SmartHelmet"
#define AP_PASS               "helmet123"

// Static IP so Unit B's dashboard can embed the stream
#define CAM_IP                192, 168, 4, 100
#define CAM_GATEWAY           192, 168, 4, 1
#define CAM_SUBNET            255, 255, 255, 0

// ── Stream Server ────────────────────────────────────────────
#define STREAM_PORT           81

// ── ESP-NOW ──────────────────────────────────────────────────
#define RECEIVER_MAC          {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

// ── Serial ───────────────────────────────────────────────────
#define SERIAL_BAUD           115200

#endif // CONFIG_H
