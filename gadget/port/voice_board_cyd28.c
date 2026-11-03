// Voice board for the LCD Wiki 2.8" ESP32-S3 (E32C28P): push-to-talk mic capture
// through the on-board ES8311 codec, for CONFIG_HOMEHUB_VOICE. Implements voice_board.h.
//
// The ES8311 sits on the SAME I2C bus as the FT6336 touch (IO15 SCL / IO16 SDA), so it
// takes the shared handle from cyd28_i2c_bus() rather than creating its own master.
// I2S (from the vendor IO table): MCLK G4, BCLK G5, WS/LRCK G7, DOUT(DAC->speaker) G6,
// DIN(ADC<-mic) G8. The speaker amp (FM8002E) is enabled by driving GPIO1 LOW (active
// low). Mirrors voice_board_sticks3.c; no PMIC on this board, so the amp is a plain GPIO.
#include "voice_board.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cyd28_lcd.h"   // cyd28_i2c_bus()

static const char *TAG = "voice.cyd28";

#define PIN_MCLK  GPIO_NUM_4
#define PIN_BCLK  GPIO_NUM_5
#define PIN_WS    GPIO_NUM_7
#define PIN_DOUT  GPIO_NUM_6    // ESP -> codec DAC (speaker)
#define PIN_DIN   GPIO_NUM_8    // codec ADC -> ESP (microphone)
#define PIN_AMP   GPIO_NUM_1    // FM8002E speaker-amp enable, ACTIVE LOW (low = on)
#define MIC_GAIN_DB 30.0f

static i2s_chan_handle_t s_tx, s_rx;
static esp_codec_dev_handle_t s_mic, s_spk;
static bool s_inited;
static bool s_mic_open;
static int s_peak_max;   // loudest sample seen since the last read (for the on-screen "sent (mic N)" readout)

// Amp enable (GPIO1, active-low): the codec's gpio_if calls set(pa_pin, enable);
// enable=true means amp on -> drive the pin LOW.
static void amp_set(bool on) { gpio_set_level(PIN_AMP, on ? 0 : 1); }
static int amp_gpio_setup(int16_t gpio, audio_gpio_dir_t dir, audio_gpio_mode_t mode) { (void)gpio; (void)dir; (void)mode; return ESP_CODEC_DEV_OK; }
static int amp_gpio_set(int16_t gpio, bool high) { (void)gpio; amp_set(high); return ESP_CODEC_DEV_OK; }
static bool amp_gpio_get(int16_t gpio) { (void)gpio; return false; }
static const audio_codec_gpio_if_t s_amp_gpio = { .setup = amp_gpio_setup, .set = amp_gpio_set, .get = amp_gpio_get };

esp_err_t voice_board_init(void) {
    if (s_inited) return ESP_OK;
    void *bus = cyd28_i2c_bus();
    if (!bus) { ESP_LOGE(TAG, "shared I2C bus not up (cyd28_i2c_bus)"); return ESP_FAIL; }

    // Amp enable: output, start OFF (high).
    gpio_config_t ag = { .pin_bit_mask = 1ULL << PIN_AMP, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&ag); gpio_set_level(PIN_AMP, 1);

    // Full-duplex I2S. Not enabled here: a running MCLK can corrupt I2C to the ES8311 on
    // this shared bus (seen on the stick), so the codec is configured with clocks idle;
    // esp_codec_dev enables the channel on open (mic capture).
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    esp_err_t err = i2s_new_channel(&cc, &s_tx, &s_rx);
    if (err != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err)); return err; }
    const i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VOICE_MIC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN },
    };
    if ((err = i2s_channel_init_std_mode(s_tx, &sc)) != ESP_OK) { ESP_LOGE(TAG, "i2s tx std: %s", esp_err_to_name(err)); return err; }
    if ((err = i2s_channel_init_std_mode(s_rx, &sc)) != ESP_OK) { ESP_LOGE(TAG, "i2s rx std: %s", esp_err_to_name(err)); return err; }

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .rx_handle = s_rx, .tx_handle = s_tx };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    // esp_codec_dev takes the 8-bit I2C address and shifts it right internally, so pass
    // ES8311_CODEC_DEFAULT_ADDR (0x30), NOT the 7-bit 0x18.
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bus };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl || !data) { ESP_LOGE(TAG, "codec interfaces failed"); return ESP_FAIL; }

    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl,
        .gpio_if = &s_amp_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = PIN_AMP,           // routed to GPIO1 by s_amp_gpio (active-low)
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
    if (pk > s_peak_max) s_peak_max = pk;   // track session loudness for the on-screen readout
    return frames;
}

// TEMP diagnostic: the loudest mic sample since the last call (and reset). musecorder shows
// it on the "sent" screen so we can tell a dead mic (0) from a quiet one without serial.
int voice_board_cyd28_peak(void) { int p = s_peak_max; s_peak_max = 0; return p; }

// Speaker: minimal for now (text replies). The amp is wired so a future TTS path can feed it.
esp_err_t voice_board_speaker_write(const int32_t *frames, size_t count) {
    (void)frames; (void)count;
    return ESP_OK;
}
void voice_board_amp(bool on) { amp_set(on); }
void voice_board_set_volume(int percent) { if (s_spk) esp_codec_dev_set_out_vol(s_spk, percent); }

bool voice_board_muted(void) { return false; }
bool voice_board_echo_cancelled(void) { return false; }   // mic closes while playing
int voice_board_dial_steps(void) { return 0; }            // no volume dial
