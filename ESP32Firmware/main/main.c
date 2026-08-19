#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "user_config.h"

#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 64
#define MATRIX_SCAN_ROWS 32
// COLOR_DEPTH_BITS_IDLE is the max, so size anything depth-dependent off it.
#define COLOR_DEPTH_BITS_IDLE USER_COLOR_DEPTH_BITS_IDLE
#define COLOR_DEPTH_BITS_ACTIVE USER_COLOR_DEPTH_BITS_ACTIVE
#define REQUEST_BUFFER_SIZE 160000
#define WIFI_CONNECTED_BIT BIT0

#define WIFI_STA_SSID (USER_WIFI_SSID[0] != '\0' ? USER_WIFI_SSID : CONFIG_LED_MATRIX_STA_SSID)
#define WIFI_STA_PASSWORD (USER_WIFI_PASSWORD[0] != '\0' ? USER_WIFI_PASSWORD : CONFIG_LED_MATRIX_STA_PASSWORD)
#define BACKEND_URL (USER_BACKEND_URL[0] != '\0' ? USER_BACKEND_URL : CONFIG_LED_MATRIX_BACKEND_URL)
#define BACKEND_FALLBACK_URL USER_BACKEND_FALLBACK_URL

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

static const char *TAG = "led_matrix";
static rgb_t frame[MATRIX_HEIGHT][MATRIX_WIDTH];
static SemaphoreHandle_t frame_lock;

// Writers (HTTP handler, Firebase poll, matrix_clear/fill) no longer touch `frame`
// directly. They write into `frame_pending` instead, and matrix_refresh_task only
// swaps it into the live `frame` once per full scan cycle (between bitplane 1 ending
// and bitplane 0 starting), never mid-row. This avoids a new frame landing partway
// through a scan and showing half old / half new content (visible as a flicker/tear,
// roughly once per USER_BACKEND_POLL_MS).
static rgb_t frame_pending[MATRIX_HEIGHT][MATRIX_WIDTH];
static SemaphoreHandle_t pending_lock;
static volatile bool frame_pending_ready = false;
static portMUX_TYPE hub75_spinlock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t wifi_event_group;
static uint8_t brightness_percent = USER_DEFAULT_BRIGHTNESS;
// Read once per refresh cycle in matrix_refresh_task (same "never change
// mid-cycle" rule as frame_pending_ready) so a depth change never lands
// mid-row/mid-bitplane. Single-byte writes are atomic on ESP32, no lock needed.
static volatile uint8_t matrix_color_depth_bits = COLOR_DEPTH_BITS_IDLE;

static inline void matrix_set_color_depth(uint8_t depth)
{
    matrix_color_depth_bits = depth;
}
static long last_backend_sequence = -1;
static bool backend_frame_applied = false;

typedef struct {
    char *data;
    int capacity;
    int length;
    bool overflow;
} http_body_t;

static const int rgb_pins[] = {
    USER_PIN_R1,
    USER_PIN_G1,
    USER_PIN_B1,
    USER_PIN_R2,
    USER_PIN_G2,
    USER_PIN_B2,
};

static const int addr_pins[] = {
    USER_PIN_A,
    USER_PIN_B,
    USER_PIN_C,
    USER_PIN_D,
    USER_PIN_E,
};

#define GPIO_MASK(pin) (1UL << (pin))
#define RGB_PIN_MASK (GPIO_MASK(USER_PIN_R1) | GPIO_MASK(USER_PIN_G1) | GPIO_MASK(USER_PIN_B1) | GPIO_MASK(USER_PIN_R2) | GPIO_MASK(USER_PIN_G2) | GPIO_MASK(USER_PIN_B2))
#define ADDR_PIN_MASK (GPIO_MASK(USER_PIN_A) | GPIO_MASK(USER_PIN_B) | GPIO_MASK(USER_PIN_C) | GPIO_MASK(USER_PIN_D) | GPIO_MASK(USER_PIN_E))
#define CLK_PIN_MASK GPIO_MASK(USER_PIN_CLK)
#define LAT_PIN_MASK GPIO_MASK(USER_PIN_LAT)
#define OE_PIN_MASK GPIO_MASK(USER_PIN_OE)

// IRAM_ATTR: these run inside matrix_refresh_task's per-row bit-bang loop, which is
// timing-critical down to single-digit microseconds. Without this, they live in flash,
// and ESP-IDF briefly disables the flash cache on BOTH cores during certain flash/NVS
// operations (e.g. the Wi-Fi driver persisting connection state around association/
// IP-get, or periodic NVS commits). Any core executing flash-resident code during that
// window stalls until the cache is re-enabled -- if that happens while OE is held low,
// a row stays lit far longer than its intended ~10-20us dwell, which is exactly the
// flash/flicker seen even with no polling task on core 1 at all.
static inline void IRAM_ATTR gpio_set_mask(uint32_t mask)
{
    REG_WRITE(GPIO_OUT_W1TS_REG, mask);
}

static inline void IRAM_ATTR gpio_clear_mask(uint32_t mask)
{
    REG_WRITE(GPIO_OUT_W1TC_REG, mask);
}

static inline void IRAM_ATTR hub75_pulse_delay(void)
{
    // Was hardcoded to 10 regardless of user_config.h; USER_HUB75_PULSE_DELAY_CYCLES
    // is now actually honored. USER_HUB75_TARGET_CLOCK_HZ is still not wired up here
    // (this is a fixed NOP-count delay, not a clock-rate calculation) -- it's kept in
    // user_config.h as a note of the panel's intended clock speed for future use.
    for (int i = 0; i < USER_HUB75_PULSE_DELAY_CYCLES; i++)
    {
        __asm__ __volatile__("nop");
    }
}

static inline void IRAM_ATTR pulse_mask(uint32_t mask)
{
    gpio_set_mask(mask);
    hub75_pulse_delay();
    gpio_clear_mask(mask);
    hub75_pulse_delay();
}

static inline void IRAM_ATTR gpio_hi(int pin)
{
    gpio_set_mask(GPIO_MASK(pin));
}

static inline void IRAM_ATTR gpio_lo(int pin)
{
    gpio_clear_mask(GPIO_MASK(pin));
}

static inline void IRAM_ATTR pulse_pin(int pin)
{
    pulse_mask(GPIO_MASK(pin));
}

static void matrix_clear(void)
{
    xSemaphoreTake(pending_lock, portMAX_DELAY);
    memset(frame_pending, 0, sizeof(frame_pending));
    frame_pending_ready = true;
    xSemaphoreGive(pending_lock);
}

static void matrix_fill(rgb_t color)
{
    xSemaphoreTake(pending_lock, portMAX_DELAY);
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            frame_pending[y][x] = color;
        }
    }
    frame_pending_ready = true;
    xSemaphoreGive(pending_lock);
}

static void IRAM_ATTR set_addr(uint8_t row)
{
    uint32_t set_mask = 0;
    if (row & BIT0) {
        set_mask |= GPIO_MASK(USER_PIN_A);
    }
    if (row & BIT1) {
        set_mask |= GPIO_MASK(USER_PIN_B);
    }
    if (row & BIT2) {
        set_mask |= GPIO_MASK(USER_PIN_C);
    }
    if (row & BIT3) {
        set_mask |= GPIO_MASK(USER_PIN_D);
    }
    if (row & BIT4) {
        set_mask |= GPIO_MASK(USER_PIN_E);
    }
    gpio_clear_mask(ADDR_PIN_MASK);
    gpio_set_mask(set_mask);
}

static inline bool IRAM_ATTR bit_on(uint8_t value, uint8_t bitplane, uint8_t depth)
{
    return (value >> (8 - depth + bitplane)) & 1;
}

static void IRAM_ATTR matrix_refresh_task(void *arg)
{
    (void)arg;
    rgb_t top;
    rgb_t bottom;

    while (true) {
        taskYIELD();

        // Only pick up a new frame at a full-cycle boundary, never mid-row/mid-bitplane.
        if (frame_pending_ready) {
            xSemaphoreTake(pending_lock, portMAX_DELAY);
            if (frame_pending_ready) {
                xSemaphoreTake(frame_lock, portMAX_DELAY);
                memcpy(frame, frame_pending, sizeof(frame));
                xSemaphoreGive(frame_lock);
                frame_pending_ready = false;
            }
            xSemaphoreGive(pending_lock);
        }

        // Latched once per cycle, not re-read per bitplane/row, so a depth
        // change from backend_poll_task can't shrink COLOR_DEPTH_BITS_IDLE
        // mid-cycle out from under bit_on()'s shift math.
        uint8_t depth = matrix_color_depth_bits;

        for (int bit = 0; bit < depth; bit++) {
            uint32_t dwell_us = ((1U << bit) * (uint32_t)brightness_percent * 10U) / 100U;
            if (dwell_us < 1) {
                dwell_us = 1;
            }

            for (int row = 0; row < MATRIX_SCAN_ROWS; row++) {
                gpio_set_mask(OE_PIN_MASK);
                xSemaphoreTake(frame_lock, portMAX_DELAY);
                portENTER_CRITICAL(&hub75_spinlock);
                for (int col = 0; col < MATRIX_WIDTH; col++) {
                    top = frame[row][col];
                    bottom = frame[row + MATRIX_SCAN_ROWS][col];

                    uint32_t color_mask = 0;
                    if (bit_on(top.r, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_R1);
                    }
                    if (bit_on(top.g, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_G1);
                    }
                    if (bit_on(top.b, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_B1);
                    }
                    if (bit_on(bottom.r, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_R2);
                    }
                    if (bit_on(bottom.g, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_G2);
                    }
                    if (bit_on(bottom.b, bit, depth)) {
                        color_mask |= GPIO_MASK(USER_PIN_B2);
                    }
                    gpio_clear_mask(RGB_PIN_MASK);
                    gpio_set_mask(color_mask);
                    pulse_mask(CLK_PIN_MASK);
                }
                portEXIT_CRITICAL(&hub75_spinlock);
                xSemaphoreGive(frame_lock);

                pulse_mask(LAT_PIN_MASK);
                set_addr(row);
                gpio_clear_mask(OE_PIN_MASK);
                esp_rom_delay_us(dwell_us);
                gpio_set_mask(OE_PIN_MASK);
            }
        }
    }
}

static esp_err_t matrix_gpio_init(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof(rgb_pins) / sizeof(rgb_pins[0]); i++) {
        mask |= 1ULL << rgb_pins[i];
    }
    for (size_t i = 0; i < sizeof(addr_pins) / sizeof(addr_pins[0]); i++) {
        mask |= 1ULL << addr_pins[i];
    }
    mask |= 1ULL << USER_PIN_LAT;
    mask |= 1ULL << USER_PIN_OE;
    mask |= 1ULL << USER_PIN_CLK;

    gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "gpio_config failed");
    gpio_hi(USER_PIN_OE);
    gpio_lo(USER_PIN_LAT);
    gpio_lo(USER_PIN_CLK);
    return ESP_OK;
}

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *find_key(const char *p, const char *key)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(p, pattern);
}

static bool parse_key_u8(const char **cursor, const char *key, uint8_t *out)
{
    const char *p = find_key(*cursor, key);
    if (!p) {
        return false;
    }
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p = skip_ws(p + 1);
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p || value < 0 || value > 255) {
        return false;
    }
    *out = (uint8_t)value;
    *cursor = end;
    return true;
}

static bool parse_key_long_value(const char *body, const char *key, long *out)
{
    const char *p = find_key(body, key);
    if (!p) {
        return false;
    }
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p = skip_ws(p + 1);
    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = value;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    return -1;
}

static esp_err_t parse_compact_frame_hex(const char *body, rgb_t out[MATRIX_HEIGHT][MATRIX_WIDTH])
{
    const char *key = find_key(body, "frameHex");
    if (!key) {
        return ESP_ERR_NOT_FOUND;
    }

    const char *p = strchr(key, ':');
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    p = strchr(p, '"');
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    p++; // now pointing at first hex digit

    int pixels_set = 0;
    for (int i = 0; i < MATRIX_WIDTH * MATRIX_HEIGHT; i++) {
        int rr_hi = hex_digit(p[0]);
        int rr_lo = hex_digit(p[1]);
        int gg_hi = hex_digit(p[2]);
        int gg_lo = hex_digit(p[3]);
        int bb_hi = hex_digit(p[4]);
        int bb_lo = hex_digit(p[5]);

        // Stop as soon as we hit something that isn't 6 more hex digits
        // (closing quote, end of string, truncated chunk, etc).
        // This is NOT an error - it just means the sender only gave us
        // `i` pixels worth of data, which is fine.
        if (rr_hi < 0 || rr_lo < 0 || gg_hi < 0 || gg_lo < 0 || bb_hi < 0 || bb_lo < 0) {
            break;
        }

        out[i / MATRIX_WIDTH][i % MATRIX_WIDTH] = (rgb_t){
            .r = (uint8_t)((rr_hi << 4) | rr_lo),
            .g = (uint8_t)((gg_hi << 4) | gg_lo),
            .b = (uint8_t)((bb_hi << 4) | bb_lo),
        };

        p += 6;
        pixels_set++;
    }

    if (pixels_set == 0) {
        ESP_LOGW(TAG, "frameHex contained no valid pixels");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Decoded %d pixels from frameHex (of %d max)", pixels_set, MATRIX_WIDTH * MATRIX_HEIGHT);
    return ESP_OK;
}

static esp_err_t parse_matrix_payload(const char *body, rgb_t out[MATRIX_HEIGHT][MATRIX_WIDTH])
{
    esp_err_t compact_err = parse_compact_frame_hex(body, out);
    if (compact_err == ESP_OK) {
        return ESP_OK;
    }

    const char *pixels_key = find_key(body, "pixels");
    if (!pixels_key) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *p = strchr(pixels_key, '[');
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    p++;

    for (int i = 0; i < MATRIX_WIDTH * MATRIX_HEIGHT; i++) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        if (!parse_key_u8(&p, "r", &r) || !parse_key_u8(&p, "g", &g) || !parse_key_u8(&p, "b", &b)) {
            ESP_LOGW(TAG, "Payload stopped at pixel %d", i);
            return ESP_ERR_INVALID_ARG;
        }
        out[i / MATRIX_WIDTH][i % MATRIX_WIDTH] = (rgb_t){ .r = r, .g = g, .b = b };
    }

    return ESP_OK;
}

static void apply_matrix_payload(const char *body)
{
    const char *trimmed = skip_ws(body);
    if (strncmp(trimmed, "null", 4) == 0 || strstr(body, "\"hasFrame\":false") || strstr(body, "\"hasFrame\": false")) {
        ESP_LOGD(TAG, "No backend frame yet");
        return;
    }

    // Brightness is applied independently of the sequence/dedup check below so a
    // brightness-only change riding along with an otherwise-unchanged frame still
    // takes effect. It drives the same brightness_percent used by matrix_refresh_task's
    // PWM dwell-time calculation (real hardware dimming), not a scale-down of the RGB
    // values themselves -- that preserves full color depth at low brightness instead
    // of crushing it toward black through repeated integer rounding.
    const char *bp = body;
    uint8_t incoming_brightness = brightness_percent;
    if (parse_key_u8(&bp, "brightness", &incoming_brightness) && incoming_brightness >= 1 && incoming_brightness <= 100) {
        if (incoming_brightness != brightness_percent) {
            ESP_LOGI(TAG, "Backend brightness changed: %u -> %u", brightness_percent, incoming_brightness);
        }
        brightness_percent = incoming_brightness;
    }

    long sequence = -1;
    if (parse_key_long_value(body, "sequence", &sequence) && sequence == last_backend_sequence) {
        ESP_LOGD(TAG, "Backend frame unchanged");
        return;
    }

    static rgb_t next_frame[MATRIX_HEIGHT][MATRIX_WIDTH];
    if (parse_matrix_payload(body, next_frame) != ESP_OK) {
        ESP_LOGW(TAG, "Backend returned invalid matrix payload");
        return;
    }

    int lit_pixels = 0;
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            if (next_frame[y][x].r != 0 || next_frame[y][x].g != 0 || next_frame[y][x].b != 0) {
                lit_pixels++;
            }
        }
    }

    xSemaphoreTake(pending_lock, portMAX_DELAY);
    memcpy(frame_pending, next_frame, sizeof(frame_pending));
    frame_pending_ready = true;
    xSemaphoreGive(pending_lock);
    if (sequence >= 0) {
        last_backend_sequence = sequence;
    }
    backend_frame_applied = true;
    ESP_LOGI(TAG, "Applied backend matrix frame: lit_pixels=%d", lit_pixels);
}

static esp_err_t read_request_body(httpd_req_t *req, char **out_body)
{
    if (req->content_len == 0 || req->content_len > REQUEST_BUFFER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *body = calloc(1, req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += ret;
    }

    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static void send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_sendstr(req, json);
}

static esp_err_t options_handler(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t health_handler(httpd_req_t *req)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"width\":64,\"height\":64,\"brightness\":%u}",
             brightness_percent);
    send_json(req, response);
    return ESP_OK;
}

static esp_err_t clear_handler(httpd_req_t *req)
{
    matrix_clear();
    send_json(req, "{\"ok\":true,\"cleared\":true}");
    return ESP_OK;
}

static esp_err_t brightness_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t err = read_request_body(req, &body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ESP_OK;
    }

    const char *p = body;
    uint8_t next = brightness_percent;
    if (!parse_key_u8(&p, "brightness", &next) || next > 100 || next < 1) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected brightness 1-100");
        return ESP_OK;
    }

    brightness_percent = next;
    free(body);
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t matrix_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t err = read_request_body(req, &body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large or missing");
        return ESP_OK;
    }

    const char *bp = body;
    uint8_t incoming_brightness = brightness_percent;
    if (parse_key_u8(&bp, "brightness", &incoming_brightness) && incoming_brightness >= 1 && incoming_brightness <= 100) {
        brightness_percent = incoming_brightness;
    }

    static rgb_t next_frame[MATRIX_HEIGHT][MATRIX_WIDTH];
    err = parse_matrix_payload(body, next_frame);
    free(body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected 4096 RGB pixels");
        return ESP_OK;
    }

    xSemaphoreTake(pending_lock, portMAX_DELAY);
    memcpy(frame_pending, next_frame, sizeof(frame_pending));
    frame_pending_ready = true;
    xSemaphoreGive(pending_lock);

    send_json(req, "{\"ok\":true,\"pixels\":4096}");
    return ESP_OK;
}

static esp_err_t backend_http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->user_data || !event->data || event->data_len <= 0) {
        return ESP_OK;
    }

    http_body_t *body = (http_body_t *)event->user_data;
    if (body->length + event->data_len >= body->capacity) {
        body->overflow = true;
        return ESP_FAIL;
    }

    memcpy(body->data + body->length, event->data, event->data_len);
    body->length += event->data_len;
    body->data[body->length] = '\0';
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (!backend_frame_applied) {
            matrix_fill((rgb_t){ .r = 0, .g = 0, .b = 255 });
            ESP_LOGI(TAG, "Filled matrix blue for Wi-Fi connected test");
        }
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool backend_poll_once(const char *url, char *buffer)
{
    http_body_t body = {
        .data = buffer,
        .capacity = REQUEST_BUFFER_SIZE,
        .length = 0,
        .overflow = false,
    };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = backend_http_event_handler,
        .user_data = &body,
        .timeout_ms = 8000,
        .crt_bundle_attach = USER_BACKEND_ALLOW_INSECURE_TLS ? NULL : esp_crt_bundle_attach,
        .skip_cert_common_name_check = USER_BACKEND_ALLOW_INSECURE_TLS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "Backend poll init failed: %s", url);
        return false;
    }

    esp_http_client_set_header(client, "Accept", "application/json,text/plain,*/*");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (ESP32-S3; Draw Anywhere LED Matrix)");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    bool request_ok = false;
    if (err == ESP_OK && status == 200 && !body.overflow && body.length > 0) {
        apply_matrix_payload(body.data);
        request_ok = true;
    } else if (err == ESP_OK && status == 204) {
        ESP_LOGD(TAG, "No backend frame yet");
        request_ok = true;
    } else {
        ESP_LOGW(TAG, "Backend poll failed: url=%s err=%s status=%d overflow=%d", url, esp_err_to_name(err), status, body.overflow);
        if (body.length > 0) {
            int snippet_len = body.length < 120 ? body.length : 120;
            ESP_LOGW(TAG, "Backend response starts: %.*s", snippet_len, body.data);
        }
    }
    esp_http_client_cleanup(client);
    return request_ok;
}

static void backend_poll_task(void *arg)
{
    (void)arg;
    const char *url = BACKEND_URL;
    bool first_poll_after_wifi = true;
    char *buffer = calloc(1, REQUEST_BUFFER_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Could not allocate backend poll buffer");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        ESP_LOGI(TAG, "Waiting for Wi-Fi before backend poll");
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        if (first_poll_after_wifi) {
            ESP_LOGI(TAG, "Wi-Fi indicator shown; waiting before first backend poll");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        // Lower bitplane count only around the HTTP/TLS call itself -- that's
        // where flash-cache stalls (mbedTLS/HTTP client code isn't IRAM-resident)
        // can land while a row is lit and show up as a flash. No benefit to
        // staying at reduced depth once the request is done.
        matrix_set_color_depth(COLOR_DEPTH_BITS_ACTIVE);
        bool request_ok = backend_poll_once(url, buffer);
        if (!request_ok && strlen(BACKEND_FALLBACK_URL) > 0) {
            ESP_LOGW(TAG, "Trying backend fallback URL");
            request_ok = backend_poll_once(BACKEND_FALLBACK_URL, buffer);
        }
        matrix_set_color_depth(COLOR_DEPTH_BITS_IDLE);
        if (first_poll_after_wifi) {
            first_poll_after_wifi = false;
            if (!request_ok && !backend_frame_applied) {
                matrix_fill((rgb_t){ .r = 255, .g = 0, .b = 0 });
                ESP_LOGW(TAG, "Filled matrix red because first backend poll failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(request_ok ? USER_BACKEND_POLL_MS : 10000));
    }
}

static httpd_handle_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;
    // Keep the HTTP server off core 1: matrix_refresh_task lives there and its
    // OE-enabled dwell windows are only ~10-20us. HTTPD_DEFAULT_CONFIG's default
    // task priority (5) is higher than matrix_refresh_task's (4), so if this ever
    // landed on core 1 it could preempt mid-row and hold a row lit for a full
    // scheduler slice -- a visible flash.
    config.core_id = 0;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = health_handler },
        { .uri = "/health", .method = HTTP_GET, .handler = health_handler },
        { .uri = "/matrix", .method = HTTP_POST, .handler = matrix_handler },
        { .uri = "/matrix", .method = HTTP_OPTIONS, .handler = options_handler },
        { .uri = "/clear", .method = HTTP_POST, .handler = clear_handler },
        { .uri = "/brightness", .method = HTTP_POST, .handler = brightness_handler },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }

    return server;
}

static void wifi_init_network(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_event_group ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    const char *sta_ssid = WIFI_STA_SSID;
    const char *sta_password = WIFI_STA_PASSWORD;

    if (strlen(sta_ssid) == 0) {
        ESP_LOGW(TAG, "USER_WIFI_SSID is empty; Wi-Fi/backend polling will not connect");
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    // By default the Wi-Fi driver persists STA config/state to NVS (flash) on every
    // connect. Each flash write forces a cross-core lock that pauses core 1 -- including
    // matrix_refresh_task -- until the write finishes, which shows up as a flash on the
    // panel timed to Wi-Fi connects/reconnects. RAM-only storage avoids that entirely;
    // the tradeoff is Wi-Fi config isn't persisted across reboots, which is fine since
    // USER_WIFI_SSID/PASSWORD are baked into the firmware anyway.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t sta_config = { 0 };
    strncpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, sta_password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Power-save duty-cycles the radio (sleep/wake on a timer), which is another common
    // source of small timing hiccups for bit-banged displays. We're plugged into mains
    // power here, not battery, so there's no reason to trade timing stability for it.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    // TEMPORARY DIAGNOSTIC: lowest legal TX power, to test whether the flicker is caused
    // by RF coupling from the Wi-Fi radio directly onto the HUB75 signal wires (CLK/LAT/OE),
    // rather than anything in software. Range is 8..84 in quarter-dBm units (2dBm..21dBm);
    // 8 = 2dBm is the minimum. This will likely hurt Wi-Fi range -- it's here to isolate the
    // cause, not to ship. Remove this call once you've confirmed/ruled out RF coupling.
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(8));

    if (strlen(sta_ssid) > 0) {
        ESP_ERROR_CHECK(esp_wifi_connect());
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID %s for backend polling", sta_ssid);
    }
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    frame_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(frame_lock ? ESP_OK : ESP_ERR_NO_MEM);
    pending_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(pending_lock ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(matrix_gpio_init());
    matrix_fill((rgb_t){ .r = 0, .g = 255, .b = 0 });
    ESP_LOGI(TAG, "Filled matrix green for startup test");

    xTaskCreatePinnedToCore(matrix_refresh_task, "matrix_refresh", 4096, NULL, 4, NULL, 1);
    wifi_init_network();
    start_http_server();

    if (strlen(BACKEND_URL) > 0 && strlen(WIFI_STA_SSID) > 0) {
        // Pinned to core 0 (matrix_refresh_task owns core 1) and given a lower
        // priority (3 < 4) than matrix_refresh_task. Previously this used plain
        // xTaskCreate() with no core affinity at the SAME priority as the refresh
        // task, so FreeRTOS could schedule it onto core 1 and round-robin it against
        // the refresh task. If that happened while OE was held low (row lit), the
        // row would stay lit for a full ~10ms scheduler slice instead of ~10-20us --
        // visible as a bright flash timed to every backend poll.
        xTaskCreatePinnedToCore(backend_poll_task, "backend_poll", 8192, NULL, 3, NULL, 0);
        ESP_LOGI(TAG, "Backend polling enabled: %s", BACKEND_URL);
    }
}