/*
 * ============================================================
 *  Unit A — ESP32-CAM (The Eye) — THERMAL OPTIMIZED
 * ============================================================
 *  Duties:
 *    1. Run Edge Impulse AI inference every 3 seconds
 *    2. Serve MJPEG stream on port 81
 *    3. Send detection results to Unit B via ESP-NOW
 *
 *  Thermal optimizations applied:
 *    - XCLK 16→8 MHz (halves sensor power draw)
 *    - JPEG quality 10→15 (less encoder work per frame)
 *    - Framebuffers 2→1 (sensor idles between captures)
 *    - Stream 30→12 fps (less CPU load)
 *    - Inference at QVGA 320×240 (4× less JPEG decode work)
 *    - Disabled AEC2/BPC/WPC DSP (less sensor-side heat)
 *    - PSRAM usage 921KB→230KB (less memory bandwidth)
 *    - Reduced serial output overhead
 *
 *  Arduino IDE settings:
 *    Board:      AI Thinker ESP32-CAM
 *    Partition:  Huge APP (3MB No OTA/1MB SPIFFS)
 *    PSRAM:      Enabled
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

#define EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN  0
#include <DrWhiteFusion-project-1_inferencing.h>
#include "config.h"

// ── Camera pins (AI-Thinker) ─────────────────────────────────
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ── Detection packet sent to Unit B ──────────────────────────
#define MAX_DET 5

typedef struct __attribute__((packed)) {
  uint8_t count;
  uint8_t vehicleFound;
  struct __attribute__((packed)) {
    char    label[12];
    uint8_t conf;
    uint16_t x, y, w, h;
  } det[MAX_DET];
} DetectionPacket;

// ── Globals ──────────────────────────────────────────────────
static uint8_t receiverMAC[] = RECEIVER_MAC;

#define STREAM_W 640
#define STREAM_H 480
#define INFER_W  320              // QVGA for inference (4× less decode work)
#define INFER_H  240
#define EI_W     EI_CLASSIFIER_INPUT_WIDTH
#define EI_H     EI_CLASSIFIER_INPUT_HEIGHT

static uint8_t *rgb_buf = NULL;   // QVGA decode buffer (230KB vs 921KB)
static uint8_t *ei_buf  = NULL;   // EI-sized buffer (PSRAM)

static httpd_handle_t stream_httpd = NULL;
static SemaphoreHandle_t cam_mutex = NULL;

static unsigned long lastInferenceTime = 0;
static const uint32_t STREAM_DELAY_MS = 1000 / STREAM_FPS;

// Stream constants
#define STREAM_BOUNDARY "frameboundary"
static const char *STREAM_CT =
    "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY;
static const char *STREAM_PART =
    "--" STREAM_BOUNDARY "\r\n"
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ──────────────────────────────────────────────────────────────
//  Camera init — THERMAL OPTIMIZED
// ──────────────────────────────────────────────────────────────
void initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM;  cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM;  cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM;  cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM;  cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;

  // ★ 8 MHz XCLK — halves sensor power vs 16 MHz
  cfg.xclk_freq_hz = XCLK_MHZ * 1000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_VGA;

  // ★ Quality 15 — reduces encoder workload (was 10)
  cfg.jpeg_quality = JPEG_QUALITY;
  // ★ Single framebuffer — sensor STOPS between captures
  cfg.fb_count     = 1;
  cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  // IMPORTANT: GRAB_WHEN_EMPTY is required for fb_count=1
  // GRAB_LATEST deadlocks with a single buffer!
  cfg.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;

  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[CAM] FAILED"); return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    // ★ Disable heavy DSP features to reduce sensor heat
    s->set_aec2(s, 0);
    s->set_bpc(s, 0);
    s->set_wpc(s, 0);
  }

  Serial.printf("[CAM] OK (%d MHz, VGA, quality=%d, 1 fb)\n", XCLK_MHZ, JPEG_QUALITY);

  for (int i = 0; i < 3; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(150);
  }
}

// ──────────────────────────────────────────────────────────────
//  MJPEG stream handler (runs on HTTP server thread)
// ──────────────────────────────────────────────────────────────
static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CT);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part[128];
  Serial.println("[STREAM] Client connected");

  while (true) {
    if (xSemaphoreTake(cam_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
      delay(20);
      continue;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    xSemaphoreGive(cam_mutex);
    if (!fb) { delay(20); continue; }

    size_t hlen = snprintf(part, sizeof(part), STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;

    // ★ Throttled to STREAM_FPS (default 12 fps, was 30)
    delay(STREAM_DELAY_MS);
  }
  Serial.println("[STREAM] Client disconnected");
  return res;
}

// Redirect root "/" to dashboard
static esp_err_t root_redirect_handler(httpd_req_t *req) {
  const char html[] =
    "<!DOCTYPE html><html><head>"
    "<meta http-equiv='refresh' content='0;url=http://192.168.4.1/'>"
    "<style>body{background:#0a0a0a;color:#00ff88;font-family:sans-serif;"
    "display:flex;justify-content:center;align-items:center;height:100vh}</style>"
    "</head><body>"
    "<p>Redirecting to <a href='http://192.168.4.1/' style='color:#00ff88'>"
    "Smart Helmet Dashboard</a>...</p>"
    "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, strlen(html));
}

void startStreamServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = STREAM_PORT;
  cfg.ctrl_port   = STREAM_PORT + 1;
  cfg.stack_size  = 8192;
  cfg.max_uri_handlers = 4;

  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET,
                      .handler = stream_handler, .user_ctx = NULL };
  httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET,
                      .handler = root_redirect_handler, .user_ctx = NULL };

  if (httpd_start(&stream_httpd, &cfg) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &root_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("[STREAM] OK → http://192.168.4.100:%d/stream\n", STREAM_PORT);
  } else {
    Serial.println("[STREAM] FAILED");
  }
}

// ──────────────────────────────────────────────────────────────
//  Resize helper: nearest-neighbour
// ──────────────────────────────────────────────────────────────
void resizeRGB(const uint8_t *src, int sw, int sh,
               uint8_t *dst, int dw, int dh) {
  for (int y = 0; y < dh; y++) {
    int sy = y * sh / dh;
    for (int x = 0; x < dw; x++) {
      int sx = x * sw / dw;
      int si = (sy * sw + sx) * 3;
      int di = (y * dw + x) * 3;
      dst[di] = src[si]; dst[di+1] = src[si+1]; dst[di+2] = src[si+2];
    }
  }
}

// ──────────────────────────────────────────────────────────────
//  EI signal callback — uses SWAP_RED_BLUE from config.h
//
//  Edge Impulse expects pixels packed as: 0x00RRGGBB
//  fmt2rgb888 byte order varies by library version:
//    SWAP_RED_BLUE=0 → buf = R,G,B (standard)
//    SWAP_RED_BLUE=1 → buf = B,G,R (some versions)
// ──────────────────────────────────────────────────────────────
static int ei_get_data(size_t offset, size_t length, float *out) {
  size_t px = offset * 3;
  for (size_t i = 0; i < length; i++) {
#if SWAP_RED_BLUE == 1
    // Buffer is B,G,R → pack as (R << 16) | (G << 8) | B
    out[i] = (ei_buf[px + 2] << 16) | (ei_buf[px + 1] << 8) | ei_buf[px];
#else
    // Buffer is R,G,B → pack as (R << 16) | (G << 8) | B
    out[i] = (ei_buf[px] << 16) | (ei_buf[px + 1] << 8) | ei_buf[px + 2];
#endif
    px += 3;
  }
  return 0;
}

// ──────────────────────────────────────────────────────────────
//  ESP-NOW
// ──────────────────────────────────────────────────────────────
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t st) {
  // Only log failures to reduce serial spam
  if (st != ESP_NOW_SEND_SUCCESS) Serial.println("[NOW] SEND FAIL");
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] Init FAILED"); return;
  }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;

  esp_now_add_peer(&peer);
  Serial.println("[NOW] Ready (broadcast)");
}

// ──────────────────────────────────────────────────────────────
//  Run AI inference and send results to Unit B
// ──────────────────────────────────────────────────────────────
void runInferenceAndSend() {
  if (!rgb_buf || !ei_buf) return;

  // 1. Grab mutex, switch to QVGA, capture, restore VGA, release
  if (xSemaphoreTake(cam_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    Serial.println("[AI] Camera busy");
    return;
  }

  // ★ Switch to QVGA — 4× less JPEG data to decode
  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_framesize(s, FRAMESIZE_QVGA);

  // Flush the stale (VGA-sized) frame from the single buffer
  camera_fb_t *stale = esp_camera_fb_get();
  if (stale) esp_camera_fb_return(stale);

  // Capture fresh QVGA frame
  camera_fb_t *fb = esp_camera_fb_get();

  // Restore VGA for stream
  if (s) s->set_framesize(s, FRAMESIZE_VGA);

  // Flush so next stream frame is a clean VGA frame
  camera_fb_t *flush = esp_camera_fb_get();
  if (flush) esp_camera_fb_return(flush);

  xSemaphoreGive(cam_mutex);

  if (!fb) { Serial.println("[AI] Capture failed"); return; }

  // 2. Decode QVGA JPEG → RGB888 (230KB vs 921KB at VGA)
  bool ok = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf);
  esp_camera_fb_return(fb);
  if (!ok) { Serial.println("[AI] Decode failed"); return; }

  // 3. Resize QVGA → EI input size
  resizeRGB(rgb_buf, INFER_W, INFER_H, ei_buf, EI_W, EI_H);

  // 4. Classify
  ei::signal_t sig;
  sig.total_length = EI_W * EI_H;
  sig.get_data = &ei_get_data;

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&sig, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("[AI] ERROR: %d\n", err);
    return;
  }

  // 5. Build detection packet
  DetectionPacket pkt;
  memset(&pkt, 0, sizeof(pkt));
  int idx = 0;

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  float scx = (float)STREAM_W / EI_W;
  float scy = (float)STREAM_H / EI_H;

  for (size_t i = 0; i < result.bounding_boxes_count && idx < MAX_DET; i++) {
    auto &bb = result.bounding_boxes[i];
    if (bb.value < CONFIDENCE_THRESHOLD) continue;

    strncpy(pkt.det[idx].label, bb.label, 11);
    pkt.det[idx].conf = (uint8_t)(bb.value * 100);
    pkt.det[idx].x = (uint16_t)(bb.x * scx);
    pkt.det[idx].y = (uint16_t)(bb.y * scy);
    pkt.det[idx].w = (uint16_t)(bb.width * scx);
    pkt.det[idx].h = (uint16_t)(bb.height * scy);

    if (strcmp(bb.label, "cars") == 0 || strcmp(bb.label, "car") == 0 ||
        strcmp(bb.label, "vehicle") == 0 || strcmp(bb.label, "truck") == 0 ||
        strcmp(bb.label, "motorcycle") == 0 || strcmp(bb.label, "bus") == 0) {
      pkt.vehicleFound = 1;
    }
    Serial.printf("[AI] %s: %.0f%%\n", bb.label, bb.value * 100);
    idx++;
  }

#else
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT && idx < MAX_DET; i++) {
    float c = result.classification[i].value;
    const char *l = result.classification[i].label;
    if (c < CONFIDENCE_THRESHOLD) continue;

    if (strcmp(l, "cars") == 0 || strcmp(l, "car") == 0 ||
        strcmp(l, "vehicle") == 0 || strcmp(l, "truck") == 0 ||
        strcmp(l, "motorcycle") == 0 || strcmp(l, "bus") == 0) {
      pkt.vehicleFound = 1;
      strncpy(pkt.det[idx].label, l, 11);
      pkt.det[idx].conf = (uint8_t)(c * 100);
      pkt.det[idx].x = 10; pkt.det[idx].y = 10;
      pkt.det[idx].w = STREAM_W - 20; pkt.det[idx].h = STREAM_H - 20;
      Serial.printf("[AI] ✓ %s %.0f%%\n", l, c * 100);
      idx++;
    }
  }
#endif

  pkt.count = idx;

  // 6. Send to Unit B
  esp_err_t sendResult = esp_now_send(receiverMAC, (uint8_t*)&pkt, sizeof(pkt));
  if (sendResult == ESP_OK) {
    Serial.printf("[NOW] count=%d vehicle=%d\n", pkt.count, pkt.vehicleFound);
  } else {
    Serial.printf("[NOW] err=%d\n", sendResult);
  }
}

// ──────────────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("=========================================");
  Serial.println("  Unit A — ESP32-CAM (The Eye)");
  Serial.println("=========================================");
  Serial.printf("  Inference interval: %d ms\n", INFERENCE_INTERVAL_MS);
  Serial.printf("  Confidence threshold: %.0f%%\n", CONFIDENCE_THRESHOLD * 100);
  Serial.printf("  EI input: %dx%d\n", EI_W, EI_H);
  Serial.printf("  EI labels: %d\n", EI_CLASSIFIER_LABEL_COUNT);

  cam_mutex = xSemaphoreCreateMutex();

  // ★ QVGA-sized buffers (230KB vs 921KB for VGA decode)
  rgb_buf = (uint8_t*)ps_malloc(INFER_W * INFER_H * 3);
  ei_buf  = (uint8_t*)ps_malloc(EI_W * EI_H * 3);
  Serial.printf("[MEM] rgb=%dKB  ei=%dB  heap=%dKB  %s\n",
                (INFER_W*INFER_H*3)/1024, EI_W*EI_H*3,
                ESP.getFreeHeap()/1024,
                (rgb_buf && ei_buf) ? "OK" : "FAILED!");

  // Init camera
  initCamera();

  // Connect to Unit B's AP
  WiFi.mode(WIFI_STA);
  IPAddress ip(CAM_IP);
  IPAddress gw(CAM_GATEWAY);
  IPAddress sn(CAM_SUBNET);
  WiFi.config(ip, gw, sn);
  WiFi.begin(AP_SSID, AP_PASS);

  Serial.printf("[WiFi] Connecting to '%s'", AP_SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500); Serial.print("."); tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] FAILED — will retry in background");
  }

  // Init ESP-NOW
  initESPNow();

  // Start stream server
  startStreamServer();

  Serial.println("\n[INFO] Unit A ready");
  Serial.println("=========================================\n");
}

// ──────────────────────────────────────────────────────────────
//  LOOP — inference every few seconds, NOT every 100ms
// ──────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  if (now - lastInferenceTime >= INFERENCE_INTERVAL_MS) {
    lastInferenceTime = now;

    unsigned long t0 = millis();
    runInferenceAndSend();
    Serial.printf("[AI] %lu ms | heap %dKB\n", millis() - t0, ESP.getFreeHeap()/1024);
  }

  // ★ Longer yield — reduces idle CPU heat
  delay(50);
}
