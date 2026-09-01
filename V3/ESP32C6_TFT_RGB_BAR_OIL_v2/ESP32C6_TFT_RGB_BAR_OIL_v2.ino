/*
  https://www.waveshare.com/product/esp32-c6-lcd-1.47.htm

  Board: ESP32C6 Dev Module
  Flash Size: 4MB(32Mb)
  Partition Scheme: No OTA (2MB APP/2MB FATFS)
=
  https://github.com/processing/processing4/tags
  https://www.dafont.com
  https://tomeko.net/online_tools/file_to_hex.php?lang=en
*/

#include <SPI.h>
//#include <TFT_eSPI.h>       // Hardware-specific library
#include <LovyanGFX.hpp>      // esp32c6 only
#include <Adafruit_NeoPixel.h>

#include "RadioSpace.h"
#include "img_motor.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BOOT_BUTTON 9

//----------------------------------------------------------------
// ESP NOW
//----------------------------------------------------------------
typedef struct __attribute__((packed)) {
  int32_t a;
  int32_t b;
} EspNowPacket;
uint8_t peerMAC[] = {0x10, 0x20, 0xBA, 0x31, 0x43, 0x94}; // ESP PRIMAC 10:20:BA:31:43:94
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
/*
#define SD_SCLK 14
#define SD_MOSI 15
#define SD_MISO 16
#define SD_SD1 18
#define SD_SD2 17
#define SD_CS 21
*/
bool sd_ok = false;
String msg = "";

//----------------------------------------------------------------
// WS-LED SETTINGS 
//----------------------------------------------------------------
#define NUMPIXELS 1
#define LED 8
Adafruit_NeoPixel pixels(NUMPIXELS, LED, NEO_RGB + NEO_KHZ800);

// ============================================================
// TFT (ST7789 172x320)
// ============================================================
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire = false;

      cfg.pin_sclk = 7;
      cfg.pin_mosi = 6;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 15;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();

      cfg.pin_cs   = 14;
      cfg.pin_rst  = 21;
      cfg.pin_busy = -1;

      cfg.panel_width  = 172;
      cfg.panel_height = 320;

      cfg.offset_x = 34;
      cfg.offset_y = 0;

      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};

LGFX tft;

//----------------------------------------------------------------
// TFT 
//----------------------------------------------------------------
#define DBG_FRAME false
#define TFT_BL2 22
//TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
int x_px = 0, y_px = 0;
int color_px = TFT_WHITE;

// Bar
#define OFFSET_Y 4
#define YTOP 320-40
int cx = 24; //70
int cy = 420; //360
int r = 320; //320
int yTop[YTOP+1];  // pre každý x
bool circleReady = false;
int cnewX_scaled = 0;
int x_scaled = 0;
int barX0 = 0;
int targetX0 = 0;
int barX1 = 0;
int targetX1 = 0;
int cnewX0 = 0;
int cprevX0 = -1;
int cnewX1 = 0;
int cprevX1 = -1;
bool redraw_bar0 = true;
bool redraw_bar1 = true;

uint16_t DARK_RED = 0;
uint16_t DARK_BLUE = 0;
uint16_t DARK_BLUE_0 = 0;
uint16_t DARK_BLUE_1 = 0;

//----------------------------------------------------------------
// FONTS
//----------------------------------------------------------------
const uint8_t* fonts_j[3][3] = {
    { radioSpaceFont22, radioSpaceFont36, radioSpaceFont52 },
    { radioSpaceFont22, radioSpaceFont36, radioSpaceFont52 },
    { radioSpaceFont22, radioSpaceFont36, radioSpaceFont52 }
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
  //const uint8_t *mac = info->src_addr;

  if (len != sizeof(EspNowPacket)) {
    //Serial.println("[-] Wrong packet size");
    return;
  }

  /*
  EspNowPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  Serial.printf("[RX] from %02X:%02X:%02X:%02X:%02X:%02X -> a=%ld b=%ld\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5],
                pkt.a, pkt.b);
  */
  
  memcpy((void*)&rxPacket, data, sizeof(EspNowPacket));
  rxNewData = true;
}


uint16_t getColor(uint8_t colorType, uint8_t shade) {
    if(shade > 3) shade = 3;
    return colors[colorType][shade];
}

void set_unit(){
    // Nastavenie textu °C"
    tft.loadFont(fonts_j[current_font][1]);
    //tft.setTextFont(1);
    tft.setTextColor(colors[current_color][3], TFT_BLACK);
    if(DBG_FRAME)tft.setTextColor(colors[current_color][3], TFT_RED);
    //tft.setTextSize(2); //4
    tft.setCursor(110, OFFSET_Y);  // (180, tft.height() - 40)
    tft.loadFont(radioSpaceFont72);
    tft.print("°");
    tft.loadFont(fonts_j[current_font][1]);
    tft.setCursor(122, OFFSET_Y);
    tft.print("C");

    // Nastavenie textu PSI
    tft.loadFont(fonts_j[current_font][1]);
    //tft.setTextFont(1);
    tft.setTextColor(colors[current_color][3], TFT_BLACK);
    if(DBG_FRAME)tft.setTextColor(colors[current_color][3], TFT_RED);
    //tft.setTextSize(2); //4
    tft.setCursor(110, tft.height() - 38);  // (180, tft.height() - 40)
    tft.print("PSI");
}

void setup() {

  pinMode(TFT_BL2, OUTPUT);
  //digitalWrite(TFT_BL2, HIGH); // zapnutie LED LCD
  tft.init();
  tft.setRotation(3);   // nastavíme landscape
  delay(150);
  pixels.begin();
  pixels.clear();
  Serial.begin(115200);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  delay(150);
  //pixels.setPixelColor(0, pixels.Color(0, 86, 155));
  pixels.setPixelColor(0, pixels.Color(111, 0, 0));
  pixels.show();

  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  for (int x = 0; x <= YTOP; x++) {
    int dx = x - cx;

    if (abs(dx) <= r) {
      int dy = sqrt(r * r - dx * dx);
      yTop[x] = cy - dy;
    } else {
      yTop[x] = 20;//20
    }

    if(yTop[x] > tft.height()-12)
      yTop[x] = tft.height() - 12;
  }

  set_unit();

  espnow_init_receiver();

  digitalWrite(TFT_BL2, HIGH); // zapnutie LED LCD
  delay(3);

  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 172, image_data);
  delay(3000);
  tft.fillScreen(TFT_BLACK);
}

void signal_temp(int temp){
  uint16_t color;

  if (temp < 80)
      color = TFT_BLUE;
  else if (temp < 115)
      color = TFT_GREEN;
  else if (temp < 125)
      color = TFT_ORANGE;
  else
      color = TFT_RED;
  tft.fillRect(tft.width()-32, 5+OFFSET_Y, tft.width(), 73, color);
}

void signal_pressure(int pressure){
  uint16_t color;

  if (pressure < 15)
      color = TFT_RED;
  else if (pressure < 20)
      color = TFT_ORANGE;
  else if (pressure <= 85)
      color = TFT_GREEN;
  else if (pressure < 95)
      color = TFT_ORANGE;
  else
      color = TFT_RED;
  tft.fillRect(tft.width()-32, tft.height()/2+OFFSET_Y+1, tft.width(), 74, color);
}

int scaleX(int x, int min, int max) {
    int min2 = 0;
    int max2 = YTOP;
    
    int scaled = (x - min) * (max2 - min2) / (max - min) + min2;
    return scaled;
}

uint8_t scaleBlue(int x) {
    int minX = 0;
    int maxX = YTOP;//320

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

void erase_value() {
    tft.fillRect(0, tft.height()/2+33, tft.width()/2-58, 55, TFT_BLACK);
    if(DBG_FRAME)tft.fillRect(0, tft.height()/2+33, tft.width()/2-58, 55, TFT_GREEN);

    tft.fillRect(0, 0, tft.width()/2-58, 55, TFT_BLACK);
    if(DBG_FRAME)tft.fillRect(0, 0, tft.width()/2-58, 55, TFT_GREEN);
}

void refresh(){
  set_unit();
}

void show_statistics(){
  ;
}

unsigned long t0 = 0;
unsigned long t1 = 0;

int butt_counter = 0;

void loop() {

  if(millis() - t0 >= 1000){
    cnewX0 = rxPacket.a;
    cnewX0 = int(cnewX0/100.0);
    //cnewX0 = random(0, 132); // <<<< SIM TEMP
    //cnewX0 = 132;
    cnewX1 = rxPacket.b;
    cnewX1 = int(cnewX1/10.0);
    //cnewX1 = random(0, 100); // <<<< SIM PRESURE
    //cnewX1 = 100;
    if(cnewX0 != cprevX0 || cnewX1 != cprevX1){
      erase_value();
      tft.loadFont(fonts_j[current_font][2]);

      int offset_str = 100 - tft.textWidth(String(cnewX0));
      tft.setCursor(offset_str, OFFSET_Y/2);
      tft.setTextColor(colors[current_color][3], TFT_BLACK);
      if(DBG_FRAME)tft.setTextColor(colors[current_color][3], TFT_RED);
      tft.print(cnewX0);

      offset_str = 100 - tft.textWidth(String(cnewX1));
      tft.setCursor(offset_str, 120);
      tft.setTextColor(colors[current_color][3], TFT_BLACK);
      if(DBG_FRAME)tft.setTextColor(colors[current_color][3], TFT_RED);
      tft.print(cnewX1);



      signal_temp(cnewX0);
      signal_pressure(cnewX1);

      cprevX0 = cnewX0;
      cprevX1 = cnewX1;
      redraw_bar0 = true;
      redraw_bar1 = true;
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
    cprevX0 = -1;
    cprevX1 = -1;
    delay(500);
  }
  if(butt_counter > 2 && butt_counter < 4){ // 400-800ms
    // change color
    current_color++;
    if(current_color >= 3){
      current_color = 0;
    }
    refresh();
    cprevX0 = -1;
    cprevX1 = -1;
    delay(500);
  }

  if(redraw_bar0){

    //----- TARGET
    targetX0 = scaleX(cnewX0, 0, 132);

    //----- SMOOTH
    int step = max(0, abs(targetX0 - barX0) / 8);

    if (barX0 < targetX0) barX0 += step;
    else if (barX0 > targetX0) barX0 -= step;

    //----- CLAMP
    if (abs(barX0 - targetX0) < step) barX0 = targetX0;

    //----- DRAW BAR
    for (int x = 0; x <= YTOP; x++) {

      uint16_t color;

      if (x <= barX0) {
        color = scaleColorDynamic(x, barX0, current_color);
      } else {
        color = colors[current_color][0];
      }

      int y_top = yTop[x];
      tft.drawLine(x, (tft.height()/2) - OFFSET_Y-2, x, (2 * (tft.height()/2) - y_top)-OFFSET_Y+1, color);
    }

    if(step == 0)
      redraw_bar0 = false;
  }

  if(redraw_bar1){

    //----- TARGET
    targetX1 = scaleX(cnewX1, 0, 100);

    //----- SMOOTH
    int step = max(0, abs(targetX1 - barX1) / 8);

    if (barX1 < targetX1) barX1 += step;
    else if (barX1 > targetX1) barX1 -= step;

    //----- CLAMP
    if (abs(barX1 - targetX1) < step) barX1 = targetX1;

    //----- DRAW BAR
    for (int x = 0; x <= YTOP; x++) {

      uint16_t color;

      if (x <= barX1) {
        color = scaleColorDynamic(x, barX1, current_color);
      } else {
        color = colors[current_color][0];
      }

      int y_top = yTop[x];
      tft.drawLine(x, tft.height()/2+OFFSET_Y, x, y_top, color);
    }

    if(step == 0)
      redraw_bar1 = false;
  }

  if(DBG_FRAME)tft.drawLine(0, tft.height()/2, tft.width(), tft.height()/2, TFT_RED);

  delay(25);
}

void espnow_init_receiver() {

  uint8_t defMAC[] = {0xAC, 0xEB, 0xE6, 0x1E, 0x1E, 0x68}; //AC:EB:E6:1E:1E:68
  WiFi.mode(WIFI_STA); // povinné
  esp_wifi_set_mac(WIFI_IF_STA, defMAC);


  if (esp_now_init() != ESP_OK) {
    Serial.println("[-] ESP-NOW init FAILED");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // MAC pre info
  Serial.println(WiFi.macAddress());
  uint8_t mac[6];
  WiFi.macAddress(mac);
  delay(100);
  Serial.println("[+] ESP-NOW RX ready");
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
}