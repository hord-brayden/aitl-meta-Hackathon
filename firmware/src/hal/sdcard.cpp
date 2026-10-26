#include "sdcard.h"
#include <SPI.h>
#include <SD.h>
#include "../config.h"

static SPIClass s_vspi(VSPI);
static bool s_ready = false;
static uint32_t s_lastProbe = 0;

bool sdBegin() {
  if (s_ready) return true;
  static bool spiInit = false;
  if (!spiInit) { s_vspi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS); spiInit = true; }
  s_ready = SD.begin(PIN_SD_CS, s_vspi, 20000000);
  if (!s_ready) SD.end();
  else { if (!SD.exists("/aitl")) SD.mkdir("/aitl"); if (!SD.exists("/aitl/chats")) SD.mkdir("/aitl/chats"); }
  return s_ready;
}
bool sdReady() { return s_ready; }
void sdTick() {
  if (s_ready || millis() - s_lastProbe < 15000) return;
  s_lastProbe = millis();
  sdBegin();
}
String sdInfo() {
  if (!s_ready) return "no card";
  uint64_t total = SD.totalBytes() / (1024ULL * 1024ULL), used = SD.usedBytes() / (1024ULL * 1024ULL);
  const char* t = SD.cardType() == CARD_SDHC ? "SDHC" : SD.cardType() == CARD_SD ? "SD" : SD.cardType() == CARD_MMC ? "MMC" : "?";
  char buf[64]; snprintf(buf, sizeof(buf), "%s %lluMB, %lluMB used", t, (unsigned long long)total, (unsigned long long)used);
  return String(buf);
}
bool sdAppendLine(const char* path, const String& line) {
  if (!s_ready) return false;
  File f = SD.open(path, FILE_APPEND); if (!f) { s_ready = false; return false; }
  f.println(line); f.close(); return true;
}
bool sdWriteFile(const char* path, const String& data) {
  if (!s_ready) return false;
  File f = SD.open(path, FILE_WRITE); if (!f) { s_ready = false; return false; }
  f.print(data); f.close(); return true;
}
String sdReadFile(const char* path, size_t maxLen) {
  if (!s_ready) return "";
  File f = SD.open(path, FILE_READ); if (!f) return "";
  String s; while (f.available() && s.length() < maxLen) s += (char)f.read();
  f.close(); return s;
}
