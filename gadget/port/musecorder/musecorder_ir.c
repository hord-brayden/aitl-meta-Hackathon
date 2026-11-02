// IR transmitter (RMT, 38 kHz) for the Musecorder port. See musecorder_ir.h.
#include "musecorder_ir.h"
#include <string.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "mc_ir";

#define IR_RESOLUTION_HZ 1000000   // 1 tick = 1 us
#define IR_CARRIER_HZ    38000
#define NEC_MAX_SYMBOLS  68        // header + 32 bits + stop

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_copy;   // we build the symbols ourselves; copy them out verbatim
static bool s_ready;

// One RMT symbol is a mark (carrier on) for d0 us then a space (carrier off) for d1 us.
static inline rmt_symbol_word_t mark_space(uint16_t mark_us, uint16_t space_us) {
    rmt_symbol_word_t s;
    s.level0 = 1; s.duration0 = mark_us;
    s.level1 = 0; s.duration1 = space_us;
    return s;
}

bool musecorder_ir_init(int gpio) {
    if (s_ready) return true;
    rmt_tx_channel_config_t ch_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = gpio,
    };
    if (rmt_new_tx_channel(&ch_cfg, &s_chan) != ESP_OK) { ESP_LOGW(TAG, "tx channel"); return false; }

    rmt_carrier_config_t carrier = {
        .frequency_hz = IR_CARRIER_HZ,
        .duty_cycle = 0.33,
    };
    if (rmt_apply_carrier(s_chan, &carrier) != ESP_OK) { ESP_LOGW(TAG, "carrier"); return false; }

    rmt_copy_encoder_config_t copy_cfg = {};   // no fields in this IDF; empty init
    if (rmt_new_copy_encoder(&copy_cfg, &s_copy) != ESP_OK) { ESP_LOGW(TAG, "encoder"); return false; }
    if (rmt_enable(s_chan) != ESP_OK) { ESP_LOGW(TAG, "enable"); return false; }

    s_ready = true;
    ESP_LOGI(TAG, "IR TX up on GPIO%d (38 kHz)", gpio);
    return true;
}

bool musecorder_ir_ready(void) { return s_ready; }

static bool send(const rmt_symbol_word_t *syms, size_t n) {
    rmt_transmit_config_t tx = { .loop_count = 0 };
    if (rmt_transmit(s_chan, s_copy, syms, n * sizeof(syms[0]), &tx) != ESP_OK) return false;
    return rmt_tx_wait_all_done(s_chan, 1000) == ESP_OK;
}

bool musecorder_ir_nec(uint8_t address, uint8_t command) {
    if (!s_ready) return false;
    rmt_symbol_word_t syms[NEC_MAX_SYMBOLS];
    size_t i = 0;
    syms[i++] = mark_space(9000, 4500);                    // NEC leader
    uint8_t bytes[4] = { address, (uint8_t)~address, command, (uint8_t)~command };
    for (int b = 0; b < 4; b++) {
        for (int bit = 0; bit < 8; bit++) {               // LSB first
            if (bytes[b] & (1 << bit)) syms[i++] = mark_space(560, 1690);   // 1
            else                       syms[i++] = mark_space(560, 560);    // 0
        }
    }
    syms[i++] = mark_space(560, 0);                        // stop mark
    return send(syms, i);
}

bool musecorder_ir_nec32(uint32_t code) {
    if (!s_ready) return false;
    // Send a 32-bit IR code MSB-first with NEC timing. IR databases (IRremote, Flipper)
    // print codes in this "as transmitted" MSB-first form, e.g. Samsung 0xE0E040BF or
    // Vizio/LG 0x20DF10EF (= NEC address 4, command 8). Sending the raw 32 bits MSB-first
    // reproduces that waveform exactly.
    rmt_symbol_word_t syms[NEC_MAX_SYMBOLS];
    size_t i = 0;
    syms[i++] = mark_space(9000, 4500);                    // NEC leader
    for (int bit = 31; bit >= 0; bit--) {                  // MSB first
        if (code & (1u << bit)) syms[i++] = mark_space(560, 1690);   // 1
        else                    syms[i++] = mark_space(560, 560);    // 0
    }
    syms[i++] = mark_space(560, 0);                        // stop mark
    return send(syms, i);
}

bool musecorder_ir_raw(const uint16_t *durations_us, size_t count) {
    if (!s_ready || !durations_us || count == 0) return false;
    // Pack pairs of (mark, space) into symbols; an odd final mark gets a 0 space.
    size_t pairs = (count + 1) / 2;
    rmt_symbol_word_t *syms = (rmt_symbol_word_t *)calloc(pairs, sizeof(rmt_symbol_word_t));
    if (!syms) return false;
    for (size_t p = 0; p < pairs; p++) {
        uint16_t mark = durations_us[2 * p];
        uint16_t space = (2 * p + 1 < count) ? durations_us[2 * p + 1] : 0;
        syms[p] = mark_space(mark, space);
    }
    bool ok = send(syms, pairs);
    free(syms);
    return ok;
}

// ---------------------------------------------------------------- receive / learn
#define IR_RX_SYMBOLS 96            // one burst; NEC is ~34, most remotes fit in 96

static rmt_channel_handle_t s_rx_chan;
static QueueHandle_t s_rx_queue;
static bool s_rx_ready;

static bool IRAM_ATTR rx_done_cb(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *edata, void *user) {
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user, edata, &hp);   // copy the event (ptr + count) to the waiter
    return hp == pdTRUE;
}

bool musecorder_ir_rx_init(int gpio) {
    if (s_rx_ready) return true;
    rmt_rx_channel_config_t cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,   // 1 tick = 1 us
        .mem_block_symbols = IR_RX_SYMBOLS,
        .gpio_num = gpio,
    };
    if (rmt_new_rx_channel(&cfg, &s_rx_chan) != ESP_OK) { ESP_LOGW(TAG, "rx channel"); return false; }
    s_rx_queue = xQueueCreate(2, sizeof(rmt_rx_done_event_data_t));
    if (!s_rx_queue) { ESP_LOGW(TAG, "rx queue"); return false; }
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_done_cb };
    if (rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rx_queue) != ESP_OK) { ESP_LOGW(TAG, "rx cb"); return false; }
    if (rmt_enable(s_rx_chan) != ESP_OK) { ESP_LOGW(TAG, "rx enable"); return false; }
    // The demodulator output idles HIGH and pulls low on a carrier burst. Without an
    // internal pull-up the StickS3's GPIO42 floats, so RMT sees one spurious edge
    // instead of the ~34 of a real frame. (Confirmed on this board's RX line.)
    gpio_set_pull_mode((gpio_num_t)gpio, GPIO_PULLUP_ONLY);
    s_rx_ready = true;
    ESP_LOGI(TAG, "IR RX up on GPIO%d (pull-up on)", gpio);
    return true;
}

bool musecorder_ir_rx_ready(void) { return s_rx_ready; }

bool musecorder_ir_capture(uint16_t *out, size_t cap, size_t *count, int timeout_ms) {
    if (!s_rx_ready || !out || !count || cap < 4) { ESP_LOGW(TAG, "capture: not ready/bad args"); return false; }
    static rmt_symbol_word_t raw[IR_RX_SYMBOLS];
    rmt_receive_config_t rcfg = {
        .signal_range_min_ns = 1000,          // ignore pulses < 1 us (noise)
        .signal_range_max_ns = 12000000,      // a gap > 12 ms ends the burst
    };
    esp_err_t e = rmt_receive(s_rx_chan, raw, sizeof(raw), &rcfg);
    if (e != ESP_OK) { ESP_LOGW(TAG, "rmt_receive: %s", esp_err_to_name(e)); return false; }
    rmt_rx_done_event_data_t ev;
    if (xQueueReceive(s_rx_queue, &ev, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) { ESP_LOGW(TAG, "capture: timed out (no pulses on GPIO)"); return false; }
    size_t n = 0;
    for (size_t i = 0; i < ev.num_symbols && n + 1 < cap; i++) {
        out[n++] = ev.received_symbols[i].duration0;      // mark
        if (ev.received_symbols[i].duration1 == 0) break; // last symbol has a 0 tail
        out[n++] = ev.received_symbols[i].duration1;       // space
    }
    *count = n;
    ESP_LOGI(TAG, "capture: %u symbols -> %u edges", (unsigned)ev.num_symbols, (unsigned)n);
    char d[160]; int p = 0;                   // dump the first dozen durations so we can tell a real frame from noise
    for (size_t i = 0; i < n && i < 12 && p < (int)sizeof(d) - 8; i++) p += snprintf(d + p, sizeof(d) - p, "%u ", out[i]);
    ESP_LOGI(TAG, "  first us: %s", d);
    return n >= 4;   // a real remote press is at least a leader + one bit
}
