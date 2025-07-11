#include <IDP_SMART_LIVESTOCK_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>

// WiFi credentials
const char* ssid = "Kiss me";
const char* password = "Emiliya0608";

// Google Sheets script URL
const char* googleScriptURL = "https://script.google.com/macros/s/AKfycbxD5KiDTbjqEZrjp-UnGANamEv02w2h5WgdEpwEX4VdppjwhGpxXGmC6e03fjZ2H7Kzgg/exec";

// Web server for camera stream
WebServer server(80);

// Camera model selection
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
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
#else
#error "Camera model not selected"
#endif

#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE         3

static bool debug_nn = false;
static bool is_initialised = false;
uint8_t* snapshot_buf;
String localIP;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

// Function declarations
bool ei_camera_init(void);
void connectToWiFi();
void sendToGoogleSheets(float aog, float hg, float og);
void handleRoot();
void handleStream();

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println("Edge Impulse Inferencing Demo");

    if (!ei_camera_init()) {
        Serial.println("Failed to initialize camera!");
        return;
    }

    connectToWiFi();

    server.on("/", handleRoot);
    server.on("/stream", handleStream);
    server.begin();

    Serial.println("HTTP server started");
    delay(2000);
}

void loop() {
    server.handleClient();

    static unsigned long lastInferenceTime = 0;
    if (millis() - lastInferenceTime >= 5000) {
        lastInferenceTime = millis();

        snapshot_buf = (uint8_t*)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
        if (!snapshot_buf) {
            Serial.println("ERR: Failed to allocate snapshot buffer!");
            return;
        }

        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
        signal.get_data = &ei_camera_get_data;

        if (!ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
            Serial.println("Failed to capture image");
            free(snapshot_buf);
            return;
        }

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
        if (err != EI_IMPULSE_OK) {
            Serial.printf("ERR: Failed to run classifier (%d)\n", err);
            free(snapshot_buf);
            return;
        }

        float aog = 0.0, hg = 0.0, og = 0.0;

        Serial.println("Predictions:");
        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            String label = ei_classifier_inferencing_categories[i];
            float value = result.classification[i].value;

            Serial.printf("Raw label = '%s' | Confidence = %.5f\n", label.c_str(), value);

            String cleanLabel = label;
            cleanLabel.toLowerCase();
            cleanLabel.replace(" ", "");

            if (cleanLabel == "almostovergrazed") aog = value;
            else if (cleanLabel == "healthygrass") hg = value;
            else if (cleanLabel == "overgrazed") og = value;
        }

        Serial.printf("Sending to Google Sheets: AOG=%.3f, HG=%.3f, OG=%.3f\n", aog, hg, og);
        sendToGoogleSheets(aog, hg, og);

#if EI_CLASSIFIER_HAS_ANOMALY
        Serial.printf("Anomaly prediction: %.3f\n", result.anomaly);
#endif

        free(snapshot_buf);
    }
}

void connectToWiFi() {
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    localIP = WiFi.localIP().toString();
    Serial.println("\nConnected!");
    Serial.println("Camera stream ready: http://" + localIP);
}

void sendToGoogleSheets(float aog, float hg, float og) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String(googleScriptURL) + "?type=grass&aog=" + String(aog, 5) + "&hg=" + String(hg, 5) + "&og=" + String(og, 5);
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode > 0) {
            Serial.println("Data sent to Google Sheets");
        } else {
            Serial.printf("Failed to send data: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        Serial.println("WiFi not connected");
    }
}

void handleRoot() {
    String html = "<html><head><title>ESP32-CAM</title></head><body>";
    html += "<h1>ESP32-CAM Stream</h1>";
    html += "<img src='/stream' width='640' height='480'/>";
    html += "<p>IP: " + localIP + "</p></body></html>";
    server.send(200, "text/html", html);
}

void handleStream() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    WiFiClient client = server.client();
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.println("Content-Length: " + String(fb->len));
    client.println("Connection: close");
    client.println();
    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

bool ei_camera_init(void) {
    if (is_initialised) return true;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
    }
    is_initialised = true;
    return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t* out_buf) {
    if (!is_initialised) {
        Serial.println("Camera is not initialized");
        return false;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }

    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    esp_camera_fb_return(fb);

    if (!converted) {
        Serial.println("Conversion failed");
        return false;
    }

    if (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS || img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS) {
        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height);
    }

    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float* out_ptr) {
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) +
                              (snapshot_buf[pixel_ix + 1] << 8) +
                              snapshot_buf[pixel_ix];
        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif
