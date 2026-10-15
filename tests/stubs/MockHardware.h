#pragma once
#include <cstdint>
#include <cstddef>

using gpio_num_t = int;
using ledc_channel_t = int;
using ledc_timer_t = int;
using esp_err_t = int;
using rmt_channel_handle_t = void*;
using rmt_encoder_handle_t = void*;
using gptimer_handle_t = void*;
constexpr int ESP_OK = 0, GPIO_NUM_4 = 4, LEDC_CHANNEL_0 = 0, LEDC_TIMER_0 = 0;
constexpr int LEDC_TIMER_16_BIT = 16, LEDC_TIMER_12_BIT = 12, LEDC_LOW_SPEED_MODE = 0;
constexpr int LEDC_AUTO_CLK = 0, LEDC_INTR_DISABLE = 0, RMT_CLK_SRC_DEFAULT = 0;
constexpr int GPTIMER_CLK_SRC_DEFAULT = 0, GPTIMER_COUNT_UP = 0;
inline int hardwareWrites = 0;
inline uint32_t outputDuty = 0, outputPhase = 0, outputFrequency = 0;
inline bool rmtEnabled = false, macroTimerRunning = false;

struct ledc_timer_config_t {
    int speed_mode, duty_resolution, timer_num;
    uint32_t freq_hz;
    int clk_cfg;
};
struct ledc_channel_config_t {
    int gpio_num, speed_mode, channel, intr_type, timer_sel;
    uint32_t duty, hpoint;
};
struct rmt_tx_channel_config_t {
    int gpio_num, clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols, trans_queue_depth;
};
struct rmt_copy_encoder_config_t {};
struct rmt_symbol_word_t {
    uint32_t duration0 : 15;
    uint32_t level0 : 1;
    uint32_t duration1 : 15;
    uint32_t level1 : 1;
};
struct rmt_transmit_config_t { int loop_count; };
struct gptimer_config_t { int clk_src, direction; uint32_t resolution_hz; };
struct gptimer_alarm_event_data_t {};
struct gptimer_event_callbacks_t {
    bool (*on_alarm)(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*);
};
struct gptimer_alarm_config_t {
    uint64_t reload_count, alarm_count;
    struct { bool auto_reload_on_alarm; } flags;
};

inline int ledc_timer_config(const ledc_timer_config_t* p) { ++hardwareWrites; outputFrequency=p->freq_hz; return 0; }
inline int ledc_channel_config(const ledc_channel_config_t* p) { ++hardwareWrites; outputDuty=p->duty; outputPhase=p->hpoint; return 0; }
inline int ledc_set_duty(int, int, uint32_t duty) { ++hardwareWrites; outputDuty=duty; return 0; }
inline int ledc_update_duty(int, int) { ++hardwareWrites; return 0; }
inline int ledc_stop(int, int, int) { ++hardwareWrites; outputDuty=0; return 0; }
inline int ledc_set_freq(int, int, uint32_t hz) { ++hardwareWrites; outputFrequency=hz; return 0; }
inline uint32_t ledc_get_freq(int, int) { return outputFrequency; }
inline int ledc_set_duty_and_update(int, int, uint32_t duty, uint32_t phase) { ++hardwareWrites; outputDuty=duty; outputPhase=phase; return 0; }
inline int rmt_new_tx_channel(const rmt_tx_channel_config_t*, rmt_channel_handle_t* h) { ++hardwareWrites; *h=reinterpret_cast<void*>(1); return 0; }
inline int rmt_new_copy_encoder(const rmt_copy_encoder_config_t*, rmt_encoder_handle_t* h) { ++hardwareWrites; *h=reinterpret_cast<void*>(2); return 0; }
inline int rmt_enable(rmt_channel_handle_t) { ++hardwareWrites; if(rmtEnabled)return 1; rmtEnabled=true; return 0; }
inline int rmt_disable(rmt_channel_handle_t) { ++hardwareWrites; rmtEnabled=false; return 0; }
inline int rmt_del_channel(rmt_channel_handle_t) { ++hardwareWrites; return 0; }
inline int rmt_del_encoder(rmt_encoder_handle_t) { ++hardwareWrites; return 0; }
inline int rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void*, size_t, const rmt_transmit_config_t*) { ++hardwareWrites; return 0; }
inline int gptimer_new_timer(const gptimer_config_t*, gptimer_handle_t* h) { ++hardwareWrites; *h=reinterpret_cast<void*>(3); return 0; }
inline int gptimer_stop(gptimer_handle_t) { ++hardwareWrites; macroTimerRunning=false; return 0; }
inline int gptimer_disable(gptimer_handle_t) { ++hardwareWrites; return 0; }
inline int gptimer_del_timer(gptimer_handle_t) { ++hardwareWrites; return 0; }
inline int gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t*, void*) { ++hardwareWrites; return 0; }
inline int gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t*) { ++hardwareWrites; return 0; }
inline int gptimer_enable(gptimer_handle_t) { ++hardwareWrites; return 0; }
inline int gptimer_start(gptimer_handle_t) { ++hardwareWrites; macroTimerRunning=true; return 0; }
