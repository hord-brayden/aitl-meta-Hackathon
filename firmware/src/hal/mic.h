#pragma once
#include <Arduino.h>
// I2S MEMS microphone (INMP441/SPH0645 on the P3 header). Captures 16 kHz mono, encodes IMA ADPCM (4:1),
// hands 128-byte frames to a callback. Also exposes a live level for the mic test screen and a simple VAD.
namespace mic {
  typedef void (*FrameCb)(const uint8_t* adpcm, size_t len, uint16_t seq);
  bool begin();                 // install the I2S driver; false if it fails (no mic is NOT detectable electrically)
  void end();
  bool started();
  void startCapture(FrameCb cb);
  void stopCapture();
  bool capturing();
  void tick();                  // call often; reads DMA buffers, encodes, emits frames
  uint8_t level();              // 0..100 recent RMS level (works while capturing or monitoring)
  void startMonitor();          // level only, no frames (mic test screen)
  void stopMonitor();
  uint32_t silenceMs();         // ms since the level last exceeded the speech threshold
  bool heardSpeech();           // true once speech was detected in this capture
}
