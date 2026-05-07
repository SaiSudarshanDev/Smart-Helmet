/*
 * ============================================================
 *  Unit B — ESP32 DevKit (The Brain) — DEMO READY
 * ============================================================
 *  Handles:
 *    1. Creates "SmartHelmet" WiFi AP
 *    2. Hosts web dashboard with live stream + overlays
 *    3. Reads HC-SR04 ultrasonic → PRIMARY collision detection
 *    4. Receives AI detection data from ESP32-CAM via ESP-NOW
 *    5. Triggers vibration motor + buzzer on proximity alert
 *
 *  Alert logic: Ultrasonic proximity triggers alerts INDEPENDENTLY.
 *  AI detection is shown as bonus data when available.
 *
 *  Dashboard:  http://192.168.4.1/
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include "config.h"

// ── Detection packet (must match Unit A) ─────────────────────
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

// ── Global state ─────────────────────────────────────────────
static DetectionPacket lastDetection;
static volatile long   ultraDist        = 0;
static volatile bool   alertActive      = false;
static volatile unsigned long lastAlertTime = 0;
static volatile bool   camConnected     = false;
static volatile unsigned long lastCamPacket = 0;
static volatile unsigned long lastVehicleTime = 0;  // when AI last saw a vehicle

// Danger zone: 0=safe, 1=warning, 2=danger
static volatile uint8_t dangerZone = 0;

static httpd_handle_t http_server = NULL;

// ──────────────────────────────────────────────────────────────
//  HC-SR04 distance measurement
// ──────────────────────────────────────────────────────────────
long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return 0;
  return dur / 58;
}

// ──────────────────────────────────────────────────────────────
//  Vibration motor + buzzer
// ──────────────────────────────────────────────────────────────
void triggerAlert() {
  Serial.println("[ALERT] ⚠ Vibrating!");
  digitalWrite(VIBRATOR_PIN, HIGH);
  if (USE_BUZZER) tone(BUZZER_PIN, 2000);
  delay(VIBRATE_DURATION_MS);
  digitalWrite(VIBRATOR_PIN, LOW);
  if (USE_BUZZER) noTone(BUZZER_PIN);
}

// ──────────────────────────────────────────────────────────────
//  ESP-NOW receive callback (v3.x)
// ──────────────────────────────────────────────────────────────
void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)sizeof(DetectionPacket)) return;

  memcpy(&lastDetection, data, sizeof(DetectionPacket));
  camConnected = true;
  lastCamPacket = millis();

  const uint8_t *m = info->src_addr;
  Serial.printf("[NOW] From %02X:%02X  dets=%d  vehicle=%d\n",
                m[4], m[5], lastDetection.count, lastDetection.vehicleFound);

  // Track when AI last detected a vehicle
  if (lastDetection.vehicleFound) {
    lastVehicleTime = millis();
    Serial.println("[NOW] 🚗 Vehicle confirmed by AI — ultrasonic ACTIVE");
  }
}

void initESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[NOW] Init FAILED"); return;
  }
  esp_now_register_recv_cb(onDataReceived);
  Serial.println("[NOW] Ready — listening for ESP32-CAM");
}

// ──────────────────────────────────────────────────────────────
//  Web Dashboard HTML — PRESENTATION READY
// ──────────────────────────────────────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Smart Helmet — Dashboard</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{
      background:#0a0a0a;color:#e0e0e0;
      font-family:'Segoe UI',system-ui,-apple-system,sans-serif;
      display:flex;flex-direction:column;align-items:center;
      min-height:100vh;padding:16px;
    }
    header{text-align:center;margin-bottom:12px}
    h1{font-size:1.6rem;color:#00ff88;letter-spacing:2px;font-weight:800}
    .sub{color:#555;font-size:0.78rem;margin-top:2px}

    .stream-wrap{
      position:relative;width:640px;max-width:100%;
      background:#111;border:2px solid #1a1a1a;border-radius:12px;
      overflow:hidden;aspect-ratio:4/3;
    }
    .stream-wrap img{width:100%;height:100%;object-fit:cover;display:block}
    .stream-wrap canvas{
      position:absolute;top:0;left:0;width:100%;height:100%;
      pointer-events:none;
    }
    .stream-wrap .no-cam{
      position:absolute;inset:0;display:flex;align-items:center;
      justify-content:center;color:#444;font-size:0.9rem;
      background:#0a0a0a;
    }

    /* Proximity bar */
    .prox-bar{
      width:640px;max-width:100%;height:8px;
      background:#1a1a1a;border-radius:4px;margin-top:8px;
      overflow:hidden;
    }
    .prox-fill{height:100%;border-radius:4px;transition:width .3s,background .3s}

    .alert-banner{
      display:none;width:640px;max-width:100%;
      background:rgba(255,50,50,0.15);border:1px solid #ff4444;
      border-radius:8px;padding:12px 20px;margin-top:10px;
      color:#ff6666;font-weight:700;text-align:center;font-size:1.1rem;
      animation:pulse .5s ease-in-out infinite alternate;
    }
    .alert-banner.show{display:block}
    @keyframes pulse{from{opacity:.6}to{opacity:1}}

    .warn-banner{
      display:none;width:640px;max-width:100%;
      background:rgba(255,170,0,0.10);border:1px solid #ffaa00;
      border-radius:8px;padding:10px 20px;margin-top:10px;
      color:#ffaa00;font-weight:600;text-align:center;
    }
    .warn-banner.show{display:block}

    .cards{
      display:grid;grid-template-columns:repeat(4,1fr);
      gap:10px;margin-top:12px;width:640px;max-width:100%;
    }
    .card{
      background:#141414;border:1px solid #222;border-radius:10px;
      padding:14px 10px;text-align:center;
      transition:border-color .3s;
    }
    .card.highlight{border-color:#ff4444;background:#1a0a0a}
    .card .lbl{font-size:.6rem;color:#666;text-transform:uppercase;letter-spacing:1.2px}
    .card .val{font-size:1.6rem;font-weight:700;margin-top:4px}
    .green{color:#00ff88} .red{color:#ff4444} .amber{color:#ffaa00} .dim{color:#555}

    .det-section{
      margin-top:12px;width:640px;max-width:100%;
    }
    .det-title{font-size:.7rem;color:#555;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
    .det-table{font-size:.75rem;color:#999}
    .det-table table{width:100%;border-collapse:collapse}
    .det-table th,.det-table td{padding:5px 8px;border-bottom:1px solid #1a1a1a;text-align:left}
    .det-table th{color:#555;text-transform:uppercase;font-size:.6rem}
    .bar{display:inline-block;height:5px;border-radius:3px;background:#00ff88}

    footer{margin-top:20px;color:#333;font-size:.6rem}
  </style>
</head>
<body>
  <header>
    <h1>🏍 SMART HELMET</h1>
    <p class="sub">AI Vehicle Detection • Ultrasonic Collision Warning</p>
  </header>

  <div class="stream-wrap">
    <img id="stream" alt="Camera">
    <canvas id="overlay"></canvas>
    <div class="no-cam" id="noCam">⏳ Waiting for ESP32-CAM stream...</div>
  </div>

  <!-- Proximity bar -->
  <div class="prox-bar"><div class="prox-fill" id="proxFill"></div></div>

  <div class="alert-banner" id="alertBanner">
    ⚠ COLLISION WARNING — Object dangerously close!
  </div>
  <div class="warn-banner" id="warnBanner">
    ⚡ CAUTION — Object approaching
  </div>

  <div class="cards">
    <div class="card" id="distCard">
      <div class="lbl">Distance</div>
      <div class="val green" id="vDist">—</div>
    </div>
    <div class="card">
      <div class="lbl">AI Detections</div>
      <div class="val green" id="vDet">0</div>
    </div>
    <div class="card">
      <div class="lbl">Status</div>
      <div class="val green" id="vStatus">Safe</div>
    </div>
    <div class="card">
      <div class="lbl">Camera</div>
      <div class="val dim" id="vCam">Offline</div>
    </div>
  </div>

  <div class="det-section">
    <div class="det-title">Detection Log</div>
    <div class="det-table">
      <table>
        <thead><tr><th>Source</th><th>Label</th><th>Confidence</th><th>Details</th></tr></thead>
        <tbody id="detRows"></tbody>
      </table>
    </div>
  </div>

  <footer>Smart Helmet v2.0 • ESP32 + ESP32-CAM • Ultrasonic + AI</footer>

  <script>
    const img    = document.getElementById('stream');
    const canvas = document.getElementById('overlay');
    const ctx    = canvas.getContext('2d');
    const noCam  = document.getElementById('noCam');
    const alertB = document.getElementById('alertBanner');
    const warnB  = document.getElementById('warnBanner');
    const proxF  = document.getElementById('proxFill');
    const distC  = document.getElementById('distCard');
    const vDist  = document.getElementById('vDist');
    const vDet   = document.getElementById('vDet');
    const vStat  = document.getElementById('vStatus');
    const vCam   = document.getElementById('vCam');
    const detRows= document.getElementById('detRows');

    const STREAM = 'CAM_STREAM_PLACEHOLDER';
    img.src = STREAM;
    img.onload = () => {
      noCam.style.display = 'none';
      vCam.textContent = 'Online';
      vCam.className = 'val green';
    };
    img.onerror = () => {
      noCam.style.display = 'flex';
      vCam.textContent = 'Offline';
      vCam.className = 'val dim';
      setTimeout(() => { img.src = STREAM + '?t=' + Date.now(); }, 3000);
    };

    setInterval(async () => {
      try {
        const r = await fetch('/api/data');
        const d = await r.json();

        // --- Distance display ---
        const dist = d.dist;
        if (dist > 0) {
          vDist.innerHTML = dist + '<small style="font-size:.6em"> cm</small>';
        } else {
          vDist.textContent = '—';
        }

        // --- Proximity bar ---
        const maxDist = 200;
        const pct = dist > 0 ? Math.min(100, (dist / maxDist) * 100) : 0;
        proxF.style.width = (100 - pct) + '%';

        // --- Danger zones ---
        const zone = d.zone; // 0=safe, 1=warning, 2=danger
        alertB.classList.toggle('show', zone === 2);
        warnB.classList.toggle('show', zone === 1);
        distC.classList.toggle('highlight', zone === 2);

        if (zone === 2) {
          vDist.className = 'val red';
          vStat.textContent = 'DANGER';
          vStat.className = 'val red';
          proxF.style.background = '#ff4444';
        } else if (zone === 1) {
          vDist.className = 'val amber';
          vStat.textContent = 'Warning';
          vStat.className = 'val amber';
          proxF.style.background = '#ffaa00';
        } else {
          vDist.className = 'val green';
          vStat.textContent = 'Safe';
          vStat.className = 'val green';
          proxF.style.background = '#00ff88';
        }

        // --- AI Detections ---
        vDet.textContent = d.count;

        // Camera status
        if (d.cam) { vCam.textContent = 'Online'; vCam.className = 'val green'; }

        // --- Canvas overlay ---
        const rect = img.getBoundingClientRect();
        canvas.width = rect.width;
        canvas.height = rect.height;
        const sx = rect.width / 640;
        const sy = rect.height / 480;
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        // Draw proximity warning overlay on stream when in danger
        if (zone === 2) {
          ctx.strokeStyle = '#ff4444';
          ctx.lineWidth = 4;
          ctx.setLineDash([10, 5]);
          ctx.strokeRect(4, 4, canvas.width - 8, canvas.height - 8);
          ctx.setLineDash([]);

          ctx.font = 'bold 18px sans-serif';
          const wt = '⚠ DANGER - TOO CLOSE';
          const m = ctx.measureText(wt);
          ctx.fillStyle = 'rgba(255,0,0,0.7)';
          ctx.fillRect(canvas.width/2 - m.width/2 - 10, 10, m.width + 20, 30);
          ctx.fillStyle = '#fff';
          ctx.fillText(wt, canvas.width/2 - m.width/2, 32);
        } else if (zone === 1) {
          ctx.strokeStyle = '#ffaa00';
          ctx.lineWidth = 2;
          ctx.setLineDash([8, 4]);
          ctx.strokeRect(4, 4, canvas.width - 8, canvas.height - 8);
          ctx.setLineDash([]);
        }

        // Draw AI detection bounding boxes
        let rows = '';
        (d.dets || []).forEach(det => {
          const bx = det.x * sx, by = det.y * sy;
          const bw = det.w * sx, bh = det.h * sy;
          const color = det.c >= 70 ? '#00ff88' : '#ffaa00';

          ctx.strokeStyle = color;
          ctx.lineWidth = 2;
          ctx.strokeRect(bx, by, bw, bh);

          // Corner brackets
          ctx.lineWidth = 3;
          const cn = 10;
          ctx.beginPath();
          ctx.moveTo(bx, by+cn); ctx.lineTo(bx,by); ctx.lineTo(bx+cn, by);
          ctx.moveTo(bx+bw-cn, by); ctx.lineTo(bx+bw, by); ctx.lineTo(bx+bw, by+cn);
          ctx.moveTo(bx+bw, by+bh-cn); ctx.lineTo(bx+bw, by+bh); ctx.lineTo(bx+bw-cn, by+bh);
          ctx.moveTo(bx+cn, by+bh); ctx.lineTo(bx, by+bh); ctx.lineTo(bx, by+bh-cn);
          ctx.stroke();

          const txt = det.l + ' ' + det.c + '%';
          ctx.font = 'bold 13px sans-serif';
          const tw = ctx.measureText(txt).width;
          ctx.fillStyle = 'rgba(0,0,0,0.75)';
          ctx.fillRect(bx, by-22, tw+10, 22);
          ctx.fillStyle = color;
          ctx.fillText(txt, bx+5, by-6);

          rows += '<tr><td style="color:#00ff88">🤖 AI</td><td style="color:'+color+'">'+det.l+'</td>'
                + '<td><span class="bar" style="width:'+det.c*.8+'px"></span> '+det.c+'%</td>'
                + '<td>'+det.x+','+det.y+' ('+det.w+'×'+det.h+')</td></tr>';
        });

        // Add ultrasonic row to detection log
        if (dist > 0) {
          const uColor = zone===2 ? '#ff4444' : zone===1 ? '#ffaa00' : '#00ff88';
          const uLabel = zone===2 ? 'DANGER' : zone===1 ? 'CAUTION' : 'CLEAR';
          rows += '<tr><td style="color:'+uColor+'">📡 Sonic</td><td style="color:'+uColor+'">'+uLabel+'</td>'
                + '<td><span class="bar" style="width:'+(Math.min(100,100-dist/2))+'px;background:'+uColor+'"></span> '+dist+' cm</td>'
                + '<td>Range: 2-400 cm</td></tr>';
        }

        detRows.innerHTML = rows || '<tr><td colspan="4" style="color:#333">No data</td></tr>';

      } catch(e) {}
    }, 250);
  </script>
</body>
</html>
)rawliteral";

// ──────────────────────────────────────────────────────────────
//  HTTP handlers
// ──────────────────────────────────────────────────────────────
static esp_err_t index_handler(httpd_req_t *req) {
  String html = String(DASHBOARD_HTML);
  html.replace("CAM_STREAM_PLACEHOLDER", CAM_STREAM_URL);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html.c_str(), html.length());
}

static esp_err_t api_handler(httpd_req_t *req) {
  char json[768];
  int off = 0;

  off += snprintf(json+off, sizeof(json)-off,
    "{\"dist\":%ld,\"count\":%d,\"zone\":%d,\"alert\":%s,\"cam\":%s,\"dets\":[",
    ultraDist,
    lastDetection.count,
    dangerZone,
    alertActive ? "true" : "false",
    camConnected ? "true" : "false");

  for (int i = 0; i < lastDetection.count && i < MAX_DET; i++) {
    if (i > 0) json[off++] = ',';
    off += snprintf(json+off, sizeof(json)-off,
      "{\"l\":\"%s\",\"c\":%d,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
      lastDetection.det[i].label,
      lastDetection.det[i].conf,
      lastDetection.det[i].x, lastDetection.det[i].y,
      lastDetection.det[i].w, lastDetection.det[i].h);
  }

  off += snprintf(json+off, sizeof(json)-off, "]}");

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, off);
}

void startDashboard() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = DASHBOARD_PORT;
  cfg.ctrl_port   = DASHBOARD_PORT + 1;
  cfg.max_uri_handlers = 8;
  cfg.stack_size  = 8192;

  httpd_uri_t idx = { .uri="/", .method=HTTP_GET,
                      .handler=index_handler, .user_ctx=NULL };
  httpd_uri_t api = { .uri="/api/data", .method=HTTP_GET,
                      .handler=api_handler, .user_ctx=NULL };

  if (httpd_start(&http_server, &cfg) == ESP_OK) {
    httpd_register_uri_handler(http_server, &idx);
    httpd_register_uri_handler(http_server, &api);
    Serial.printf("[HTTP] Dashboard → http://192.168.4.1:%d/\n", DASHBOARD_PORT);
  } else {
    Serial.println("[HTTP] FAILED");
  }
}

// ──────────────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("=========================================");
  Serial.println("  Unit B — ESP32 (The Brain) v2.0");
  Serial.println("=========================================");

  // Vibrator + buzzer pins
  pinMode(VIBRATOR_PIN, OUTPUT);
  digitalWrite(VIBRATOR_PIN, LOW);
  if (USE_BUZZER) {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Create WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);

  Serial.printf("[WiFi] AP: %s / %s\n", AP_SSID, AP_PASS);
  Serial.printf("[WiFi] IP: %s\n", WiFi.softAPIP().toString().c_str());

  // Init ESP-NOW
  initESPNow();

  // Start dashboard
  startDashboard();

  // Clear detection buffer
  memset(&lastDetection, 0, sizeof(lastDetection));

  Serial.println("\n[INFO] Unit B ready");
  Serial.printf("[INFO] Danger < %d cm | Warning < %d cm\n",
                DANGER_DISTANCE_CM, WARNING_DISTANCE_CM);
  Serial.println("=========================================\n");
}

// ──────────────────────────────────────────────────────────────
//  LOOP — AI-gated ultrasonic: only measure when vehicle detected
// ──────────────────────────────────────────────────────────────
#define VEHICLE_PERSIST_MS  5000  // keep ultrasonic active 5s after last AI detection

void loop() {
  unsigned long now = millis();

  // 1. Check if AI recently detected a vehicle
  bool vehicleActive = (lastVehicleTime > 0) && ((now - lastVehicleTime) < VEHICLE_PERSIST_MS);

  if (vehicleActive) {
    // 2. Vehicle detected by AI → measure distance
    long dist = getDistanceCM();
    ultraDist = dist;

    // 3. Determine danger zone
    if (dist > 0 && dist < DANGER_DISTANCE_CM) {
      dangerZone = 2;  // DANGER
    } else if (dist > 0 && dist < WARNING_DISTANCE_CM) {
      dangerZone = 1;  // WARNING
    } else {
      dangerZone = 0;  // SAFE (vehicle seen but far away)
    }

    // 4. Log
    if (dist > 0) {
      const char* zoneStr = dangerZone == 2 ? "DANGER" :
                            dangerZone == 1 ? "WARNING" : "SAFE";
      Serial.printf("[US] %ld cm [%s] (AI: vehicle in frame)\n", dist, zoneStr);
    }

    // 5. Alert if in danger zone
    if (dangerZone == 2 && (now - lastAlertTime) > ALERT_COOLDOWN_MS) {
      alertActive = true;
      lastAlertTime = now;

      Serial.println("╔════════════════════════════════════════╗");
      Serial.printf( "║  ⚠ COLLISION ALERT — %ld cm away!     ║\n", dist);
      Serial.println("║  AI + Ultrasonic confirmed threat      ║");
      Serial.println("╚════════════════════════════════════════╝");

      triggerAlert();
    } else if (dangerZone < 2) {
      alertActive = false;
    }

  } else {
    // No vehicle detected by AI → ultrasonic stays idle
    ultraDist  = 0;
    dangerZone = 0;
    alertActive = false;
  }

  // 6. Mark camera as disconnected if no packets for 10s
  if (camConnected && (now - lastCamPacket) > 10000) {
    camConnected = false;
    Serial.println("[CAM] No packets for 10s — marking offline");
  }

  delay(100);  // 10 Hz polling
}
