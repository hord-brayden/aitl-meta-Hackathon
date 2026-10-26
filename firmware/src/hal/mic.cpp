#include "mic.h"
#include <driver/i2s.h>
#include "../config.h"
#include "../core/settings.h"

namespace mic {
static bool s_started = false, s_capturing = false, s_monitor = false, s_heard = false;
static FrameCb s_cb = nullptr;
static uint16_t s_seq = 0;
static uint8_t s_level = 0;
static uint32_t s_lastLoud = 0;
static int32_t s_raw[MIC_FRAME_SAMPLES];
static int16_t s_pcm[MIC_FRAME_SAMPLES];
static uint8_t s_out[MIC_FRAME_SAMPLES / 2];
// IMA ADPCM state
static int s_pred = 0, s_index = 0;
static const int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int16_t kStepTable[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};

static uint8_t encodeSample(int16_t sample) {
  int step = kStepTable[s_index];
  int diff = sample - s_pred;
  uint8_t code = 0;
  if (diff < 0) { code = 8; diff = -diff; }
  int tmp = step; int delta = 0;
  if (diff >= tmp) { code |= 4; diff -= tmp; delta += tmp; } tmp >>= 1;
  if (diff >= tmp) { code |= 2; diff -= tmp; delta += tmp; } tmp >>= 1;
  if (diff >= tmp) { code |= 1; delta += tmp; }
  delta += step >> 3;
  s_pred += (code & 8) ? -delta : delta;
  if (s_pred > 32767) s_pred = 32767; else if (s_pred < -32768) s_pred = -32768;
  s_index += kIndexTable[code & 7]; if (s_index < 0) s_index = 0; else if (s_index > 88) s_index = 88;
  return code;
}

bool begin() {
  if (s_started) return true;
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6; cfg.dma_buf_len = MIC_FRAME_SAMPLES;
  cfg.use_apll = false; cfg.tx_desc_auto_clear = false; cfg.fixed_mclk = 0;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {}; pins.bck_io_num = PIN_MIC_SCK; pins.ws_io_num = PIN_MIC_WS; pins.data_out_num = I2S_PIN_NO_CHANGE; pins.data_in_num = PIN_MIC_SD;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return false; }
  i2s_zero_dma_buffer(I2S_NUM_0);
  s_started = true; return true;
}
void end() { if (!s_started) return; stopCapture(); i2s_driver_uninstall(I2S_NUM_0); s_started = false; }
bool started() { return s_started; }
bool capturing() { return s_capturing; }
void startCapture(FrameCb cb) { if (!s_started && !begin()) return; s_cb = cb; s_seq = 0; s_pred = 0; s_index = 0; s_heard = false; s_lastLoud = millis(); s_capturing = true; i2s_zero_dma_buffer(I2S_NUM_0); }
void stopCapture() { s_capturing = false; s_cb = nullptr; }
void startMonitor() { if (!s_started) begin(); s_monitor = true; }
void stopMonitor() { s_monitor = false; }
uint8_t level() { return s_level; }
uint32_t silenceMs() { return millis() - s_lastLoud; }
bool heardSpeech() { return s_heard; }

void tick() {
  if (!s_started || (!s_capturing && !s_monitor)) return;
  size_t got = 0;
  if (i2s_read(I2S_NUM_0, s_raw, sizeof(s_raw), &got, 0) != ESP_OK || got < sizeof(s_raw)) return;
  uint8_t shift = 16 - constrain((int)g_settings.micGain, 1, 8);   // 24-bit sample in the top bits of 32
  uint64_t acc = 0;
  for (int i = 0; i < MIC_FRAME_SAMPLES; i++) {
    int32_t v = s_raw[i] >> shift;
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    s_pcm[i] = (int16_t)v; acc += (uint64_t)((int32_t)v * v);
  }
  uint32_t rms = sqrtf((float)(acc / MIC_FRAME_SAMPLES));
  uint8_t lv = (uint8_t)constrain((int)(rms / 250), 0, 100);    // ~25000 RMS == full scale
  s_level = (s_level * 3 + lv) / 4;
  if (lv > 6) { s_lastLoud = millis(); if (lv > 10) s_heard = true; }
  if (!s_capturing || !s_cb) return;
  for (int i = 0; i < MIC_FRAME_SAMPLES; i += 2) s_out[i / 2] = (encodeSample(s_pcm[i]) & 0x0F) | (encodeSample(s_pcm[i + 1]) << 4);
  s_cb(s_out, sizeof(s_out), s_seq++);
}
}  // namespace mic
