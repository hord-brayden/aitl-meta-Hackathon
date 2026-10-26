// AITL hardware probe for 2.8" capacitive CYD: identify touch IC, display IC, SD, LED.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
static void scan(int sda, int scl) {
  Wire.begin(sda, scl, 400000);
  Serial.printf("I2C scan SDA=%d SCL=%d:", sda, scl);
  int n=0;
  for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; } }
  if(!n) Serial.print(" (none)");
  Serial.println();
  // Identify common capacitive controllers
  for (uint8_t a : {0x15, 0x5D, 0x14, 0x38}) {
    Wire.beginTransmission(a); if (Wire.endTransmission()!=0) continue;
    if (a==0x15) { Wire.beginTransmission(a); Wire.write(0xA7); Wire.endTransmission(false); Wire.requestFrom(a,(uint8_t)3);
      uint8_t id=Wire.read(), proj=Wire.read(), fw=Wire.read(); Serial.printf("  CST8xx @0x15: chipID=0x%02X proj=0x%02X fw=0x%02X (B4=816S B5=816T B6=816D B7=820)\n",id,proj,fw); }
    if (a==0x5D||a==0x14) { Wire.beginTransmission(a); Wire.write(0x81); Wire.write(0x40); Wire.endTransmission(false); Wire.requestFrom(a,(uint8_t)4);
      char pid[5]={0}; for(int i=0;i<4;i++) pid[i]=Wire.read(); Serial.printf("  GT9xx @0x%02X: product=%s\n",a,pid); }
    if (a==0x38) { Wire.beginTransmission(a); Wire.write(0xA3); Wire.endTransmission(false); Wire.requestFrom(a,(uint8_t)1); uint8_t cid=Wire.read();
      Wire.beginTransmission(a); Wire.write(0xA8); Wire.endTransmission(false); Wire.requestFrom(a,(uint8_t)1); uint8_t vid=Wire.read();
      Serial.printf("  FT6x36 @0x38: chipID=0x%02X vendor=0x%02X\n",cid,vid); }
  }
  Wire.end();
}
static SPIClass hspi(HSPI);
static void tftRead(uint8_t cmd, int n, const char* label) {
  digitalWrite(15, LOW); digitalWrite(2, LOW); hspi.transfer(cmd); digitalWrite(2, HIGH);
  Serial.printf("  %s (0x%02X):", label, cmd);
  for (int i=0;i<n;i++) Serial.printf(" %02X", hspi.transfer(0x00));
  digitalWrite(15, HIGH); Serial.println();
}
// Resistive XPT2046 on 25/32/39/33 (IRQ 36), bit-banged
static uint16_t xptRead(uint8_t cmd) {
  digitalWrite(33, LOW);
  for (int i = 7; i >= 0; i--) { digitalWrite(32, (cmd >> i) & 1); digitalWrite(25, HIGH); delayMicroseconds(2); digitalWrite(25, LOW); delayMicroseconds(2); }
  uint16_t v = 0; for (int i = 0; i < 16; i++) { digitalWrite(25, HIGH); delayMicroseconds(2); v = (v << 1) | digitalRead(39); digitalWrite(25, LOW); delayMicroseconds(2); }
  digitalWrite(33, HIGH); return (v >> 4) & 0xFFF;
}
static void xptProbe() {
  pinMode(33, OUTPUT); digitalWrite(33, HIGH); pinMode(25, OUTPUT); digitalWrite(25, LOW); pinMode(32, OUTPUT); pinMode(39, INPUT); pinMode(36, INPUT);
  Serial.printf("XPT2046 probe: IRQ36=%d  X=%u Y=%u Z1=%u Z2=%u (untouched: IRQ high, Z1 small)\n", digitalRead(36), xptRead(0xD1), xptRead(0x91), xptRead(0xB1), xptRead(0xC1));
}
void setup() {
  Serial.begin(115200); delay(800);
  Serial.println("\n=== AITL PROBE ===");
  xptProbe();
  pinMode(25, OUTPUT); digitalWrite(25, LOW); delay(20); digitalWrite(25, HIGH); delay(120);
  scan(33, 32); scan(21, 22); scan(27, 22);
  // Display ID reads on HSPI (SCLK14 MISO12 MOSI13), CS15, DC2
  pinMode(15, OUTPUT); digitalWrite(15, HIGH); pinMode(2, OUTPUT); digitalWrite(2, HIGH);
  hspi.begin(14, 12, 13, 15);
  hspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  // soft reset + sleep out
  digitalWrite(15, LOW); digitalWrite(2, LOW); hspi.transfer(0x01); digitalWrite(15, HIGH); delay(150);
  digitalWrite(15, LOW); digitalWrite(2, LOW); hspi.transfer(0x11); digitalWrite(15, HIGH); delay(150);
  Serial.println("Display ID reads (dummy byte first):");
  tftRead(0x04, 4, "RDDID  ST7789=85 85 52 / ILI9341=00 00 00");
  tftRead(0xD3, 4, "RDID4  ILI9341=00 00 93 41 / ST7796=00 00 77 96");
  tftRead(0xDA, 2, "RDID1"); tftRead(0xDB, 2, "RDID2"); tftRead(0xDC, 2, "RDID3");
  tftRead(0x0A, 2, "RDDPM"); tftRead(0x0C, 2, "RDDCOLMOD");
  hspi.endTransaction();
  // Backlight + RGB LED
  pinMode(27, OUTPUT); pinMode(4, OUTPUT); pinMode(16, OUTPUT); pinMode(17, OUTPUT);
  digitalWrite(4,HIGH); digitalWrite(16,HIGH); digitalWrite(17,HIGH);
  for (int i=0;i<2;i++){ digitalWrite(27,LOW); digitalWrite(17,LOW); delay(250); digitalWrite(27,HIGH); digitalWrite(17,HIGH); delay(250);} 
  Serial.println("Backlight(27) + blue LED(17) blinked");
  // SD on VSPI
  SPIClass* vspi = new SPIClass(VSPI); vspi->begin(18,19,23,5);
  if (SD.begin(5, *vspi, 4000000)) { Serial.printf("SD: type=%d size=%lluMB\n", SD.cardType(), SD.cardSize()/(1024ULL*1024ULL)); }
  else Serial.println("SD: no card / init failed");
  Serial.printf("Heap free=%u PSRAM=%u\n", ESP.getFreeHeap(), ESP.getPsramSize());
  pinMode(21, INPUT);
}
void loop() {
  static uint32_t t=0; if(millis()-t>1000){t=millis(); Serial.printf("IRQ36=%d XPT X=%u Y=%u Z1=%u | INT21=%d\n", digitalRead(36), xptRead(0xD1), xptRead(0x91), xptRead(0xB1), digitalRead(21));}
}
