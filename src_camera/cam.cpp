#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "claw_mqtt_connection.h"
#include "firmware_config.h"
//Note: Ich muss es noch auf einem anderen Setup testen, da mein USB Kabel glaube ich schuld daran ist, dass sich der Kamera ESP nicht mehr connected.

const char* ssid = CLAW_CLIENT_WIFI_SSID ;
const char* password = CLAW_CLIENT_WIFI_PASSWORD;


WebServer server(80);


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


#define FLASH_LED_GPIO     4

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;


  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;     
    config.jpeg_quality = 10;             
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;    
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Kamera-Initialisierung fehlgeschlagen: 0x%x\n", err);
    return;
  }

  Serial.println("Kamera erfolgreich gestartet.");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32-CAM</title>
</head>
<body>
  <h1>ESP32-CAM</h1>

  <p><a href="/capture">Einzelbild anzeigen</a></p>
  <p><a href="/stream">Livestream starten</a></p>
  <p><a href="/data">Kameradaten anzeigen</a></p>

  <h2>Stream:</h2>
  <img src="/stream" width="640">
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "text/plain", "Kamera konnte kein Bild aufnehmen.");
    return;
  }

  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  server.send(200);

  WiFiClient client = server.client();
  client.write(fb->buf, fb->len);

  esp_camera_fb_return(fb);
}

void handleData() {
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    server.send(500, "application/json", "{\"error\":\"Kein Frame erhalten\"}");
    return;
  }

  String json = "{";
  json += "\"width\":" + String(fb->width) + ",";
  json += "\"height\":" + String(fb->height) + ",";
  json += "\"length_bytes\":" + String(fb->len) + ",";
  json += "\"format\":" + String(fb->format) + ",";
  json += "\"timestamp_sec\":" + String(fb->timestamp.tv_sec) + ",";
  json += "\"timestamp_usec\":" + String(fb->timestamp.tv_usec);
  json += "}";

  esp_camera_fb_return(fb);

  server.send(200, "application/json", json);
}

void handleStream() {
  WiFiClient client = server.client();

  String header = "HTTP/1.1 200 OK\r\n";
  header += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
  header += "Cache-Control: no-cache\r\n";
  header += "Connection: close\r\n\r\n";

  client.print(header);

  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Frame konnte nicht gelesen werden.");
      break;
    }

    client.print("--frame\r\n");
    client.print("Content-Type: image/jpeg\r\n");
    client.print("Content-Length: ");
    client.print(fb->len);
    client.print("\r\n\r\n");

    client.write(fb->buf, fb->len);
    client.print("\r\n");

    esp_camera_fb_return(fb);

    delay(50); 
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(FLASH_LED_GPIO, OUTPUT);
  digitalWrite(FLASH_LED_GPIO, LOW);

  startCamera();

  WiFi.begin(ssid, password);
  Serial.print("Verbinde mit WLAN");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WLAN verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/stream", handleStream);
  server.on("/data", handleData);

  server.begin();
  Serial.println("Webserver gestartet.");
}

void loop() {
  server.handleClient();
}

