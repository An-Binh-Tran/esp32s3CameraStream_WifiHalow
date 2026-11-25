#include "camera_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include <cstdio>   // snprintf
#include <cstring>  // strlen
#include <cstdint>  // uint8_t

#include <cstdlib>      // malloc, free
#include <sys/socket.h> // setsockopt
#include <netinet/tcp.h>// TCP_NODELAY

// Tracks whether the camera started up successfully. The /stream handler
// checks this before trying to grab frames, so a camera failure doesn't
// crash the server - it just makes /stream reply with an error instead.
static bool cameraOK = false;
static const char *TAG  = "camera_server";

// URL path that serves the live video stream.
static const char *STREAM_URL = "/stream";
// Target frames-per-second for the stream. Used to pace how often we
// grab and send a new frame.
static const uint8_t FRAMERATE = 10;  // approx FPS
// Handle to the running HTTP server instance (nullptr until initServer() runs).
static httpd_handle_t server = nullptr;


// Handles GET / - just returns a tiny HTML page with an <img> tag pointing
// at /stream, so a browser can view the video by simply loading the root page.
static esp_err_t handle_root(httpd_req_t *req)
{
    // Equivalent HTML to your Arduino version
    const char *html =
        "<html><head><title>XIAO ESP32S3 Stream</title></head>"
        "<body style=\"text-align:center\"><h1>Stream</h1>"
        "<img src=\"/stream\" style=\"display:block;margin:0 auto\" />"
        "</body></html>";

    // Set content type
    httpd_resp_set_type(req, "text/html");

    // Send full response
    return httpd_resp_send(req, html, strlen(html));
}


// Handles GET /stream - this is the actual video feed.
// It works as "MJPEG over HTTP": instead of one response, we keep the
// connection open and keep sending a sequence of JPEG images one after
// another, each wrapped in a small text header. Browsers understand this
// as a live-updating <img> stream. The loop keeps running until either the
// camera goes away or the client disconnects (detected by a failed send).
static esp_err_t handle_stream(httpd_req_t *req)
{
    if (!cameraOK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        const char *msg = "Camera Not Available";
        return httpd_resp_send(req, msg, strlen(msg));
    }

    // Safe to enable TCP_NODELAY now because we send one raw write per frame
    // (no tiny chunked-encoding wrapper writes).
    int sockfd = httpd_req_to_sockfd(req);
    int nodelay = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    const size_t send_buf_size = 16384;
    char *send_buf = (char *)malloc(send_buf_size);
    if (!send_buf) {
        ESP_LOGE(TAG, "Failed to allocate stream buffer");
        return ESP_ERR_NO_MEM;
    }

    // Send raw HTTP response header once (no Transfer-Encoding: chunked)
    const char *http_hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n";

    int hdr_len = strlen(http_hdr);
    int hdr_sent = 0;
    while (hdr_sent < hdr_len) {
        int ret = httpd_send(req, http_hdr + hdr_sent, hdr_len - hdr_sent);
        if (ret <= 0) {
            free(send_buf);
            return ESP_FAIL;
        }
        hdr_sent += ret;
    }

    uint32_t frame_count = 0;

    while (cameraOK) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        size_t fb_len = fb->len;

        if (++frame_count % 10 == 0) {
            ESP_LOGI(TAG, "Frame #%lu: %u bytes",
                     (unsigned long)frame_count, (unsigned int)fb_len);
        }

        int head_len = snprintf(send_buf, 128,
                                "--frame\r\n"
                                "Content-Type: image/jpeg\r\n"
                                "Content-Length: %u\r\n\r\n",
                                (unsigned int)fb_len);

        if (head_len < 0 || (size_t)head_len + fb_len + 2 > send_buf_size) {
            esp_camera_fb_return(fb);
            continue;
        }

        memcpy(send_buf + head_len, fb->buf, fb_len);
        esp_camera_fb_return(fb);

        send_buf[head_len + fb_len]     = '\r';
        send_buf[head_len + fb_len + 1] = '\n';

       int total_len = head_len + fb_len + 2;
        int sent = 0;
        while (sent < total_len) {
            int chunk = total_len - sent;
            if (chunk > 1380) chunk = 1380;
            int ret = httpd_send(req, send_buf + sent, chunk);
            if (ret <= 0) {
                ESP_LOGW(TAG, "Client disconnected while sending frame");
                free(send_buf);
                return ESP_FAIL;
            }
            sent += ret;
            if (sent < total_len) {
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(85));
    }

    free(send_buf);
    return ESP_FAIL;
}

// ------------- SERVER INIT -------------
// Starts the HTTP server and wires up the two routes above ("/" and
// "/stream"). Call this once, after Wi-Fi is up.
void initServer(void)
{
    if (server != nullptr) {
        // Already started - calling this twice would be a mistake, so just
        // warn and bail out rather than starting a second server.
        ESP_LOGW(TAG, "HTTP server is already running");
        return;
    }

    // Start from ESP-IDF's sensible default HTTP server settings, then
    // override the couple of options we care about below.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Run the HTTP server's task on Core 0 (the stimulation task in
    // main.cpp is pinned to Core 1, so this keeps the two workloads apart).
    config.core_id = 0;
    // Standard HTTP port, so it's reachable as http://<ip or bugcam.local>/
    config.server_port = 80;
    esp_err_t ret = httpd_start(&server, &config);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s",
                 esp_err_to_name(ret));
        server = nullptr;
        return;
    }

    // Register the "/" route -> serves the simple HTML viewer page.
    httpd_uri_t root_uri = {};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = handle_root;
    root_uri.user_ctx = nullptr;

    ret = httpd_register_uri_handler(server, &root_uri);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not register /: %s", esp_err_to_name(ret));
        httpd_stop(server);
        server = nullptr;
        return;
    }

    // Register the "/stream" route -> serves the live MJPEG video feed.
    httpd_uri_t stream_uri = {};
    stream_uri.uri = STREAM_URL;
    stream_uri.method = HTTP_GET;
    stream_uri.handler = handle_stream;
    stream_uri.user_ctx = nullptr;

    ret = httpd_register_uri_handler(server, &stream_uri);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not register /stream: %s",
                 esp_err_to_name(ret));

        httpd_stop(server);
        server = nullptr;
        return;
    }

    if (!cameraOK) {
        ESP_LOGW(TAG, "Camera unavailable; /stream returns HTTP 503");
    }

    ESP_LOGI(TAG, "HTTP server started, port=%d, core=%d",
             config.server_port,
             config.core_id);
}

// ---------- CAMERA ----------
// Configures and powers on the onboard camera module (pins, resolution,
// image format, etc.) so that handle_stream() above has frames to send.
// Returns true on success, false if the camera hardware didn't respond.
bool initCamera() {
  camera_config_t config = {};
  // Which LEDC (LED control / PWM) peripheral channel and timer to use for
  // driving the camera's external clock signal (XCLK).
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  // Data bus pins (8-bit parallel camera interface, D0-D7) - board-specific,
  // defined in camera_pins.h for this XIAO ESP32S3 board.
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  // Clock/sync pins needed to read frames off the sensor.
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  // SCCB (the camera's I2C-like control bus) pins, used to configure the
  // sensor chip itself (brightness, flip, etc.).
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  // Power-down and reset control pins for the sensor module.
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  // Speed of the clock we feed the sensor.
  config.xclk_freq_hz = 20000000;

  // Ask the sensor to output JPEG-encoded frames directly (rather than raw
  // pixels), since that's what we want to stream over HTTP.
  config.pixel_format = PIXFORMAT_JPEG;
  // QVGA = 320x240 - kept small to keep frames quick to capture/send.
  config.frame_size   = FRAMESIZE_QVGA; //change to QQVGA
  // Always hand us the newest available frame rather than queuing old ones,
  // so the stream doesn't fall behind real time.
  config.grab_mode    = CAMERA_GRAB_LATEST;
  // Store frame buffers in external PSRAM (there isn't enough internal RAM).
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  // Lower number = higher JPEG quality (and bigger file size). 25 favours
  // smaller/faster frames over image quality.
  config.jpeg_quality = 30; //Change this to 35
  // Only keep one frame buffer in memory at a time.
  config.fb_count     = 1; //change this later 

  // Actually initialise the camera driver with the settings above.
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // Camera hardware not found/responding - log it and report failure so
    // the rest of the app can carry on without video.
    ESP_LOGI(TAG, "Camera init failed with error 0x%x\n", err);
    cameraOK = false;
    return false;
  }

  // Some image-quality tweaks that only apply to the OV3660 sensor variant
  // (a different camera module also used on some of these boards).
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);       // flip image vertically (sensor is mounted upside down)
    // s->set_framesize(s, FRAMESIZE_QQVGA); // output 160x120 into the 15 KB buffer
    s->set_brightness(s, 1);  // slightly brighter
    s->set_saturation(s, -2); // slightly less saturated colours
  }

  cameraOK = true;
  return true;
}
