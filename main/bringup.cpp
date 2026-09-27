#include "bringup.hpp"
#include "alarm_service.hpp"
#include "core/alarm_core.hpp"
#include "core/test_tone.hpp"
#include "drivers/i2c_bus.hpp"
#include "drivers/timekeeping.hpp"
#include "drivers/controls.hpp"
#include "drivers/sensing.hpp"
#include "drivers/display.hpp"
#include "drivers/audio.hpp"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(CONFIG_CLOCK_SIMULATED_NFC) && !defined(CONFIG_CLOCK_DEVELOPMENT_BUILD)
#error "Simulated NFC requires the development build gate"
#endif

namespace {
enum class CommandType { status, scan, set_time, audio, brightness, ambient, alarm_status,
                         alarm_set, alarm_zone, tag_enroll, simulated_tag, alarm_trigger };
struct Command {
    CommandType type;
    clock_core::DateTime time{};
    int value = 0;
    clock_core::AlarmDefinition alarm{};
    clock_core::TagId tag{};
    clock_core::TimeZone zone = clock_core::TimeZone::utc;
};
QueueHandle_t commands = nullptr;
clock_hw::I2cBus bus;
clock_hw::Timekeeping timekeeping(bus);
clock_hw::Controls controls(bus);
clock_hw::Sensing sensing(bus);
clock_hw::Display display(bus);
clock_hw::Audio audio;
clock_app::AlarmService alarm;
int brightness = 79;
bool ambient = false, status_page = false;
int nfc_address = -1;

int send(Command command) {
    if (!commands || xQueueSend(commands,&command,0) != pdTRUE) {
        std::puts("Busy: command queue full/unavailable; retry"); return 1;
    }
    std::puts("Queued; result follows in the device log"); return 0;
}
int status_command(int argc, char**) { return argc == 1 ? send({CommandType::status}) : 1; }
int scan_command(int argc, char**) { return argc == 1 ? send({CommandType::scan}) : 1; }
int audio_command(int argc, char** argv) {
    uint8_t level = 1;
    if (argc < 1 || argc > 2 || !clock_core::parse_test_level(argc == 2 ? argv[1] : nullptr,level)) {
        std::puts("Usage: audio_test [1|5] (percent digital peak; fixed 2 seconds)"); return 1;
    }
    return send({CommandType::audio,{},level});
}
int set_command(int argc, char** argv) {
    Command command{CommandType::set_time};
    if (argc != 2 || !clock_core::parse_utc(argv[1],command.time)) {
        std::puts("Usage: clock_set YYYY-MM-DDTHH:MM:SSZ (UTC, years 2000-2099)"); return 1;
    }
    return send(command);
}
int brightness_command(int argc, char** argv) {
    if (argc != 2 || !argv[1][0]) return 1;
    for (const char* p = argv[1]; *p; ++p) if (*p < '0' || *p > '9') return 1;
    if (std::strlen(argv[1]) > 3) return 1;
    int value = std::atoi(argv[1]);
    if (value < 1 || value > 255) { std::puts("Brightness range: 1-255"); return 1; }
    return send({CommandType::brightness,{},value});
}
int ambient_command(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1],"on") && std::strcmp(argv[1],"off"))) return 1;
    return send({CommandType::ambient,{},!std::strcmp(argv[1],"on")});
}
int alarm_status_command(int argc, char**) { return argc == 1 ? send({CommandType::alarm_status}) : 1; }
#ifdef CONFIG_CLOCK_SIMULATED_NFC
int alarm_set_command(int argc, char** argv) {
    if (argc != 3 || std::strlen(argv[1]) != 5 || argv[1][2] != ':' || std::strlen(argv[2]) != 7)
        return 1;
    if (argv[1][0] < '0' || argv[1][0] > '9' || argv[1][1] < '0' || argv[1][1] > '9' ||
        argv[1][3] < '0' || argv[1][3] > '9' || argv[1][4] < '0' || argv[1][4] > '9') return 1;
    Command command{CommandType::alarm_set};
    command.alarm = {1,static_cast<uint8_t>((argv[1][0]-'0')*10+argv[1][1]-'0'),
        static_cast<uint8_t>((argv[1][3]-'0')*10+argv[1][4]-'0'),0,true};
    for (int i = 0; i < 7; ++i) {
        if (argv[2][i] != '0' && argv[2][i] != '1') return 1;
        if (argv[2][i] == '1') command.alarm.weekdays |= 1U << i;
    }
    if (command.alarm.hour > 23 || command.alarm.minute > 59 || command.alarm.weekdays == 0) return 1;
    return send(command);
}
int alarm_zone_command(int argc, char** argv) {
    Command command{CommandType::alarm_zone};
    if (argc != 2 || !clock_core::parse_timezone(argv[1],command.zone)) return 1;
    return send(command);
}
int tag_command(CommandType type, int argc, char** argv) {
    Command command{type};
    if (argc != 2 || !clock_core::parse_tag(argv[1],command.tag)) return 1;
    return send(command);
}
int tag_enroll_command(int argc, char** argv) { return tag_command(CommandType::tag_enroll,argc,argv); }
int simulated_tag_command(int argc, char** argv) { return tag_command(CommandType::simulated_tag,argc,argv); }
int alarm_trigger_command(int argc, char**) { return argc == 1 ? send({CommandType::alarm_trigger}) : 1; }
#endif
void scan() {
    ESP_LOGI("i2c", "discovery SDA=8 SCL=9 at 100kHz; ACK does not establish device identity");
    unsigned count = 0; nfc_address = -1;
    for (uint8_t a = 8; a < 120; ++a) {
        auto err = bus.probe(a);
        if (err == ESP_OK) {
            ESP_LOGI("i2c", "ACK at 0x%02x",a); ++count;
            if (a >= 0x28 && a <= 0x2b) nfc_address = a;
        }
        else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW("i2c", "scan stopped at 0x%02x: %s; check pull-ups/bus wiring",a,esp_err_to_name(err));
            break;
        }
    }
    ESP_LOGI("i2c", "%u ACKs; expected OLED=3C RTC=68 light=23 encoder=36; PN7160 expected 28-2B but NCI not started",count);
}
void format_time(char* text, size_t size, const char* format) {
    time_t now = std::time(nullptr);
    tm utc{}; gmtime_r(&now,&utc);
    std::strftime(text,size,format,&utc);
}
void report() {
    char text[32] = "INVALID";
    if (timekeeping.valid()) format_time(text,sizeof(text),"%Y-%m-%dT%H:%M:%SZ");
    ESP_LOGI("status", "UTC=%s source=%s RTC=%s",text,timekeeping.source(),timekeeping.rtc_status());
    ESP_LOGI("status", "OLED=%s encoder=%s audio=%s NFC=%s",display.online()?"OK":"MISSING/IO ERROR",controls.status(),audio.status(),
             nfc_address < 0 ? "NOT SEEN / NCI NOT STARTED" : "I2C ACK ONLY / NCI NOT STARTED");
    ESP_LOGI("status", "battery_mV=%d raw_ADC=%d calibrated=%d lux=%.1f (-1=unavailable); brightness=%d ambient=%d",
             sensing.battery_mv,sensing.raw_adc,sensing.calibrated,sensing.lux,brightness,ambient);
    ESP_LOGI("status", "M3 alarm=%s active=%u missed=%u skipped=%u; no Wi-Fi or persistent settings",
             alarm.state_name(),alarm.active_count(),alarm.missed_count(),alarm.skipped_count());
    alarm.request_status();
}
void render() {
    display.clear();
#ifdef CONFIG_CLOCK_SIMULATED_NFC
    if (alarm.ringing()) display.text(0,0,"DEV SIM ALARM RINGING");
    else display.text(0,0,"DEV SIM NFC - RAM ONLY");
#elif defined(CONFIG_CLOCK_DEVELOPMENT_BUILD)
    if (alarm.ringing()) display.text(0,0,"DEV ALARM RINGING");
    else display.text(0,0,"DEVELOPMENT BUILD");
#else
    if (alarm.ringing()) display.text(0,0,"ALARM RINGING - NFC REQUIRED");
    else display.text(0,0,"M3 CORE - CONFIG PENDING");
#endif
    char line[40];
    if (!status_page) {
        if (timekeeping.valid()) format_time(line,sizeof(line),"%H:%M:%S");
        else std::strcpy(line,"--:--:--");
        display.text(0,12,line,3);
        if (timekeeping.valid()) format_time(line,sizeof(line),"%Y-%m-%d UTC");
        else std::strcpy(line,"SET UTC VIA SERIAL");
        display.text(0,32,line);
        std::snprintf(line,sizeof(line),"RTC %s",timekeeping.rtc_status()); display.text(0,42,line);
        std::snprintf(line,sizeof(line),"BAT %d MV  LUX %.0f",sensing.battery_mv,sensing.lux); display.text(0,51,line);
        std::snprintf(line,sizeof(line),"ALARM %s NFC %s",alarm.state_name(),nfc_address < 0 ? "NO ACK" : "ACK ONLY");
        display.text(0,59,line);
    } else {
        std::snprintf(line,sizeof(line),"ENCODER %s",controls.status()); display.text(0,12,line);
        std::snprintf(line,sizeof(line),"AUDIO %s",audio.status()); display.text(0,22,line);
        std::snprintf(line,sizeof(line),"BRIGHTNESS %d AUTO %s",brightness,ambient?"ON":"OFF"); display.text(0,32,line);
        std::snprintf(line,sizeof(line),"ADC %d  BAT %d MV",sensing.raw_adc,sensing.battery_mv); display.text(0,42,line);
        std::snprintf(line,sizeof(line),"ALARM %s ACTIVE %u",alarm.state_name(),alarm.active_count()); display.text(0,52,line);
        display.text(0,59,"PN7160 NCI NOT STARTED");
    }
    display.present();
}
void peripheral_task(void*) {
    auto err = bus.init();
    ESP_LOGI("bringup", "I2C init: %s",esp_err_to_name(err));
    ESP_LOGI("bringup", "buttons: %s",esp_err_to_name(controls.init_buttons()));
    ESP_LOGI("bringup", "ADC: %s",esp_err_to_name(sensing.init_adc()));
    ESP_LOGI("bringup", "audio driver: %s (speaker presence cannot be detected)",esp_err_to_name(audio.init()));
    ESP_LOGI("bringup", "alarm task: %s",esp_err_to_name(alarm.init(audio)));
    // Display reset happens in service before discovery so a held-reset OLED
    // isn't incorrectly reported absent by the initial scan.
    render(); display.service(esp_timer_get_time()/1000,brightness);
    if (err == ESP_OK) scan();
    int64_t slow_at = 0, log_at = 0;
    while (true) {
        const int64_t now_ms = esp_timer_get_time()/1000;
        const auto events = controls.poll(now_ms);
        if (events.button1 || events.encoder_button) { status_page = !status_page; render(); }
        if (events.button2) report(); // Never an audio/dismissal shortcut.
        if (events.turn) {
            // Limit any implausible large step after an encoder reset to one UI
            // update's range; no overflow or accidental wrap to full brightness.
            brightness = std::clamp(brightness + int(std::clamp(events.turn,int64_t(-16),int64_t(16))) * 4,1,255);
            ESP_LOGI("controls", "encoder delta=%lld brightness=%d",static_cast<long long>(events.turn),brightness);
            render();
        }
        Command command{};
        if (xQueueReceive(commands,&command,0) == pdTRUE) {
            switch (command.type) {
            case CommandType::status: report(); break;
            case CommandType::scan: scan(); break;
            case CommandType::set_time:
                if (alarm.ringing()) ESP_LOGW("bringup", "clock_set rejected while alarm is ringing");
                else timekeeping.set(command.time);
                render(); break;
            case CommandType::audio:
                ESP_LOGI("bringup", "%s",!alarm.ringing() && audio.request_test(command.value)?"audio test accepted":"audio test rejected: ringing/busy/unavailable"); break;
            case CommandType::brightness: brightness = command.value; render(); break;
            case CommandType::ambient: ambient = command.value; render(); break;
            case CommandType::alarm_status: alarm.request_status(); break;
            case CommandType::alarm_set: alarm.configure(command.alarm); break;
            case CommandType::alarm_zone: alarm.set_timezone(command.zone); break;
            case CommandType::tag_enroll: alarm.enroll(command.tag); break;
            case CommandType::simulated_tag: alarm.submit_tag(command.tag); break;
            case CommandType::alarm_trigger: alarm.development_trigger(); break;
            }
        }
        if (now_ms >= slow_at) {
            timekeeping.poll(); sensing.poll(now_ms);
            alarm.tick(timekeeping.valid(),timekeeping.valid() ? std::time(nullptr) : 0);
            render(); slow_at = now_ms + 1000;
        }
        // Ambient setting acts as a ceiling with manual brightness. A missing
        // light sensor falls back to manual; it never blanks the display.
        int actual = brightness;
        if (ambient && sensing.lux >= 0) {
            const int light_cap = sensing.lux < 5 ? 8 : (sensing.lux < 50 ? 32 : (sensing.lux < 200 ? 79 : 160));
            actual = std::min(brightness,light_cap);
        }
        display.service(now_ms,actual);
        if (now_ms >= log_at) { report(); log_at = now_ms + 10000; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void console() {
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "clock> "; cfg.max_cmdline_length = 96;
    esp_console_repl_t* repl = nullptr;
    auto err = esp_console_new_repl_stdio(&cfg,&repl);
    if (err != ESP_OK) { ESP_LOGE("console", "init failed: %s",esp_err_to_name(err)); return; }
    const esp_console_cmd_t entries[]{
        {.command="clock_status",.help="Report time/peripheral status",.hint=nullptr,.func=status_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr},
        {.command="i2c_scan",.help="Scan non-reserved I2C addresses",.hint=nullptr,.func=scan_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr},
        {.command="clock_set",.help="Set UTC: clock_set YYYY-MM-DDTHH:MM:SSZ",.hint=nullptr,.func=set_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr},
        {.command="audio_test",.help="audio_test [1|5]: 2-second 440Hz test, default 1% peak",.hint=nullptr,.func=audio_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr},
        {.command="brightness",.help="Set OLED contrast 1-255 (RAM only)",.hint=nullptr,.func=brightness_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr},
        {.command="ambient",.help="ambient on|off (RAM only)",.hint=nullptr,.func=ambient_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
        ,{.command="alarm_status",.help="Report alarm-core state",.hint=nullptr,.func=alarm_status_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
#ifdef CONFIG_CLOCK_SIMULATED_NFC
        ,{.command="tag_enroll",.help="DEV RAM only: tag_enroll HEX_UID",.hint=nullptr,.func=tag_enroll_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
        ,{.command="alarm_zone",.help="DEV RAM only: UTC|America/Los_Angeles",.hint=nullptr,.func=alarm_zone_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
        ,{.command="alarm_set",.help="DEV RAM only: alarm_set HH:MM SMTWTFS mask (0/1)",.hint=nullptr,.func=alarm_set_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
        ,{.command="alarm_trigger",.help="DEV: trigger now through alarm state machine",.hint=nullptr,.func=alarm_trigger_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
        ,{.command="nfc_sim",.help="DEV SIM: nfc_sim HEX_UID; same authorization path as reader",.hint=nullptr,.func=simulated_tag_command,.argtable=nullptr,.func_w_context=nullptr,.context=nullptr}
#endif
    };
    err = esp_console_register_help_command();
    for (const auto& entry : entries) if (err == ESP_OK) err = esp_console_cmd_register(&entry);
    if (err == ESP_OK) err = esp_console_start_repl(repl);
    if (err != ESP_OK) ESP_LOGE("console", "start failed: %s",esp_err_to_name(err));
}
}
void start_bringup() {
    commands = xQueueCreate(8,sizeof(Command));
    if (!commands || xTaskCreate(peripheral_task,"clock_io",8192,nullptr,3,nullptr) != pdPASS) {
        ESP_LOGE("bringup", "cannot allocate peripheral task/queue"); return;
    }
    console();
}
