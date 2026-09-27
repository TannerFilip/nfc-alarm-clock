#include "web_wifi.hpp"

#include "esp_event.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace clock_app {
namespace {

constexpr char kHost[] = "192.168.4.1";
constexpr char kOrigin[] = "http://192.168.4.1";

// HTTP is intentionally limited to the isolated, time-bounded WPA2 SoftAP.
// The WPA2 password, per-start CSRF token, Host/Origin checks and lack of a
// STA listener provide defense in depth; this is not an HTTPS transport.
constexpr char kPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>NFC alarm clock setup</title><style>
body{font:16px system-ui;max-width:36rem;margin:2rem auto;padding:0 1rem;background:#111;color:#eee}
input,button,select{font:inherit;padding:.55rem;margin:.25rem 0}fieldset{margin:1rem 0;border:1px solid #555}
.field{display:block;margin:.65rem 0}.hint{display:block;color:#bbb;font-size:.88rem;margin:.15rem 0}.days label{display:inline-block;margin:.25rem .5rem .25rem 0}
.badge{color:#111;background:#ffd43b;padding:.3rem;font-weight:700}#msg{min-height:1.5em}
</style></head><body><p class="badge">SETUP MODE — local temporary network</p>
<h1>Alarm clock setup</h1><p>Connected devices can configure this clock while setup mode is active.</p><main id="config">
<fieldset><legend>Timezone</legend><label class="field">Clock timezone<select id="zone"><option>UTC</option><option>America/Los_Angeles</option></select><span class="hint">Alarm times use this timezone. The clock stores time internally in UTC.</span></label>
<button onclick="send('timezone','zone='+encodeURIComponent(zone.value))">Save</button></fieldset>
<fieldset><legend>Alarm</legend><p class="hint">An enabled alarm requires an enrolled tag. Saving the same ID updates that alarm.</p>
<label class="field">Alarm ID<input id="aid" type="number" min="1" max="4294967295" value="1"><span class="hint">Use a simple unique number, such as 1. Use this ID to remove the alarm later.</span></label>
<label class="field">Hour (0–23)<input id="hour" type="number" min="0" max="23" placeholder="7"></label><label class="field">Minute (0–59)<input id="minute" type="number" min="0" max="59" placeholder="00"></label>
<div class="field">Weekdays<span class="hint">Select every day on which this alarm should ring.</span><div class="days"><label><input id="sun" type="checkbox">Sun</label><label><input id="mon" type="checkbox">Mon</label><label><input id="tue" type="checkbox">Tue</label><label><input id="wed" type="checkbox">Wed</label><label><input id="thu" type="checkbox">Thu</label><label><input id="fri" type="checkbox">Fri</label><label><input id="sat" type="checkbox">Sat</label></div></div>
<label><input id="enabled" type="checkbox" checked> Alarm enabled</label>
<button onclick="alarm()">Save alarm</button><button onclick="send('alarm-remove','id='+aid.value)">Remove alarm</button></fieldset>
<fieldset><legend>Dismissal tag</legend><label class="field">Tag UID in hexadecimal<input id="tag" maxlength="20" placeholder="Example: 01020304"><span class="hint">This identifier is authorized to dismiss an active alarm. Enter hexadecimal digits only.</span></label><button onclick="send('enroll','tag='+tag.value)">Enroll tag</button><button onclick="send('tag-remove','tag='+tag.value)">Remove tag</button></fieldset>
<fieldset><legend>Display and sound</legend><label class="field">OLED brightness (1–255)<input id="brightness" type="number" min="1" max="255" value="79"></label>
<label><input id="ambient" type="checkbox"> Adjust brightness using the light sensor</label><button onclick="displayCfg()">Save display settings</button>
<label class="field">Maximum alarm volume (1–5%)<input id="volume" type="number" min="1" max="5" value="5"><span class="hint">The alarm ramps up to this limit. Even 5% may be loud.</span></label><button onclick="send('volume','volume='+volume.value)">Save volume</button></fieldset>
<fieldset><legend>Clock</legend><label class="field">Device name<input id="name" maxlength="24" placeholder="Bedroom clock"><span class="hint">A local label stored with the clock settings.</span></label><button onclick="send('name','name='+encodeURIComponent(name.value))">Save name</button>
<label class="field">Current UTC date and time<input id="utc" maxlength="20" placeholder="2026-09-27T12:00:00Z"><span class="hint">Use exactly YYYY-MM-DDTHH:MM:SSZ. This sets the RTC; alarm times use the timezone above.</span></label><button onclick="send('time','utc='+encodeURIComponent(utc.value))">Set UTC</button></fieldset>
</main><p id="msg"></p><script>
let csrf='';const msg=document.querySelector('#msg');
async function initialize(){let r=await fetch("/api/status");if(r.ok){let j=await r.json();csrf=j.csrf;msg.textContent="Ready";}else msg.textContent="Setup unavailable";}
async function send(kind,body){if(!csrf){msg.textContent='Setup is still initializing.';return;}let r=await fetch('/api/config/'+kind,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Setup-CSRF':csrf},body});msg.textContent=r.ok?'Request queued. Check the clock status or serial log to confirm it was saved.':'Rejected: check the fields and clock state.';}
function weekdayMask(){return [sun,mon,tue,wed,thu,fri,sat].reduce((mask,box,index)=>mask|(box.checked?1<<index:0),0);}
function alarm(){let days=weekdayMask();if(!days){msg.textContent='Select at least one weekday.';return;}send('alarm','id='+aid.value+'&hour='+hour.value+'&minute='+minute.value+'&days='+days+'&enabled='+(enabled.checked?'1':'0'));}
function displayCfg(){send('display', 'brightness='+brightness.value+'&ambient='+(ambient.checked?'1':'0'));}
initialize();
</script></body></html>)HTML";

void random_hex(char* output, size_t hex_digits) {
    static constexpr char alphabet[] = "0123456789ABCDEF";
    uint8_t bytes[16]{};
    esp_fill_random(bytes, (hex_digits + 1) / 2);
    for (size_t i = 0; i < hex_digits; ++i) {
        output[i] = alphabet[(bytes[i / 2] >> ((i & 1U) ? 0 : 4)) & 0x0f];
    }
    output[hex_digits] = '\0';
}

void random_readable(char* output, size_t length) {
    static constexpr char alphabet[] = "ACEFHJKMNPRTWXYZ";
    uint8_t bytes[16]{};
    if (length > sizeof(bytes)) length = sizeof(bytes);
    esp_fill_random(bytes,length);
    for (size_t i = 0; i < length; ++i) output[i] = alphabet[bytes[i] & 0x0f];
    output[length] = '\0';
}

bool exact_header(httpd_req_t* request, const char* name, const char* expected) {
    char value[96]{};
    const size_t length = httpd_req_get_hdr_value_len(request, name);
    return length > 0 && length < sizeof(value) &&
           httpd_req_get_hdr_value_str(request, name, value, sizeof(value)) == ESP_OK &&
           std::strcmp(value, expected) == 0;
}

bool valid_content_type(httpd_req_t* request) {
    char value[64]{};
    const size_t length = httpd_req_get_hdr_value_len(request, "Content-Type");
    if (length == 0 || length >= sizeof(value) ||
        httpd_req_get_hdr_value_str(request, "Content-Type", value, sizeof(value)) != ESP_OK) return false;
    return std::strncmp(value, "application/x-www-form-urlencoded", 33) == 0;
}

const char* field(const char* body, const char* key, char* value, size_t capacity) {
    const size_t key_length = std::strlen(key);
    for (const char* cursor = body; *cursor;) {
        const char* end = std::strchr(cursor, '&');
        if (!end) end = cursor + std::strlen(cursor);
        if (static_cast<size_t>(end - cursor) > key_length &&
            std::memcmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            const char* start = cursor + key_length + 1;
            size_t written = 0;
            while (start < end) {
                unsigned decoded = 0;
                if (*start == '+') {
                    decoded = ' ';
                    ++start;
                } else if (*start == '%' && end - start >= 3 &&
                           std::isxdigit(static_cast<unsigned char>(start[1])) &&
                           std::isxdigit(static_cast<unsigned char>(start[2]))) {
                    auto nibble = [](unsigned char c) { return c <= '9' ? c - '0' : (c & 0xdf) - 'A' + 10; };
                    decoded = (nibble(start[1]) << 4) | nibble(start[2]);
                    start += 3;
                    if (decoded == 0) return nullptr;
                } else {
                    decoded = static_cast<unsigned char>(*start++);
                }
                if (written + 1 >= capacity) return nullptr;
                value[written++] = static_cast<char>(decoded);
            }
            value[written] = '\0';
            return value;
        }
        cursor = *end ? end + 1 : end;
    }
    return nullptr;
}

bool parse_u32(const char* text, uint32_t minimum, uint32_t maximum, uint32_t& value) {
    if (!text || !*text) return false;
    uint64_t parsed = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (!std::isdigit(*p)) return false;
        parsed = parsed * 10 + (*p - '0');
        if (parsed > maximum) return false;
    }
    if (parsed < minimum) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

esp_err_t reject(httpd_req_t* request, httpd_err_code_t code, const char* message) {
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_send_err(request, code, message);
}

esp_err_t conflict(httpd_req_t* request, const char* message) {
    httpd_resp_set_status(request, "409 Conflict");
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, message);
}

esp_err_t server_busy(httpd_req_t* request) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, "Busy");
}

} // namespace

esp_err_t WebWifiService::init(RingingPredicate ringing, void* ringing_context,
                               MutationCallback mutation, void* mutation_context) {
    if (!ringing || !mutation) return ESP_ERR_INVALID_ARG;
    if (initialized_.exchange(true)) return ESP_ERR_INVALID_STATE;
    lock_ = xSemaphoreCreateMutexStatic(&lock_storage_);
    if (!lock_) { initialized_ = false; return ESP_ERR_NO_MEM; }
    ringing_ = ringing;
    ringing_context_ = ringing_context;
    mutation_ = mutation;
    mutation_context_ = mutation_context;
    const esp_timer_create_args_t timer_args{expiry_entry, this, ESP_TIMER_TASK, "setup_expiry", true};
    const esp_err_t error = esp_timer_create(&timer_args, &expiry_timer_);
    if (error != ESP_OK) initialized_ = false;
    return error;
}

esp_err_t WebWifiService::start_setup(uint32_t duration_seconds) {
    if (!initialized_ || active_) return ESP_ERR_INVALID_STATE;
    stop_requested_ = false;
    if (duration_seconds < min_duration_seconds || duration_seconds > max_duration_seconds) return ESP_ERR_INVALID_ARG;
    if (ringing_(ringing_context_)) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    char suffix[7]{};
    random_readable(suffix,6);
    std::snprintf(ssid_, sizeof(ssid_), "NFC-Clock-%s", suffix);
    random_readable(password_,12);
    random_hex(csrf_, 32);
    expires_at_us_ = esp_timer_get_time() + static_cast<int64_t>(duration_seconds) * 1000000LL;
    last_request_ms_ = 0;
    xSemaphoreGive(lock_);

    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) { stop_setup(); return error; }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) { stop_setup(); return error; }
    if (!ap_netif_) ap_netif_ = esp_netif_create_default_wifi_ap();
    if (!ap_netif_) { stop_setup(); return ESP_ERR_NO_MEM; }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init.nvs_enable = false; // Ephemeral AP credentials must not be persisted by the Wi-Fi driver.
    error = esp_wifi_init(&wifi_init);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE && error != ESP_ERR_WIFI_INIT_STATE) { stop_setup(); return error; }
    wifi_config_t wifi{};
    std::memcpy(wifi.ap.ssid, ssid_, std::strlen(ssid_));
    wifi.ap.ssid_len = std::strlen(ssid_);
    std::memcpy(wifi.ap.password, password_, std::strlen(password_));
    wifi.ap.channel = 1;
    wifi.ap.max_connection = 2;
    wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.ap.pmf_cfg.required = true;
    error = esp_wifi_set_mode(WIFI_MODE_AP);
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (error == ESP_OK) error = esp_wifi_start();
    if (error != ESP_OK) { stop_setup(); return error; }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.uri_match_fn = httpd_uri_match_wildcard;
    error = httpd_start(&server_, &config);
    if (error != ESP_OK) { stop_setup(); return error; }
    const httpd_uri_t handlers[] = {
        {"/", HTTP_GET, root_entry, this},
        {"/api/status", HTTP_GET, status_entry, this},
        {"/api/config/*", HTTP_POST, mutation_entry, this},
    };
    for (const auto& handler : handlers) {
        error = httpd_register_uri_handler(server_, &handler);
        if (error != ESP_OK) { stop_setup(); return error; }
    }
    active_ = true;
    error = esp_timer_start_once(expiry_timer_, static_cast<uint64_t>(duration_seconds) * 1000000ULL);
    if (error != ESP_OK) { stop_setup(); return error; }
    return ESP_OK;
}

void WebWifiService::service() {
    if (stop_requested_.exchange(false)) stop_setup();
}

void WebWifiService::stop_setup() {
    active_ = false;
    if (expiry_timer_) (void)esp_timer_stop(expiry_timer_);
    httpd_handle_t server = server_;
    server_ = nullptr;
    if (server) (void)httpd_stop(server);
    (void)esp_wifi_stop();
    if (lock_ && xSemaphoreTake(lock_, pdMS_TO_TICKS(100)) == pdTRUE) {
        clear_secrets_locked();
        xSemaphoreGive(lock_);
    }
}

bool WebWifiService::credentials(SetupCredentials& result) const {
    result = {};
    if (!active_ || !lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    result.active = active_;
    std::memcpy(result.ssid, ssid_, sizeof(result.ssid));
    std::memcpy(result.password, password_, sizeof(result.password));
    const int64_t remaining = std::max<int64_t>(0, expires_at_us_ - esp_timer_get_time());
    result.remaining_seconds = static_cast<uint32_t>((remaining + 999999) / 1000000);
    xSemaphoreGive(lock_);
    return result.active;
}

void WebWifiService::expiry_entry(void* argument) {
    static_cast<WebWifiService*>(argument)->stop_requested_ = true;
}
esp_err_t WebWifiService::root_entry(httpd_req_t* request) { return static_cast<WebWifiService*>(request->user_ctx)->serve_root(request); }
esp_err_t WebWifiService::status_entry(httpd_req_t* request) { return static_cast<WebWifiService*>(request->user_ctx)->serve_status(request); }
esp_err_t WebWifiService::mutation_entry(httpd_req_t* request) { return static_cast<WebWifiService*>(request->user_ctx)->mutate(request); }

esp_err_t WebWifiService::serve_root(httpd_req_t* request) {
    if (!active_ || !correct_host(request) || !request_allowed()) return reject(request, HTTPD_403_FORBIDDEN, "Rejected");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(request, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebWifiService::serve_status(httpd_req_t* request) {
    if (!active_ || !correct_host(request)) return reject(request, HTTPD_403_FORBIDDEN, "Rejected");
    char response[64]{};
    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(100)) != pdTRUE) return server_busy(request);
    std::snprintf(response,sizeof(response),"{\"setup\":true,\"csrf\":\"%s\"}",csrf_);
    xSemaphoreGive(lock_);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request,response,HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebWifiService::mutate(httpd_req_t* request) {
    if (!active_ || !correct_host(request) || !correct_origin(request) || !request_allowed() ||
        !csrf_valid(request) || !valid_content_type(request))
        return reject(request, HTTPD_403_FORBIDDEN, "Rejected");
    if (ringing_(ringing_context_)) return conflict(request, "Alarm is ringing");
    char body[max_body + 1]{};
    if (!read_body(request, body)) return reject(request, HTTPD_400_BAD_REQUEST, "Invalid request");
    WebMutation mutation{};
    char value[64]{};
    const char* path = request->uri;
    bool valid = false;
    if (std::strcmp(path, "/api/config/timezone") == 0) {
        mutation.type = WebMutationType::timezone;
        valid = field(body, "zone", value, sizeof(value)) && clock_core::parse_timezone(value, mutation.zone);
    } else if (std::strcmp(path, "/api/config/enroll") == 0) {
        mutation.type = WebMutationType::enroll_tag;
        valid = field(body, "tag", value, sizeof(value)) && clock_core::parse_tag(value, mutation.tag);
    } else if (std::strcmp(path, "/api/config/tag-remove") == 0) {
        mutation.type = WebMutationType::remove_tag;
        valid = field(body, "tag", value, sizeof(value)) && clock_core::parse_tag(value, mutation.tag);
    } else if (std::strcmp(path, "/api/config/alarm-remove") == 0) {
        mutation.type = WebMutationType::remove_alarm;
        uint32_t id = 0;
        valid = field(body, "id", value, sizeof(value)) && parse_u32(value, 1, UINT32_MAX, id);
        mutation.alarm.id = id;
    } else if (std::strcmp(path, "/api/config/alarm") == 0) {
        mutation.type = WebMutationType::alarm;
        uint32_t id = 0, hour = 0, minute = 0, days = 0, enabled = 0;
        valid = field(body, "id", value, sizeof(value)) && parse_u32(value, 1, UINT32_MAX, id) &&
                field(body, "hour", value, sizeof(value)) && parse_u32(value, 0, 23, hour) &&
                field(body, "minute", value, sizeof(value)) && parse_u32(value, 0, 59, minute) &&
                field(body, "days", value, sizeof(value)) && parse_u32(value, 1, 127, days) &&
                field(body, "enabled", value, sizeof(value)) && parse_u32(value, 0, 1, enabled);
        mutation.alarm = {id, static_cast<uint8_t>(hour), static_cast<uint8_t>(minute),
                          static_cast<uint8_t>(days), enabled != 0};
    } else if (std::strcmp(path, "/api/config/display") == 0) {
        mutation.type = WebMutationType::display;
        uint32_t brightness = 0, ambient = 0;
        valid = field(body, "brightness", value, sizeof(value)) && parse_u32(value, 1, 255, brightness) &&
                field(body, "ambient", value, sizeof(value)) && parse_u32(value, 0, 1, ambient);
        mutation.brightness = static_cast<uint8_t>(brightness);
        mutation.ambient_brightness = ambient != 0;
    } else if (std::strcmp(path, "/api/config/volume") == 0) {
        mutation.type = WebMutationType::alarm_volume;
        uint32_t volume = 0;
        valid = field(body, "volume", value, sizeof(value)) && parse_u32(value, 1, 5, volume);
        mutation.volume = static_cast<uint8_t>(volume);
    } else if (std::strcmp(path, "/api/config/name") == 0) {
        mutation.type = WebMutationType::device_name;
        valid = field(body, "name", mutation.device_name, sizeof(mutation.device_name));
        mutation.device_name_length = static_cast<uint8_t>(std::strlen(mutation.device_name));
        for (size_t i = 0; valid && i < mutation.device_name_length; ++i) {
            const auto byte = static_cast<unsigned char>(mutation.device_name[i]);
            valid = byte >= 0x20 && byte <= 0x7e;
        }
    } else if (std::strcmp(path, "/api/config/time") == 0) {
        mutation.type = WebMutationType::manual_utc;
        clock_core::DateTime utc{};
        valid = field(body, "utc", value, sizeof(value)) && clock_core::parse_utc(value, utc);
        if (valid) mutation.utc_seconds = clock_core::epoch_seconds(utc);
    }
    std::memset(body, 0, sizeof(body));
    if (!valid) return reject(request, HTTPD_400_BAD_REQUEST, "Invalid configuration");
    if (!mutation_(mutation, mutation_context_)) return conflict(request, "Configuration rejected");
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"queued\":true}");
}

bool WebWifiService::request_allowed() {
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    uint32_t previous = last_request_ms_.load();
    while (static_cast<uint32_t>(now - previous) >= request_interval_ms) {
        if (last_request_ms_.compare_exchange_weak(previous, now)) return true;
    }
    return false;
}

bool WebWifiService::correct_host(httpd_req_t* request) const {
    return exact_header(request, "Host", kHost) || exact_header(request, "Host", "192.168.4.1:80");
}

bool WebWifiService::correct_origin(httpd_req_t* request) const { return exact_header(request, "Origin", kOrigin); }

bool WebWifiService::csrf_valid(httpd_req_t* request) const {
    char supplied[40]{};
    const size_t length = httpd_req_get_hdr_value_len(request, "X-Setup-CSRF");
    if (length != 32 || httpd_req_get_hdr_value_str(request, "X-Setup-CSRF", supplied, sizeof(supplied)) != ESP_OK) return false;
    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < 32; ++i) difference |= static_cast<unsigned>(supplied[i] ^ csrf_[i]);
    xSemaphoreGive(lock_);
    return difference == 0;
}

bool WebWifiService::read_body(httpd_req_t* request, char (&body)[max_body + 1]) const {
    if (request->content_len <= 0 || request->content_len > static_cast<int>(max_body)) return false;
    size_t received = 0;
    while (received < static_cast<size_t>(request->content_len)) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) return false;
        received += static_cast<size_t>(count);
    }
    body[received] = '\0';
    return true;
}

void WebWifiService::clear_secrets_locked() {
    std::memset(ssid_, 0, sizeof(ssid_));
    std::memset(password_, 0, sizeof(password_));
    std::memset(csrf_, 0, sizeof(csrf_));
    expires_at_us_ = 0;
}

} // namespace clock_app
