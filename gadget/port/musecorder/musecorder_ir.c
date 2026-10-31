// IR transmitter (RMT, 38 kHz) for the Musecorder port. See musecorder_ir.h.
#include "musecorder_ir.h"
#include <string.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
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
