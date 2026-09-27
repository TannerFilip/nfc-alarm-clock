#pragma once

#include "core/alarm_core.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace clock_app {

// A bounded message for the configuration owner. The callback must validate the
// request against durable configuration and enqueue it; it runs on an HTTP task.
enum class WebMutationType : uint8_t {
    timezone,
    alarm,
    remove_alarm,
    enroll_tag,
    remove_tag,
    display,
    alarm_volume,
    device_name,
    manual_utc
};

struct WebMutation {
    WebMutationType type = WebMutationType::timezone;
    clock_core::TimeZone zone = clock_core::TimeZone::utc;
    clock_core::AlarmDefinition alarm{};
    clock_core::TagId tag{};
    uint8_t brightness = 0;
    bool ambient_brightness = false;
    uint8_t volume = 0;
    char device_name[25]{};
    uint8_t device_name_length = 0;
    int64_t utc_seconds = 0;
};

struct SetupCredentials {
    bool active = false;
    char ssid[24]{};
    char password[17]{};
    uint32_t remaining_seconds = 0;
};

class WebWifiService {
public:
    using RingingPredicate = bool (*)(void* context);
    using MutationCallback = bool (*)(const WebMutation& mutation, void* context);

    // The caller retains both contexts for the lifetime of this service.
    esp_err_t init(RingingPredicate ringing, void* ringing_context,
                   MutationCallback mutation, void* mutation_context);

    // Starts a WPA2-only AP for a bounded interval. Credentials are regenerated
    // for every successful start and are available only through credentials().
    // No credential or token is ever logged by this module.
    esp_err_t start_setup(uint32_t duration_seconds = 15 * 60);
    void stop_setup();
    void service(); // Performs deferred expiry shutdown outside the esp_timer callback.
    bool credentials(SetupCredentials& result) const;
    bool active() const { return active_.load(); }

private:
    static constexpr size_t max_body = 256;
    static constexpr uint32_t min_duration_seconds = 60;
    static constexpr uint32_t max_duration_seconds = 30 * 60;
    static constexpr uint32_t request_interval_ms = 150;

    static void expiry_entry(void* argument);
    static esp_err_t root_entry(httpd_req_t* request);
    static esp_err_t status_entry(httpd_req_t* request);
    static esp_err_t mutation_entry(httpd_req_t* request);

    esp_err_t serve_root(httpd_req_t* request);
    esp_err_t serve_status(httpd_req_t* request);
    esp_err_t mutate(httpd_req_t* request);

    bool request_allowed();
    bool correct_host(httpd_req_t* request) const;
    bool correct_origin(httpd_req_t* request) const;
    bool csrf_valid(httpd_req_t* request) const;
    bool read_body(httpd_req_t* request, char (&body)[max_body + 1]) const;
    void clear_secrets_locked();

    mutable StaticSemaphore_t lock_storage_{};
    mutable SemaphoreHandle_t lock_ = nullptr;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> active_{false};
    std::atomic<bool> stop_requested_{false};
    RingingPredicate ringing_ = nullptr;
    void* ringing_context_ = nullptr;
    MutationCallback mutation_ = nullptr;
    void* mutation_context_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
    httpd_handle_t server_ = nullptr;
    esp_timer_handle_t expiry_timer_ = nullptr;
    int64_t expires_at_us_ = 0;
    std::atomic<uint32_t> last_request_ms_{0};
    char ssid_[24]{};
    char password_[17]{};
    char csrf_[33]{};
};

} // namespace clock_app
