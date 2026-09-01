/*
  https://www.waveshare.com/product/arduino/boards-kits/esp32-s3/esp32-s3-zero.htm?___SID=U

  Board: ESP32S3 Dev Module
  Flash Size: 16MB(128Mb)
  Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
  PSRAM: OPI PSRAM
  10:20:BA:31:43:94

  lsof /dev/ttyACM0
  kill 383653
*/

//====================================================
// ESP32-S3 + NTC 10k
// Zapojenie:
// 3.3V ---- 1.5k ----+---- ADC
//                   |
//                 NTC 10k
//                   |
//                  GND
//====================================================


#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_now.h>

//----------------------------------------------------------------
// ESP NOW
//----------------------------------------------------------------
typedef struct {
  int a;
  int b;
} EspNowPacket;
uint8_t peerMAC[] = {0xAC, 0xEB, 0xE6, 0x1E, 0x1E, 0x68}; // ESP PRIMAC AC:EB:E6:1E:1E:68

//----------------------------------------------------------------
// WS-LED SETTINGS 
//----------------------------------------------------------------
#define NUMPIXELS 1
#define LED 21
Adafruit_NeoPixel pixels(NUMPIXELS, LED, NEO_GRB + NEO_KHZ800);

//----------------------------------------------------------------
// ADC SETTINGS 
//----------------------------------------------------------------
const int ADC_PIN = 1;
const float VCC = 3.305f;
const float R_FIXED = 1494.0f;     // 1.5k

struct Point
{
    float temp;
    float resistance;
};

// Tvoje namerané hodnoty
const Point table[] =
{
    {12, 16580},
    {22, 11770},
    {32, 8330},
    {42, 5960},
    {52, 4500},
    {62, 3400},
    {72, 2700},
    {82, 2100},
    {92, 1860},
    {102,1330},
    {120,1000}
};

const int TABLE_SIZE = sizeof(table) / sizeof(table[0]);

//----------------------------------------------------
// Priemer z 64 merani
//----------------------------------------------------
uint32_t readMilliVoltsAvg(int pin)
{
    uint32_t sum = 0;

    for(int i=0;i<64;i++)
    {
        sum += analogReadMilliVolts(pin);
        delayMicroseconds(100);
    }

    return sum / 64;
}

//----------------------------------------------------
// Napatie -> odpor NTC
//----------------------------------------------------
float voltageToResistance(float voltage)
{
    if(voltage <= 0.001f)
        return 1000000.0f;

    if(voltage >= (VCC-0.001f))
        return 1.0f;

    return R_FIXED * voltage / (VCC - voltage);
}

//----------------------------------------------------
// Linearna interpolacia + beta
//----------------------------------------------------
float resistanceToTemperature(float R){
    //=========================================
    // POD TABULKOU - extrapolácia z prvých 2 bodov
    //=========================================
    if(R > table[0].resistance)
    {
        float R1 = table[0].resistance;     // 16580
        float R2 = table[1].resistance;     // 11770

        float T1 = table[0].temp;           // 12
        float T2 = table[1].temp;           // 22

        return T1 + (R1 - R) * (T2 - T1) / (R1 - R2);
    }


    //=========================================
    // NAD TABULKOU - extrapolácia z posledných 2 bodov
    //=========================================
    if(R < table[TABLE_SIZE-1].resistance)
    {
        float R1 = table[TABLE_SIZE-2].resistance; // 1330
        float R2 = table[TABLE_SIZE-1].resistance; // 1000

        float T1 = table[TABLE_SIZE-2].temp;       // 102
        float T2 = table[TABLE_SIZE-1].temp;       // 120

        return T1 + (R1 - R) * (T2 - T1) / (R1 - R2);
    }


    //=========================================
    // VNÚTRI TABULKY - lineárna interpolácia
    //=========================================
    for(int i = 0; i < TABLE_SIZE-1; i++)
    {
        if(R <= table[i].resistance &&
           R >= table[i+1].resistance)
        {
            float R1 = table[i].resistance;
            float R2 = table[i+1].resistance;

            float T1 = table[i].temp;
            float T2 = table[i+1].temp;

            return T1 + (R1 - R) * (T2 - T1) / (R1 - R2);
        }
    }

    return NAN;
}

void setup() {

  delay(250);
  pixels.begin();
  pixels.clear();
  Serial.begin(115200);

  delay(500);
  pixels.setPixelColor(0, pixels.Color(111, 0, 0));
  pixels.show();

  analogReadResolution(12);   // 0-4095
  analogSetAttenuation(ADC_11db);
  delay(500);
  espnow_init();
}

void loop(){
    uint32_t mv = readMilliVoltsAvg(ADC_PIN);

    float voltage = mv / 1000.0f;

    float resistance = voltageToResistance(voltage);

    float temperature = resistanceToTemperature(resistance);

    Serial.print("Napatie : ");
    Serial.print(voltage,3);
    Serial.println(" V");

    Serial.print("Odpor   : ");
    Serial.print(resistance,0);
    Serial.println(" ohm");

    Serial.print("Teplota : ");
    Serial.print(temperature,1);
    Serial.println(" C");

    Serial.println("-----------------------");

    int itemperature = (int)round(temperature * 100.0f);
    int iresistance  = (int)round(resistance);
    send_2int(itemperature, iresistance);

    delay(1000);
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

}

void send_2int(int val1, int val2) {
  Serial.print("[*] Sending... ");
  Serial.print(val1);
  Serial.print(" : ");
  Serial.println(val2);
  EspNowPacket pkt;
  pkt.a = val1;
  pkt.b = val2;

  esp_err_t result = esp_now_send(peerMAC, (uint8_t*)&pkt, sizeof(pkt));

  if (result != ESP_OK) {
    Serial.println("[-] Send error");
  }
}