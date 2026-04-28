/*
  https://www.waveshare.com/product/esp32-s3-lcd-1.47.htm

  Board: ESP32S3 Dev Module
  Flash Size: 16MB(128Mb)
  Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
  PSRAM: OPI PSRAM

  WCMCU-230 - CAN bus module based on SN65HVD230
  CANH -> PIN 6 OBDII
  CANL -> PIN 14 OBDII
  CRX  -> PIN 4 EPS32
  CTX  -> PIN 3 ESP32
*/

#include "esp_system.h"

#include <SPI.h>
#include <TFT_eSPI.h>       // Hardware-specific library
#include <Adafruit_NeoPixel.h>

#include "FS.h"
#include "SD_MMC.h"

#include "driver/twai.h"
#include "esp_sleep.h"

#include <WiFi.h>
#include <esp_now.h>

#include <Preferences.h>

#define BOOT_BUTTON 0  // GPIO0

//----------------------------------------------------------------
// ESP NOW
//----------------------------------------------------------------
typedef struct {
  int a;
  int b;
} EspNowPacket;
uint8_t peerMAC[] = {0xE4, 0xB0, 0x63, 0x41, 0x32, 0x18}; // ESP PRIMAC E4:B0:63:41:32:18

//----------------------------------------------------------------
// OBD2
//----------------------------------------------------------------
uint8_t obd_step = 0;
int map_value = 0;
float maf_value = 0;

//----------------------------------------------------------------
// CAN (TWAI)
//----------------------------------------------------------------
#define CAN_RX GPIO_NUM_4
#define CAN_TX GPIO_NUM_3
bool can_ok = false;
uint32_t bitrate_table[] = {100, 125, 250, 500, 1000}; // = 100k,125k,250k,500k,1M
#define BITRATE_COUNT 5
uint32_t br_val;

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

//----------------------------------------------------------------
// Preferences
//----------------------------------------------------------------
Preferences prefs;

uint32_t load_bitrate_setting() {
  prefs.begin("can", true);
  uint32_t val = prefs.getUInt("br", 500); // default 500 kbps
  prefs.end();

  Serial.printf("Loaded bitrate val: %u\n", val);
  return val;
}

void save_bitrate_setting(uint32_t val) {
  prefs.begin("can", false);
  prefs.putUInt("br", val);
  prefs.end();

  Serial.printf("Saved bitrate val: %u\n", val);
}

void change_bitrate() {

  uint32_t current = load_bitrate_setting();
  uint32_t next = 500; // fallback

  // nájdi aktuálny v poli
  for (int i = 0; i < BITRATE_COUNT; i++) {
    if (bitrate_table[i] == current) {
      next = bitrate_table[(i + 1) % BITRATE_COUNT];
      break;
    }
  }

  Serial.printf("Bitrate change: %u -> %u\n", current, next);

  save_bitrate_setting(next);

  delay(600); // nech sa stihne zapísať

  esp_restart(); // hard reset
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

void write_buffer_bin() {

  if (!sd_ok) return;
  if (bufferIndex == 0) return;

  fs::File file = SD_MMC.open("/can.bin", FILE_APPEND);
  if (!file) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(0, 200);
    tft.println(" LOG open failed");
    tft.setTextColor(TFT_BLUE);
    Serial.println("LOG open failed");
    return;
  }

  size_t bytesToWrite = bufferIndex * sizeof(LogFrame);
  size_t written = file.write((uint8_t*)buffer, bytesToWrite);

  if (written != bytesToWrite) {
    Serial.printf("Write error: %u/%u\n", written, bytesToWrite);
    pixels.setPixelColor(0, pixels.Color(111, 0, 0));
    pixels.show();
  } else {
    Serial.printf("Wrote %u frames (%u bytes)\n", bufferIndex, written);
    pixels.setPixelColor(0, pixels.Color(0, 111, 0));
    pixels.show();
  }

  file.close();

  // reset buffer
  bufferIndex = 0;
}

void write_buffer() {//csv
  if (!sd_ok) return;
  if (bufferIndex == 0) return;

  fs::File file = SD_MMC.open("/canexbit.csv", FILE_APPEND);
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
    //bool isExtended = (f.id > 0x7FF); // >11-bit → extended
    bool isExtended = (f.id & 0x80000000);

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

void sim_can_fram(){
  ;
}


void setup() {

  pinMode(TFT_BL2, OUTPUT);
  tft.init();
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

  // CAN (TWAI) configuration
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);

  // 500 kbps je štandard pre OBD-II (väčšina áut)
  br_val = load_bitrate_setting();
  twai_timing_config_t t_config;
  Serial.printf("Using bitrate: %u kbps\n", br_val);

  switch (br_val) {
    case 100:
      t_config = TWAI_TIMING_CONFIG_100KBITS();
      break;
    case 125:
      t_config = TWAI_TIMING_CONFIG_125KBITS();
      break;
    case 250:
      t_config = TWAI_TIMING_CONFIG_250KBITS();
      break;
    case 500:
      t_config = TWAI_TIMING_CONFIG_500KBITS();
      break;
    case 1000:
      t_config = TWAI_TIMING_CONFIG_1MBITS();
      break;
    default:
      Serial.println("Invalid bitrate, fallback to 500k");
      t_config = TWAI_TIMING_CONFIG_500KBITS();
      save_bitrate_setting(500);
      break;
  }
  //twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  //twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();

  // prijímame všetko (sniffer)
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // install driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("TWAI driver installed");
  } else {
    Serial.println("TWAI driver install FAILED");
  }

  // start CAN
  if (twai_start() == ESP_OK) {
    Serial.println("TWAI started");
    can_ok = true;
  } else {
    Serial.println("TWAI start FAILED");
  }

  digitalWrite(TFT_BL2, HIGH); // zapnutie LED
  espnow_init();

  if (sd_ok && can_ok){
    pixels.setPixelColor(0, pixels.Color(0, 0, 111));
    pixels.show();
  }

}

int butt_counter = 0;

void loop() {

  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(2);
  tft.setTextFont(1);

  tft.setCursor(0, 20);

  if (sd_ok){
    tft.setTextColor(TFT_GREEN);
    tft.println(" SD........OK");
    Serial.println(" SD........OK");
  }
  else{
    tft.setTextColor(TFT_RED);
    tft.println(" SD.......FAIL");
    Serial.println(" SD.......FAIL");
  }

  tft.println("");
  if (can_ok){
    tft.setTextColor(TFT_GREEN);
    tft.println(" CAN.......OK");
    Serial.println(" CAN.......OK");
  }
  else{
    tft.setTextColor(TFT_RED);
    tft.println(" CAN......FAIL");
    Serial.println(" CAN......FAIL");
  }
  
  tft.setCursor(0, 260);
  tft.setTextColor(TFT_BLUE);
  tft.printf("]= %u kbps", br_val);
  Serial.printf("Using bitrate: %u kbps\n", br_val);

  tft.setTextColor(TFT_BLUE);
  static uint32_t last_flush = millis();
  static uint32_t sleep_timer = millis();
  static uint32_t send_timer = millis();
  static uint32_t send_timer_obd = millis();

  while (true) {

    twai_message_t message;

    // pokus o prijatie rámca (timeout 10 ms)
    if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {

      // ochrana pred overflow (pred zapisom)
      if (bufferIndex >= BUFFER_SIZE) {
        write_buffer();
      }

      // ulozenie do bufferu
      if (bufferIndex < BUFFER_SIZE) {

        buffer[bufferIndex].id = message.identifier;

        // bezpecny DLC
        uint8_t dlc = message.data_length_code;
        if (dlc > 8) dlc = 8;

        buffer[bufferIndex].dlc = dlc;

        // vycisti buffer
        memset(buffer[bufferIndex].data, 0, 8);

        // kopiruj len bezpecny rozsah
        for (int i = 0; i < dlc; i++) {
          buffer[bufferIndex].data[i] = message.data[i];
        }

        if (message.identifier == 0x000007E8 && dlc >= 4) {
          // 0x41 = OBD response
          // 0x0B = MAP PID
          if (message.data[1] == 0x41 && message.data[2] == 0x0B) {
            map_value = message.data[3];  // MAP v kPa
          }
          // 0x10 MAF
          if (message.data[1] == 0x41 && message.data[2] == 0x10) {
            maf_value = ((message.data[3] << 8) | message.data[4]) / 100.0;
          }
        }
        // -----------------------------------------------

        // timestamp (ms)
        buffer[bufferIndex].ts = millis();

        bufferIndex++;
      }

      // debug
      //Serial.printf("ID: %03X DLC:%d\n", message.identifier, message.data_length_code);
    }

    if (millis() - send_timer_obd >= 70) {
      switch (obd_step) {
        case 0:
          send_obd_map_request();
          break;
        case 1:
          send_obd_maf_request();
          break;
        case 2:
          //send_obd_oil_temp_request();
          break;
      }
      obd_step++;
      if (obd_step >= 3){
        obd_step = 0;
      }
      send_timer_obd = millis();
    }

    if (millis() - send_timer >= 50) {
      
      int val1 = random(40, 260); // <<<<< SIM
      int val2 = millis() / 1000;

      send_2int(map_value, val2);
      if(map_value != 0){
        tft.fillRect(0, 180, tft.width(), 20, TFT_ORANGE);
      }


      send_timer = millis();
      if(digitalRead(BOOT_BUTTON) == LOW){
        butt_counter++;
      }
      else{
        butt_counter = 0;
      }

      if(butt_counter > 40){ //2s
        butt_counter = 0;
        tft.fillRect(0, 220, tft.width(), 20, TFT_RED);
        tft.setCursor(0, 223);
        tft.setTextColor(TFT_BLACK);
        tft.println("  REBOOT... ");
        delay(2000);
        change_bitrate();
      }
    }

    // periodicky flush (kazdu 1 sekundu)
    if (millis() - last_flush > 1000) {

      tft.fillRect(0, 148, tft.width(), 20, TFT_BLACK);
      tft.setCursor(0, 150);
      tft.setTextColor(TFT_BLUE);
      tft.printf(" Frames: %u   ", bufferIndex);
      tft.setTextColor(TFT_BLUE);

      // zapisuj len keď niečo je
      if (bufferIndex > 0) {
        write_buffer();
      }

      last_flush = millis();
    }

    if (millis() - sleep_timer > 60000) {
      ;//sleep_now();
    }

    // watchdog safe
    delay(1);
  }

}

void send_obd_map_request() {
  //0x000007DF ,0,8, 02,01,0B,FF,FF,FF,FF,FF
  twai_message_t msg = {};
  msg.identifier = 0x7DF;
  msg.extd = 0;
  msg.rtr = 0;
  msg.data_length_code = 8;

  msg.data[0] = 0x02; // length
  msg.data[1] = 0x01; // mode 01 (current data)
  msg.data[2] = 0x0B; // PID MAP
  msg.data[3] = 0x00;
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;

  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

void send_obd_maf_request() {
  twai_message_t msg = {};
  msg.identifier = 0x7DF;
  msg.extd = 0;
  msg.rtr = 0;
  msg.data_length_code = 8;

  msg.data[0] = 0x02;
  msg.data[1] = 0x01;
  msg.data[2] = 0x10; // PID MAF
  msg.data[3] = 0x00;
  msg.data[4] = 0x00;
  msg.data[5] = 0x00;
  msg.data[6] = 0x00;
  msg.data[7] = 0x00;

  twai_transmit(&msg, pdMS_TO_TICKS(10));
}

void sleep_now() {
  write_buffer();
  digitalWrite(TFT_BL2, LOW); // off LCD LED
  pixels.clear();
  pixels.show();              // off RGB LED
  esp_sleep_enable_timer_wakeup(10 * 1000000ULL);
  esp_deep_sleep_start();
}

void espnow_init() {

  WiFi.mode(WIFI_STA); // nutnE pre ESP-NOW

  if (esp_now_init() != ESP_OK) {
    Serial.println("[-] ESP-NOW init FAILED");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[-] Failed to add peer");
    return;
  }

  delay(100);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
              mac[0], mac[1], mac[2],
              mac[3], mac[4], mac[5]);

  Serial.println("[+] ESP-NOW ready");
  //Serial.println(WiFi.macAddress()); //80:B5:4E:D9:25:E8

}

void send_2int(int val1, int val2) {
  //Serial.println("[*] Sending...");
  EspNowPacket pkt;
  pkt.a = val1;
  pkt.b = val2;

  esp_err_t result = esp_now_send(peerMAC, (uint8_t*)&pkt, sizeof(pkt));

  if (result != ESP_OK) {
    Serial.println("[-] Send error");
  }
}
