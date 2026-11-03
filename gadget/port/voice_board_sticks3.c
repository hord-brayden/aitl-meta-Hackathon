// Voice board for the M5Stack StickS3: push-to-talk microphone capture through
// the on-board ES8311 codec, for CONFIG_HOMEHUB_VOICE. Implements voice_board.h.
//
// This mirrors Meta's own working StickS3 audio bring-up
// (components/muse/boards/board_m5stack_sticks3.c, audio_init): a full-duplex I2S
// (MCLK G18, BCLK G17, WS G15, DAC/speaker G14, ADC/mic G16) is created and
// ENABLED before the codec is configured, because the ES8311 clocks its control
// logic off MCLK and won't accept its I2C register writes without it. The speaker
// amp is the PMIC's PYG3 line, which the codec toggles through a small gpio_if.
// The push-to-talk reply comes back as text in the Muse app, so the speaker path
// stays minimal here (no PCM is written until a TTS path feeds voice_player).
#include "voice_board.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sticks3_lcd.h"   // sticks3_i2c_bus(), sticks3_spk_amp()

static const char *TAG = "voice.sticks3";

#define ES8311_I2C_ADDR 0x18
#define PIN_MCLK  GPIO_NUM_18
#define PIN_BCLK  GPIO_NUM_17
#define PIN_WS    GPIO_NUM_15
#define PIN_DOUT  GPIO_NUM_14   // ESP -> codec DAC (speaker)
#define PIN_DIN   GPIO_NUM_16   // codec ADC -> ESP (microphone)
#define MIC_GAIN_DB 30.0f

static i2s_chan_handle_t s_tx, s_rx;
static esp_codec_dev_handle_t s_mic, s_spk;
static bool s_inited;
static bool s_mic_open;

// The codec driver switches the AW8737 amp through this (pa_pin), which we route
// to the PMIC's PYG3 line via sticks3_spk_amp().
static int amp_gpio_setup(int16_t gpio, audio_gpio_dir_t dir, audio_gpio_mode_t mode) {
    (void)gpio; (void)dir; (void)mode; return ESP_CODEC_DEV_OK;
}
static int amp_gpio_set(int16_t gpio, bool high) { (void)gpio; sticks3_spk_amp(high); return ESP_CODEC_DEV_OK; }
static bool amp_gpio_get(int16_t gpio) { (void)gpio; return false; }
static const audio_codec_gpio_if_t s_amp_gpio = { .setup = amp_gpio_setup, .set = amp_gpio_set, .get = amp_gpio_get };

esp_err_t voice_board_init(void) {
    if (s_inited) return ESP_OK;
    void *bus = sticks3_i2c_bus();
    if (!bus) { ESP_LOGE(TAG, "internal I2C bus not up (call sticks3_lcd_init first)"); return ESP_FAIL; }

    // Full-duplex I2S, enabled before the codec is configured so MCLK is running
    // for the ES8311's I2C register writes (see file header). Left enabled; the
    // esp_codec_dev data interface reference-counts enable/disable on open/close.
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    esp_err_t err = i2s_new_channel(&cc, &s_tx, &s_rx);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err)); return err; }
    const i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_MCLK,
            .bclk = PIN_BCLK,
            .ws = PIN_WS,
            .dout = PIN_DOUT,
            .din = PIN_DIN,
        },
    };
    if ((err = i2s_channel_init_std_mode(s_tx, &sc)) != ESP_OK) { ESP_LOGE(TAG, "i2s tx std: %s", esp_err_to_name(err)); return err; }
    if ((err = i2s_channel_init_std_mode(s_rx, &sc)) != ESP_OK) { ESP_LOGE(TAG, "i2s rx std: %s", esp_err_to_name(err)); return err; }
    // NOTE: do NOT enable I2S here. On this board, a running MCLK corrupts I2C to
    // the ES8311 (a raw register read works with I2S off and fails with it on), so
    // the codec is configured with the clocks idle; esp_codec_dev enables I2S on open.

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = s_tx };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    // esp_codec_dev takes the 8-bit I2C address and shifts it right internally, so pass
    // ES8311_CODEC_DEFAULT_ADDR (0x30), NOT the 7-bit 0x18 (which it would turn into 0x0C).
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl || !data) { ESP_LOGE(TAG, "codec interfaces failed"); return ESP_FAIL; }

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl,
        .gpio_if = &s_amp_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = 3,                 // PMIC PYG3 (routed to the amp by s_amp_gpio)
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    if (!codec) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }

    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data };
    esp_codec_dev_cfg_t in_cfg  = { .dev_type = ESP_CODEC_DEV_TYPE_IN,  .codec_if = codec, .data_if = data };
    s_spk = esp_codec_dev_new(&out_cfg);
    s_mic = esp_codec_dev_new(&in_cfg);
    if (!s_spk || !s_mic) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    s_inited = true;
    ESP_LOGI(TAG, "ES8311 voice board up (mic %d Hz)", VOICE_MIC_RATE);
    return ESP_OK;
}

esp_err_t voice_board_mic_start(void) {
    if (!s_inited && voice_board_init() != ESP_OK) return ESP_FAIL;
    if (s_mic_open) return ESP_OK;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = VOICE_MIC_RATE,
    };
    int r = esp_codec_dev_open(s_mic, &fs);
    if (r != 0) { ESP_LOGE(TAG, "mic open: %d", r); return ESP_FAIL; }
    esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);
    s_mic_open = true;
    return ESP_OK;
}

void voice_board_mic_stop(void) {
    if (s_mic_open) { esp_codec_dev_close(s_mic); s_mic_open = false; }
}

size_t voice_board_mic_read(int16_t *pcm, size_t frames, int *peak) {
    if (!s_mic_open || !pcm) { if (peak) *peak = 0; return 0; }
    int r = esp_codec_dev_read(s_mic, pcm, (int)(frames * sizeof(int16_t)));
    if (r != 0) { if (peak) *peak = 0; return 0; }
    int pk = 0;
    for (size_t i = 0; i < frames; i++) { int a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > pk) pk = a; }
    if (peak) *peak = pk;
    return frames;
}

// Speaker: minimal for now. The default reply is text in the Muse app, so nothing
// is played; the amp is wired so a future TTS path can feed voice_player.
esp_err_t voice_board_speaker_write(const int32_t *frames, size_t count) {
    (void)frames; (void)count;
    return ESP_OK;
}
void voice_board_amp(bool on) { sticks3_spk_amp(on); }
void voice_board_set_volume(int percent) { if (s_spk) esp_codec_dev_set_out_vol(s_spk, percent); }

bool voice_board_muted(void) { return false; }          // no hardware mute switch on the stick
bool voice_board_echo_cancelled(void) { return false; } // no echo canceller; mic closes while playing
int voice_board_dial_steps(void) { return 0; }          // no volume dial
