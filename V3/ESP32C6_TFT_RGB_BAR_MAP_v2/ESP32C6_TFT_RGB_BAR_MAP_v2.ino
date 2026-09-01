/*
  https://www.waveshare.com/product/esp32-c6-lcd-1.47.htm

  Board: ESP32C6 Dev Module
  Flash Size: 4MB(32Mb)
  Partition Scheme: No OTA (2MB APP/2MB FATFS)
  E4:B0:63:41:32:18

  https://github.com/processing/processing4/tags
  https://www.dafont.com
  https://tomeko.net/online_tools/file_to_hex.php?lang=en
*/

/*
PYTHON CONVERT .BMT TO .H ARRAY rgb565

from PIL import Image

INPUT_FILE = "turbo1.bmp"
OUTPUT_FILE = "img_turbo.h"
ARRAY_NAME = "image_data"

WIDTH = 320
HEIGHT = 172

# Načítanie obrázka
img = Image.open(INPUT_FILE).convert("RGB")

# Ak treba, zmeniť veľkosť
if img.size != (WIDTH, HEIGHT):
    img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)

def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

with open(OUTPUT_FILE, "w") as f:
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"const uint16_t {ARRAY_NAME}[{WIDTH*HEIGHT}] = {{\n")

    count = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = img.getpixel((x, y))
            rgb565 = rgb888_to_rgb565(r, g, b)

            f.write(f"0x{rgb565:04X},")

            count += 1
            if count % 12 == 0:
                f.write("\n")
            else:
                f.write(" ")

    f.write("\n};\n")

print(f"Hotovo. Súbor uložený ako {OUTPUT_FILE}")
*/

#include <SPI.h>
//#include <TFT_eSPI.h>       // Hardware-specific library
#include <LovyanGFX.hpp>      // esp32c6 only
#include <Adafruit_NeoPixel.h>

#include "RadioSpace.h"
#include "img_turbo.h"

#include <WiFi.h>
#include <esp_now.h>

#include <Preferences.h>

#define BOOT_BUTTON 9

//----------------------------------------------------------------
// ESP NOW
//----------------------------------------------------------------
typedef struct __attribute__((packed)) {
  int32_t a;
  int32_t b;
  int32_t c;
  int32_t d;
  int32_t e;
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
#define TFT_BL2 22
//TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
int x_px = 0, y_px = 0;
int color_px = TFT_WHITE;

// Cursor up
const int cwidth = 10;
const int ceight = 16;
int maximumX = 20;
int cnewY = 15;
// Cursor down
int down_maximumX = 0;
int down_cnewX = 0;
int down_cprevX = -1;
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
bool redraw_bar = true;

uint16_t DARK_RED = 0;
uint16_t DARK_BLUE = 0;
uint16_t DARK_BLUE_0 = 0;
uint16_t DARK_BLUE_1 = 0;

//----------------------------------------------------------------
// FONTS
//----------------------------------------------------------------
const uint8_t* fonts_j[3][3] = {
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

//----------------------------------------------------------------
// Preferences
//----------------------------------------------------------------
int odo_max = 0;
int speed_max = 0;
Preferences prefs;

void save_odometer(int val) {
  prefs.begin("data", false); // true = read-only, false = read/write
  prefs.putInt("odometer", val);
  prefs.end();

  Serial.printf("Saved odometer val: %d\n", val);
}

int load_odometer() {
  if (!prefs.begin("data", true)) {
    Serial.println("ERROR: Preferences.begin() failed!");
    return 0;
  }

  int val = prefs.getInt("odometer", 0);
  prefs.end();

  Serial.printf("Loaded odometer val: %d\n", val);

  return val;
}

void save_speed(int val) {
  prefs.begin("data", false); // true = read-only, false = read/write
  prefs.putInt("speed", val);
  prefs.end();

  Serial.printf("Saved speed val: %d\n", val);
}

int load_speed() {
  if (!prefs.begin("data", true)) {
    Serial.println("ERROR: Preferences.begin() failed!");
    return 0;
  }

  int val = prefs.getInt("speed", 0);
  prefs.end();

  Serial.printf("Loaded speed val: %d\n", val);

  return val;
}

//----------------------------------------------------------------
// setup
//----------------------------------------------------------------
uint16_t getColor(uint8_t colorType, uint8_t shade) {
    if(shade > 3) shade = 3;
    return colors[colorType][shade];
}

void set_unit(){
    // Nastavenie textu
    tft.loadFont(fonts_j[current_font][1]);
    //tft.setTextFont(1);
    tft.setTextColor(colors[current_color][3]);
    //tft.setTextSize(2); //4
    tft.setCursor(180, tft.height() - 66);  // (180, tft.height() - 40)
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

      if(yTop[x] > tft.height() - 30)
        yTop[x] = tft.height() - 30;
    }

    circleReady = true;

    set_unit();
  }
  
  espnow_init_receiver();

  digitalWrite(TFT_BL2, HIGH); // zapnutie LED LCD
  delay(3);

  odo_max = load_odometer();
  speed_max = load_speed();

  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 320, 172, image_data);
  delay(2000);
  tft.fillScreen(TFT_BLACK);
}

int scaleX(int x) {
    int min1 = 20;
    int max1 = 260;
    int min2 = 20;
    int max2 = 300;
    
    int scaled = (x - min1) * (max2 - min2) / (max1 - min1) + min2;
    return scaled;
}

int scaleX_down(int x) {
    int min1 = 0;
    int max1 = 200;
    int min2 = 60;
    int max2 = 250;
    
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
void drawTriangle_down(int x0, int y) {

    int x = scaleX_down(x0);
    int leftX = x - cwidth / 2;
    int leftY = y;
    int rightX = x + cwidth / 2;
    int rightY = y;

    tft.loadFont(fonts_j[current_font][0]);
    //tft.setTextColor(colors[current_color][3]);
    tft.setTextColor(TFT_RED);

    //if(down_maximumX != 0){
    int offset_str = tft.textWidth(String(x0)) + 10;
    tft.setCursor(x-offset_str, tft.height() - 20);
    tft.printf("%d", x0);
    //}

    tft.fillTriangle(leftX, leftY, rightX, rightY, x, y - ceight, colors[current_color][3]);

    tft.setCursor(x+7, y - ceight - 7);
    tft.printf("g/s");


}

void drawValueText(int x, int y, int value) {
    tft.loadFont(fonts_j[current_font][0]);
    //tft.setTextColor(colors[current_color][3]);
    tft.setTextColor(TFT_RED);
    tft.setCursor(x-50, y-15);
    tft.printf("%d", value);
}

void drawValueText_down(int value) {
    int offset_str = tft.textWidth(String(value)) + tft.textWidth(String(down_maximumX)) + 28;
    tft.loadFont(fonts_j[current_font][0]);
    //tft.setTextColor(colors[current_color][3], TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(scaleX_down(down_maximumX) - offset_str, tft.height() - 20);
    tft.printf("%d -", value);
}

void drawValueText_corner(int value) {
  if( value >= 0 && value <= 99 ){
    tft.loadFont(fonts_j[current_font][0]);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(282, tft.height() - 24);
    tft.printf("%d", value);

    int offset_str = tft.textWidth(String(value));
    tft.loadFont(radioSpaceFont52);
    tft.setCursor(282+offset_str, tft.height() - 24);
    tft.printf("°");
  }
}

void erase_cursor() {
    tft.fillRect(0, 0, tft.width(), cnewY+2, TFT_BLACK);
}
void erase_down_cursor() {
    tft.fillRect(0, tft.height()-22, tft.width(), tft.height(), TFT_BLACK);
}

void erase_value() {
    tft.fillRect(0, tft.height()/2-8, tft.width()/2+16, 70, TFT_BLACK);
    //tft.fillRect(176, tft.height() - 28, 100, 32, TFT_BLACK); //speed debug
}
void erase_down_value() {
    int offset_str = tft.textWidth(String(down_maximumX)) + 14;
    tft.fillRect(0, tft.height()-22, scaleX_down(down_maximumX)-offset_str, tft.height(), TFT_BLACK);
    //tft.fillRect(176, tft.height() - 28, 100, 32, TFT_BLACK); //speed debug
}

bool speedtimer_running = false;
bool speedtimer_show = true;
unsigned long last_speedtimer_blink = 0;
unsigned long from0to100 = 0;

void start_speedtimer(int map_value, int speed) {

  int chekc_speed_1 = 1010; // 101km/h = gps 100km/h
  if(map_value == 0)
    return;

  if(speed == 0){
    speedtimer_running = false;
  }

  // start timer
  if(speed > 0 && speedtimer_running == false){
    speedtimer_running = true;  // set tag
    from0to100 = millis();      // save curret time im ms
    Serial.println("start timer");
  }
  
  if(speed >= chekc_speed_1 && millis() - from0to100 < 10000){ // speed 101km/h timer less then 10s
    float seconds = (millis() - from0to100) / 1000.0;
    tft.fillScreen(TFT_BLACK);
    tft.loadFont(fonts_j[current_font][2]);
    tft.setCursor(76, 46);
    tft.setTextColor(colors[current_color][3], TFT_BLACK);
    tft.print(seconds);
    from0to100 = 0;
    delay(6000);  // freeze screen - hold value 6s
    tft.fillScreen(TFT_BLACK);
    refresh();
  }
  else if(speed == 0 || millis() - from0to100 < 10000 ){ // blinking
    if(millis() - last_speedtimer_blink >= 500){
      last_speedtimer_blink = millis();
      speedtimer_show = !speedtimer_show;
      Serial.println(millis() - from0to100);
    }

    if(speedtimer_show){
      tft.fillCircle(tft.width()/3-17, tft.height()/2-22, 8, TFT_RED);
    }
    else{
      tft.fillCircle(tft.width()/3-17, tft.height()/2-22, 8, TFT_BLACK);
    }
  }
  else{
    tft.fillCircle(tft.width()/3-17, tft.height()/2-22, 8, TFT_BLACK);
  }
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
  drawValueText(cnewX_scaled, cnewY-2, cnewX);
}

void set_cursor_down(){
  erase_down_cursor();
  drawTriangle_down(down_maximumX, tft.height()-2);
}

void show_statistics(){
  ;
}

void in_menu(){

  unsigned long tmenu = 0;
  unsigned long texit = millis();
  unsigned long tmenu_theme = millis();
  unsigned long tmenu_exit = millis();
  unsigned long tmenu_led = millis();
  int btn_c = 0;

  show_pref_menu(btn_c);
  delay(500);

  int loading_theme = 1;
  int loading_exit = 1;
  int loading_led = 1;

  while(true){
    
    if(millis() - tmenu >= 100){
      tmenu = millis();

      if(digitalRead(BOOT_BUTTON) == LOW){
        btn_c++;
        if(btn_c >= 7){
          btn_c = 0;
        }
        show_pref_menu(btn_c);
        texit = millis();
        delay(250);
      }
    }// if millis

    if(btn_c == 4 && millis() - tmenu_theme >= 900){
      loading_theme++;
      tmenu_theme = millis();
      Serial.println("jump to theme");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(tft.width()/2-68+(loading_theme*10), tft.height()/2+22);
      tft.print(".");
      if(loading_theme >= 5){
        loading_theme = 1;
      }
    }

    if(btn_c != 4 && millis() - texit >= 10000){
      tft.fillScreen(TFT_BLACK);
      break;
    }
    else if(btn_c == 5 && millis() - tmenu_led >= 900){
      loading_led++;
      tmenu_led = millis();
      Serial.println("jump to led");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(tft.width()/2-98+(loading_led*10), tft.height()/2+22);
      tft.print(".");
      if(loading_led >= 5){
        loading_led = 1;
      }
    }
    else if(btn_c == 6 && millis() - tmenu_exit >= 900){
      loading_exit++;
      tmenu_exit = millis();
      Serial.println("jump to exit");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(tft.width()/2-98+(loading_exit*10), tft.height()/2+22);
      tft.print(".");
      if(loading_exit >= 5){
        tft.fillScreen(TFT_BLACK);
        break;
      }
    }

    delay(10);
  }
}

void loading_bar() {
  Serial.println("in loading_bar()");

  int h = 30;
  int x = 20;
  int y = tft.height()/2-h/2;
  int w = tft.width()-x-x;
  int b = 5;
  // FRAME
  tft.fillScreen(TFT_BLACK);
  tft.loadFont(radioSpaceFont22);
  tft.setTextColor(TFT_GRAY);

  tft.setCursor(tft.width()/2-tft.textWidth(String("SHOW RECORDS"))/2, y-h);
  tft.print("SHOW RECORDS");

  tft.fillRect(x, y, w, h, TFT_GRAY);
  // FILL
  tft.fillRect(x+b, y+b, w-b-b, h-b-b, TFT_BLACK);
  
  int count_button = 0;
  while(true){
    if(digitalRead(BOOT_BUTTON) == LOW){
      count_button++;
    }
    else{
      count_button = 0;
      break;
    }
    Serial.println(count_button);
    
    int val = constrain(count_button, 0, 10); // Orezanie hodnoty na 0-10
    val = map(val, 0, 10, x+b, (w-b-b));      // preskalovat hodnotu od 0-10 do x+b-(w-b-b)
    tft.fillRect(x+b, y+b, val, h-b-b, TFT_WHITE);

    delay(50);

    if(count_button >= 10){
      in_menu();
      break;
    }
  }
  tft.fillScreen(TFT_BLACK);
  delay(100);
}

unsigned long t0 = 0;
unsigned long t1 = 0;
unsigned long t2 = 0;

void loop() {

  if(millis() - t0 >= 100){
    cnewX = rxPacket.a;
    //cnewX = random(40, 260); // <<<< SIM
    if(cnewX != cprevX){
      erase_value();
      tft.loadFont(fonts_j[current_font][2]);

      int offset_str = 166 - tft.textWidth(String(cnewX));
      tft.setCursor(offset_str, 76);

      tft.setTextColor(colors[current_color][3], TFT_BLACK);
      tft.print(cnewX);

      cprevX = cnewX;
      redraw_bar = true;
    }

    start_speedtimer(rxPacket.a, rxPacket.b); // MAP, speed 0-2020 (/10)

    if(rxPacket.e > odo_max){
      odo_max = rxPacket.e;
      save_odometer(odo_max);
    }

    if(rxPacket.b > speed_max){
      speed_max = rxPacket.b;
      save_speed(speed_max);
    }

    t0 = millis();

    if(digitalRead(BOOT_BUTTON) == LOW){
      loading_bar();
      refresh();
    }
  }
  /*
  if(butt_counter > 10){ // >= 5000ms
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
  if(butt_counter > 3 && butt_counter < 5){ // 1500-2500ms
    // change color
    current_color++;
    if(current_color >= 3){
      current_color = 0;
    }
    refresh();
    cprevX = -1;
    delay(500);
  }
  */

  //----- CURSOR UP
  if (cnewX > maximumX){
    set_cursor();
  }

  //----- CURSOR DOWN
  if(redraw_bar || millis() - t2 >= 250){
    down_cnewX = rxPacket.d; // MAF 0-200 g/s
    //down_cnewX = random(150, 180);
    
    if (down_cnewX > down_maximumX){
      down_maximumX = down_cnewX;
      set_cursor_down();
    }
    else if(redraw_bar && down_maximumX < 222){ //ak je pod cnewX printom
      set_cursor_down();
    }
    
    if(down_cnewX != down_cprevX){
      erase_down_value();
      drawValueText_down(down_cnewX);
      down_cprevX = down_cnewX;
    }
    
    drawValueText_corner(rxPacket.c); // IAT 0-200 celsius
    t2 = millis();
    redraw_bar = false;
    
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

  //////////////
  /*
  int x = 0;

  while (x <= 320) {

    int x_end = min(x + 4, 320); // blok po 4 pixeloch

    uint16_t color;

    if (x_end <= barX) {
      color = scaleColorDynamic(x_end, barX, current_color);
    } else {
      color = colors[current_color][0];
    }

    tft.fillRect(x, 21, (x_end - x + 1), yTop[x], color);

    x = x_end + 1;
  }*/
  //////////////

  delay(1);
}

void show_pref_menu(int scroll){
  /*
  * ODO       (km)  :
  * MAP MAX   (kpi) :
  * MAF MAX   (g/s) :
  * IAT MAX   (°C)  :
  * SPEED MAX (km/h):
  *
  * > THEME MENU
  * > LED MENU
  * > EXIT
  */

  // ----- SHOW TABLE
  tft.fillScreen(TFT_BLACK);
  int pref_odo = load_odometer();
  int pref_speed = load_speed(); //1010

  tft.loadFont(radioSpaceFont22);

  int x_label = 10;
  int x_right = tft.width()-x_label;
  int dy = 32;
  int y = 10 - (scroll * dy);

  // ----- ODOMETER
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(x_label, y);
  tft.print("ODOMETER (km)");
  int offset_str = x_right - tft.textWidth(String(pref_odo));
  tft.setCursor(offset_str, y);
  tft.printf("%d", pref_odo);

  // ----- MAP MAX
  tft.setTextColor(TFT_WHITE);
  y += dy;
  tft.setCursor(x_label, y);
  tft.print("MAP max (kPa)");
  offset_str = x_right - tft.textWidth("--");
  tft.setCursor(offset_str, y);
  tft.print("--");

  // ----- MAF MAX
  y += dy;
  tft.setCursor(x_label, y);
  tft.print("MAF max (g/s)");
  offset_str = x_right - tft.textWidth("--");
  tft.setCursor(offset_str, y);
  tft.print("--");

  // ----- IAT MAX
  y += dy;
  tft.setCursor(x_label, y);
  tft.print("IAT max (°C)");
  offset_str = x_right - tft.textWidth("--");
  tft.setCursor(offset_str, y);
  tft.print("--");

  // ----- SPEED MAX
  tft.setTextColor(TFT_ORANGE);
  y += dy;
  tft.setCursor(x_label, y);
  tft.print("SPEED max (km/h)");
  float speed_display = pref_speed / 10.0;
  offset_str = x_right - tft.textWidth(String(speed_display, 1));
  tft.setCursor(offset_str, y);
  tft.printf("%.1f", speed_display);

  // ----- SPEED 0-100
  tft.setTextColor(TFT_ORANGE);
  y += dy;
  tft.setCursor(x_label, y);
  tft.print("SPEED 0-100 (s)");
  offset_str = x_right - tft.textWidth("--");
  tft.setCursor(offset_str, y);
  tft.print("--");

  // ----- SPACER
  tft.setTextColor(TFT_WHITE);
  y += dy;
  tft.setCursor(x_label, y);
  tft.print(" ");

  // ----- THEME COLOR uint8_t current_color = 0; // 0=modrá, 1=zelená, 2=červená
  tft.setTextColor(TFT_WHITE);
  y += dy;
  if(scroll == 4){
    tft.setCursor(x_label, y);
    tft.print(">");
    x_label += 15;
  }
  
  tft.setCursor(x_label, y);
  tft.print("Theme");
  offset_str = x_right - tft.textWidth("BLUE");
  tft.setCursor(offset_str, y);
  tft.print("BLUE");

  // ----- LED
  tft.setTextColor(TFT_WHITE);
  y += dy;
  if(scroll == 5){
    tft.setCursor(x_label, y);
    tft.print(">");
    x_label += 15;
  }
  
  tft.setCursor(x_label, y);
  tft.print("Led");
  offset_str = x_right - tft.textWidth("RED");
  tft.setCursor(offset_str, y);
  tft.print("RED");

  // ----- EXIT
  tft.setTextColor(TFT_WHITE);
  y += dy;
  if(scroll == 6){
    tft.setCursor(x_label, y);
    tft.print(">");
    x_label += 15;
  }
  else{
    x_label -= 15;
  }

  tft.setCursor(x_label, y);
  tft.print("Exit");

  tft.setTextColor(TFT_WHITE);
}

void espnow_init_receiver(){

  WiFi.mode(WIFI_STA); // povinné

  if (esp_now_init() != ESP_OK) {
    Serial.println("[-] ESP-NOW init FAILED");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // MAC pre info
  Serial.println(WiFi.macAddress()); //E4:B0:63:41:32:18
  uint8_t mac[6];
  WiFi.macAddress(mac);
  delay(100);
  Serial.println("[+] ESP-NOW RX ready");
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
}