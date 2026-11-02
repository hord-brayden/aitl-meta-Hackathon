// Voice board for the M5Stack StickS3: push-to-talk microphone capture through
// the on-board ES8311 codec, for CONFIG_HOMEHUB_VOICE. Implements voice_board.h.
//
// The ES8311 is a mono codec on the stick's internal I2C bus (0x18, shared with
// the PMIC and IMU). Audio runs over I2S: MCLK G18, BCLK G17, WS G15, the codec's
// ADC output (mic) on DIN G16, its DAC input (speaker) on DOUT G14. The codec
// driver ships with the SDK as the esp_codec_dev component.
//
// This is a microphone-first board: the push-to-talk reply comes back as text in
// the Muse app, so speaker playback is left minimal (the amp is wired, playback
// is a no-op until a TTS path feeds voice_player). All audio is 16 kHz mono.
#include "voice_board.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"

#include "sticks3_lcd.h"   // sticks3_i2c_bus(), sticks3_spk_amp()

static const char *TAG = "voice.sticks3";

#define ES8311_I2C_ADDR 0x18
#define PIN_MCLK  GPIO_NUM_18
#define PIN_BCLK  GPIO_NUM_17
#define PIN_WS    GPIO_NUM_15
#define PIN_DOUT  GPIO_NUM_14   // ESP -> codec DAC (speaker)
#define PIN_DIN   GPIO_NUM_16   // codec ADC -> ESP (microphone)
#define MIC_GAIN_DB 30.0f

static i2s_chan_handle_t s_rx;
static esp_codec_dev_handle_t s_dev;
static bool s_inited;
static bool s_mic_open;

esp_err_t voice_board_init(void) {
    if (s_inited) return ESP_OK;
    void *bus = sticks3_i2c_bus();
    if (!bus) { ESP_LOGE(TAG, "internal I2C bus not up (call sticks3_lcd_init first)"); return ESP_FAIL; }

    // I2S: receive only (microphone). The codec driver reconfigures and enables
    // the channel on open, so we just create and init it in standard mode here.
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&cc, NULL, &s_rx);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err)); return err; }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_MCLK,
            .bclk = PIN_BCLK,
            .ws = PIN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_rx, &sc);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s std init: %s", esp_err_to_name(err)); return err; }

    // ES8311 over the shared I2C bus, driven as an I2S slave (the ESP is master).
    audio_codec_i2c_cfg_t i2c_cfg = { .port = 0, .addr = ES8311_I2C_ADDR, .bus_handle = bus };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = NULL };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    if (!ctrl || !gpio || !data) { ESP_LOGE(TAG, "codec interfaces failed"); return ESP_FAIL; }

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl,
        .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,   // microphone only
        .pa_pin = -1,                                 // speaker amp is on the PMIC, handled separately
        .master_mode = false,                         // the ESP32 is the I2S master
        .use_mclk = true,
        .digital_mic = false,
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    if (!codec) { ESP_LOGE(TAG, "es8311_codec_new failed"); return ESP_FAIL; }

    esp_codec_dev_cfg_t dc = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = codec, .data_if = data };
    s_dev = esp_codec_dev_new(&dc);
    if (!s_dev) { ESP_LOGE(TAG, "esp_codec_dev_new failed"); return ESP_FAIL; }

    s_inited = true;
    ESP_LOGI(TAG, "ES8311 voice board up (mic %d Hz mono)", VOICE_MIC_RATE);
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
    int r = esp_codec_dev_open(s_dev, &fs);
    if (r != 0) { ESP_LOGE(TAG, "codec open: %d", r); return ESP_FAIL; }
    esp_codec_dev_set_in_gain(s_dev, MIC_GAIN_DB);
    s_mic_open = true;
    return ESP_OK;
}

void voice_board_mic_stop(void) {
    if (s_mic_open) { esp_codec_dev_close(s_dev); s_mic_open = false; }
}

size_t voice_board_mic_read(int16_t *pcm, size_t frames, int *peak) {
    if (!s_mic_open || !pcm) { if (peak) *peak = 0; return 0; }
    int r = esp_codec_dev_read(s_dev, pcm, (int)(frames * sizeof(int16_t)));
    if (r != 0) { if (peak) *peak = 0; return 0; }
    int pk = 0;
    for (size_t i = 0; i < frames; i++) { int a = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (a > pk) pk = a; }
    if (peak) *peak = pk;
    return frames;
}

// Speaker: minimal for now. The default reply is text in the Muse app, so nothing
// is played; the amp is still wired so a future TTS path can feed voice_player.
esp_err_t voice_board_speaker_write(const int32_t *frames, size_t count) {
    (void)frames; (void)count;
    return ESP_OK;
}
void voice_board_amp(bool on) { sticks3_spk_amp(on); }
void voice_board_set_volume(int percent) { (void)percent; }

bool voice_board_muted(void) { return false; }          // no hardware mute switch on the stick
bool voice_board_echo_cancelled(void) { return false; } // no echo canceller; mic closes while playing
int voice_board_dial_steps(void) { return 0; }          // no volume dial
