#pragma once
#include <Arduino.h>
bool   sdBegin();                 // mount if a card is present; safe to call repeatedly
bool   sdReady();
void   sdTick();                  // periodic re-probe for card insertion
String sdInfo();                  // "32GB SDHC, 1.2GB used" or "no card"
bool   sdAppendLine(const char* path, const String& line);
bool   sdWriteFile(const char* path, const String& data);
String sdReadFile(const char* path, size_t maxLen = 4096);
