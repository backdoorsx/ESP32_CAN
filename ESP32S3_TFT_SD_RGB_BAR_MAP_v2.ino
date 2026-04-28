/*
  https://www.waveshare.com/product/esp32-s3-lcd-1.47.htm

  Board: ESP32S3 Dev Module
  Flash Size: 16MB(128Mb)
  Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
  PSRAM: OPI PSRAM

  Board: ESP32C6 Dev Module
  Flash Size: 4MB(32Mb)
  Partition Scheme: No OTA (2MB APP/2MB FATFS)
=
  https://github.com/processing/processing4/tags
  https://www.dafont.com
  https://tomeko.net/online_tools/file_to_hex.php?lang=en
*/

#include <SPI.h>
#include <TFT_eSPI.h>       // Hardware-specific library
#include <Adafruit_NeoPixel.h>

#include "FS.h"
#include "SD_MMC.h"

#include "RadioSpace.h"

#include <WiFi.h>
#include <esp_now.h>

#define BOOT_BUTTON 0  // GPIO0

//----------------------------------------------------------------
// ESP NOW
//----------------------------------------------------------------
typedef struct __attribute__((packed)) {
  int32_t a;
  int32_t b;
} EspNowPacket;
uint8_t peerMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}; // ESP PRIMAC
volatile EspNowPacket lastPkt;
volatile bool newData = false;
volatile EspNowPacket rxPacket;
volatile bool rxNewData = false;

//----------------------------------------------------------------
// BUFFER
//----------------------------------------------------------------
#define BUFFER_SIZE 1024

__attribute__((packed)) typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t ts;
} LogFrame;

LogFrame buffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;

//----------------------------------------------------------------
// SD
//----------------------------------------------------------------
#define SD_SCLK 14
#define SD_MOSI 15
#define SD_MISO 16
#define SD_SD1 18
#define SD_SD2 17
#define SD_CS 21
bool sd_ok = false;
String msg = "";

//----------------------------------------------------------------
// WS-LED SETTINGS 
//----------------------------------------------------------------
#define NUMPIXELS 1
#define LED 38
Adafruit_NeoPixel pixels(NUMPIXELS, LED, NEO_RGB + NEO_KHZ800);

//----------------------------------------------------------------
// TFT 
//----------------------------------------------------------------
#define TFT_BL2 46
TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
int x_px = 0, y_px = 0;
int color_px = TFT_WHITE;

// Cursor
const int cwidth = 10;
const int ceight = 16;
int maximumX = 20;
int cnewY = 15;
// Bar
int cx = 88;
int cy = 280;
int r = 240;
int yTop[321];  // pre každý x
bool circleReady = false;
int cnewX_scaled = 0;
int x_scaled = 0;
int prev_barX = 0;
int barX = 0;
int targetX = 0;
int cnewX = 0;
int cprevX = -1;

uint16_t DARK_RED = 0;
uint16_t DARK_BLUE = 0;
uint16_t DARK_BLUE_0 = 0;
uint16_t DARK_BLUE_1 = 0;

//----------------------------------------------------------------
// FONTS
//----------------------------------------------------------------
const uint8_t* fonts[3][3] = {
    { radioSpaceFont22, radioSpaceFont36, radioSpaceFont80 },
    { radioSpaceFont22, radioSpaceFont36, radioSpace3dFont80 },
    { radioSpaceFont22, radioSpaceFont36, radioSpaceFont80 }
};
int current_font = 0;

//----------------------------------------------------------------
// THEMES
//----------------------------------------------------------------
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
// 4 odtiene pre každú farbu (od tmavej po svetlú)
const uint16_t colors[3][4] = {
  { color565(0x00, 0x00, 0x22), color565(0x00, 0x00, 0x66), color565(0x00, 0x00, 0xAA), color565(0x00, 0x00, 0xFF) }, // modrá
  { color565(0x00, 0x22, 0x00), color565(0x00, 0x66, 0x00), color565(0x00, 0xAA, 0x00), color565(0x00, 0xFF, 0x00) }, // zelená
  { color565(0x22, 0x00, 0x00), color565(0x66, 0x00, 0x00), color565(0xAA, 0x00, 0x00), color565(0xFF, 0x00, 0x00) }  // červená
};

uint8_t current_color = 0; // 0=modrá, 1=zelená, 2=červená
uint8_t shade_index = 0;   // 0..3 odtieň

//----------------------------------------------------------------
// ESP_NOW - Callback
//----------------------------------------------------------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {

  // MAC odosielateľa
  const uint8_t *mac = info->src_addr;

  if (len != sizeof(EspNowPacket)) {
    Serial.println("[-] Wrong packet size");
    return;
  }

  ///*
  EspNowPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  Serial.printf("[RX] from %02X:%02X:%02X:%02X:%02X:%02X -> a=%ld b=%ld\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5],
                pkt.a, pkt.b);
  //*/
  
  memcpy((void*)&rxPacket, data, sizeof(EspNowPacket));
  rxNewData = true;
}

//----------------------------------------------------------------
// SD LS
//----------------------------------------------------------------
void ls(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  fs::File root = fs.open(dirname);  // <-- change here
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  fs::File file = root.openNextFile();  // <-- and here
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        ls(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void append_file(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  fs::File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
}

void read_file(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);

  fs::File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
}

void write_buffer() {//csv
  if (!sd_ok) return;
  if (bufferIndex == 0) return;

  fs::File file = SD_MMC.open("/canex.csv", FILE_APPEND);
  if (!file) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(0, 200);
    tft.println(" LOG open failed");
    tft.setTextColor(TFT_BLUE);
    Serial.println("LOG open failed");
    return;
  }

  for (uint16_t i = 0; i < bufferIndex; i++) {
    LogFrame &f = buffer[i];

    // Rozlíšenie ID typu
    bool isExtended = (f.id > 0x7FF); // >11-bit → extended

    // CSV formát: timestamp, ID, extended_flag, DLC, data0..7
    file.printf("%lu,0x%08lX,%u,%u,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",
                f.ts,                    // čas v ms
                f.id & 0x1FFFFFFF,       // 29-bit maska
                isExtended ? 1 : 0,      // 1 = extended, 0 = standard
                f.dlc,
                f.data[0], f.data[1], f.data[2], f.data[3],
                f.data[4], f.data[5], f.data[6], f.data[7]);
    /*
    // Zápis CSV riadku
    //file.printf("%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
    file.printf("%lu,0x%03lX,%u,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",
                f.ts,
                f.id,
                f.dlc,
                f.data[0], f.data[1], f.data[2], f.data[3],
                f.data[4], f.data[5], f.data[6], f.data[7]);
    */
  }

  file.close();

  pixels.setPixelColor(0, pixels.Color(0, 111, 0));
  pixels.show();

  // reset buffer
  bufferIndex = 0;
}

uint16_t getColor(uint8_t colorType, uint8_t shade) {
    if(shade > 3) shade = 3;
    return colors[colorType][shade];
}

void set_unit(){
    // Nastavenie textu
    tft.loadFont(fonts[current_font][1]);
    //tft.setTextFont(1);
    tft.setTextColor(colors[current_color][3]);
    //tft.setTextSize(2); //4
    tft.setCursor(180, tft.height() - 38);  // (180, tft.height() - 40)
    tft.println("KPa");
}

void setup() {

  pinMode(TFT_BL2, OUTPUT);
  //digitalWrite(TFT_BL2, HIGH); // zapnutie LED LCD
  tft.init();
  tft.setRotation(3);   // nastavíme landscape
  delay(250);
  pixels.begin();
  pixels.clear();
  Serial.begin(115200);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  delay(500);
  pixels.setPixelColor(0, pixels.Color(111, 0, 0));
  pixels.show();

  // SD
  if(! SD_MMC.setPins(SD_SCLK, SD_MOSI, SD_MISO, SD_SD1, SD_SD2, SD_CS)){
    Serial.println("Pin change failed!");
  }
  else{
    if (!SD_MMC.begin()) {
      Serial.println("Card Mount Failed");
    }
    else{
      uint8_t cardType = SD_MMC.cardType();
      if (cardType == CARD_NONE) {
        Serial.println("No SD_MMC card attached");
      }
      else{
        Serial.print("SD_MMC Card Type: ");
        if (cardType == CARD_MMC) {
          Serial.println("MMC");
        } else if (cardType == CARD_SD) {
          Serial.println("SDSC");
        } else if (cardType == CARD_SDHC) {
          Serial.println("SDHC");
        } else {
          Serial.println("UNKNOWN");
        }
        uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
        Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
        ls(SD_MMC, "/", 0);
        Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
        Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
        sd_ok = true;
      }
    }
  }

  if (sd_ok){
    pixels.setPixelColor(0, pixels.Color(0, 0, 111));
    pixels.show();
  }

  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  if (!circleReady) {
    for (int x = 0; x <= 320; x++) {
      int dx = x - cx;

      if (abs(dx) <= r) {
        int dy = sqrt(r * r - dx * dx);
        yTop[x] = cy - dy;
      } else {
        yTop[x] = 20;
      }
    }

    circleReady = true;

    set_unit();
  }
  
  espnow_init_receiver();

  digitalWrite(TFT_BL2, HIGH); // zapnutie LED LCD
  delay(3);
}

int scaleX(int x) {
    int min1 = 20;
    int max1 = 260;
    int min2 = 20;
    int max2 = 300;
    
    int scaled = (x - min1) * (max2 - min2) / (max1 - min1) + min2;
    return scaled;
}

uint8_t scaleBlue(int x) {
    int minX = 0;
    int maxX = 320;

    int minB = 0x66;
    int maxB = 0xFF;

    int b = (x - minX) * (maxB - minB) / (maxX - minX) + minB;

    return (uint8_t)b;
}

uint16_t scaleColorDynamic(int x, int target, uint8_t colorType) {
    if (target <= 0) target = 1; // bezpečnosť proti deleniu nulou
    int val = 0x66 + (0xFF - 0x66) * x / target; // škálovanie 0x66 -> 0xFF
    if (val > 0xFF) val = 0xFF;
    if (val < 0x66) val = 0x66;

    switch(colorType){
        case 0: return color565(0x00, 0x00, val); // modrá
        case 1: return color565(0x00, val, 0x00); // zelená
        case 2: return color565(val, 0x00, 0x00); // červená
        default: return color565(0x00, 0x00, val); // fallback modrá
    }
}

void drawTriangle(int x, int y) {
    int leftX = x - cwidth / 2;
    int leftY = y - ceight;
    int rightX = x + cwidth / 2;
    int rightY = y - ceight;
    tft.fillTriangle(leftX, leftY, rightX, rightY, x, y, colors[current_color][3]);
}

void drawValueText(int x, int y, int value) {
    tft.loadFont(fonts[current_font][0]);
    tft.setTextColor(colors[current_color][2]);
    tft.setCursor(x-50, y-15);
    tft.printf("%d", value);
}

void erase_cursor() {
    tft.fillRect(0, 0, tft.width(), cnewY+1, TFT_BLACK);
}

void erase_value() {
    tft.fillRect(0, tft.height()/2, tft.width()/2+16, 70, TFT_BLACK);
}

void refresh(){
  set_unit();
  set_cursor();
}

void set_cursor(){
  maximumX = cnewX;
  cnewX_scaled = scaleX(cnewX);

  erase_cursor();
  drawTriangle(cnewX_scaled, cnewY);
  drawValueText(cnewX_scaled, cnewY, cnewX);
}

void show_statistics(){
  ;
}

unsigned long t0 = 0;
unsigned long t1 = 0;

int butt_counter = 0;

void loop() {

  if(millis() - t0 >= 100){
    if(cnewX != cprevX){
      //cnewX = random(40, 260); // <<<< SIM
      cnewX = rxPacket.a; 

      erase_value();
      tft.loadFont(fonts[current_font][2]);

      int offset_str = 166 - tft.textWidth(String(cnewX));
      tft.setCursor(offset_str, 100);

      tft.setTextColor(colors[current_color][3], TFT_BLACK);
      tft.print(cnewX);

      cprevX = cnewX;
    }

    t0 = millis();

    if(digitalRead(BOOT_BUTTON) == LOW){
      butt_counter++;
    }
    else{
      butt_counter = 0;
    }
  }

  if(butt_counter > 10){ // 2sec
    //change font type
    current_font++;
    if(current_font >= 3){
      current_font = 0;
    }
    butt_counter = 0;
    refresh();
    cprevX = -1;
    delay(500);
  }
  if(butt_counter > 2 && butt_counter < 4){ // 400-800ms
    // change color
    current_color++;
    if(current_color >= 3){
      current_color = 0;
    }
    refresh();
    cprevX = -1;
    delay(500);
  }

  //----- CURSOR
  if (cnewX > maximumX){
    set_cursor();
  }

  //----- TARGET
  targetX = scaleX(cnewX);

  //----- SMOOTH
  int step = max(1, abs(targetX - barX) / 8);

  if (barX < targetX) barX += step;
  else if (barX > targetX) barX -= step;

  //----- CLAMP
  if (abs(barX - targetX) < step) barX = targetX;

  //----- DRAW BAR
  for (int x = 0; x <= 320; x++) {

    uint16_t color;

    if (x <= barX) {
      color = scaleColorDynamic(x, barX, current_color);
    } else {
      color = colors[current_color][0];
    }

    int y_top = yTop[x];

    tft.drawLine(x, 21, x, y_top, color);
  }

  delay(1);
}

void espnow_init_receiver() {

  WiFi.mode(WIFI_STA); // povinné

  if (esp_now_init() != ESP_OK) {
    Serial.println("[-] ESP-NOW init FAILED");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // MAC pre info
  Serial.println(WiFi.macAddress()); //E8:F6:0A:92:51:D0
  uint8_t mac[6];
  WiFi.macAddress(mac);
  delay(100);
  Serial.println("[+] ESP-NOW RX ready");
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
}