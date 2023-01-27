#include <Arduino.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <max6675.h>
#include <Smoothed.h>
#include <Preferences.h>
#include <PID_v1.h>

#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET "ESPAsync_WiFiManager v1.15.0"
#define ESP_ASYNC_WIFIMANAGER_VERSION_MIN 1015000

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _ESPASYNC_WIFIMGR_LOGLEVEL_ 4

// To not display stored SSIDs and PWDs on Config Portal, select false. Default is true
// Even the stored Credentials are not display, just leave them all blank to reconnect and reuse the stored Credentials
//#define DISPLAY_STORED_CREDENTIALS_IN_CP        false

#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>

// From v1.1.0
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
//////

// You only need to format the filesystem once
//#define FORMAT_FILESYSTEM true
#define FORMAT_FILESYSTEM false

// LittleFS has higher priority than SPIFFS
#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 2))
#define USE_LITTLEFS true
#define USE_SPIFFS false
#elif defined(ARDUINO_ESP32C3_DEV)
// For core v1.0.6-, ESP32-C3 only supporting SPIFFS and EEPROM. To use v2.0.0+ for LittleFS
#define USE_LITTLEFS false
#define USE_SPIFFS true
#endif

#if USE_LITTLEFS
// Use LittleFS
#include "FS.h"

// Check cores/esp32/esp_arduino_version.h and cores/esp32/core_version.h
//#if ( ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(2, 0, 0) )  //(ESP_ARDUINO_VERSION_MAJOR >= 2)
#if (defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 2))
#if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
#warning Using ESP32 Core 1.0.6 or 2.0.0+
#endif

// The library has been merged into esp32 core from release 1.0.6
#include <LittleFS.h>  // https://github.com/espressif/arduino-esp32/tree/master/libraries/LittleFS

FS *filesystem = &LittleFS;
#define FileFS LittleFS
#define FS_Name "LittleFS"
#else
#if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
#warning Using ESP32 Core 1.0.5-. You must install LITTLEFS library
#endif

// The library has been merged into esp32 core from release 1.0.6
#include <LITTLEFS.h>  // https://github.com/lorol/LITTLEFS

FS *filesystem = &LITTLEFS;
#define FileFS LITTLEFS
#define FS_Name "LittleFS"
#endif

#elif USE_SPIFFS
#include <SPIFFS.h>
FS *filesystem = &SPIFFS;
#define FileFS SPIFFS
#define FS_Name "SPIFFS"
#else
// Use FFat
#include <FFat.h>
FS *filesystem = &FFat;
#define FileFS FFat
#define FS_Name "FFat"
#endif

#include <SPIFFSEditor.h>

// From v1.1.0
#define MIN_AP_PASSWORD_SIZE 8

#define SSID_MAX_LEN 32
//From v1.0.10, WPA2 passwords can be up to 63 characters long.
#define PASS_MAX_LEN 64

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw[PASS_MAX_LEN];
} WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
} WiFi_Credentials_String;

#define NUM_WIFI_CREDENTIALS 2

// Assuming max 49 chars
#define TZNAME_MAX_LEN 50
#define TIMEZONE_MAX_LEN 50

typedef struct
{
  WiFi_Credentials WiFi_Creds[NUM_WIFI_CREDENTIALS];
  char TZ_Name[TZNAME_MAX_LEN];  // "America/Toronto"
  char TZ[TIMEZONE_MAX_LEN];     // "EST5EDT,M3.2.0,M11.1.0"
  uint16_t checksum;
} WM_Config;

WM_Config WM_config;

#define CONFIG_FILENAME F("/wifi_cred.dat")
//////

// Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
bool initialConfig = false;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESP_WiFiManager.h>
#define USE_AVAILABLE_PAGES true

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
#define USE_STATIC_IP_CONFIG_IN_CP true

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP true

// Just use enough to save memory. On ESP8266, can cause blank ConfigPortal screen
// if using too much memory
#define USING_AFRICA false
#define USING_AMERICA false
#define USING_ANTARCTICA false
#define USING_ASIA false
#define USING_ATLANTIC false
#define USING_AUSTRALIA false
#define USING_EUROPE true
#define USING_INDIAN false
#define USING_PACIFIC false
#define USING_ETC_GMT false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP false

#define USING_CORS_FEATURE true

////////////////////////////////////////////

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
// Force DHCP to be true
#if defined(USE_DHCP_IP)
#undef USE_DHCP_IP
#endif
#define USE_DHCP_IP true
#else
// You can select DHCP or Static IP here
//#define USE_DHCP_IP     true
#define USE_DHCP_IP false
#endif

#if (USE_DHCP_IP)
// Use DHCP

#if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
#warning Using DHCP IP
#endif

IPAddress stationIP = IPAddress(0, 0, 0, 0);
IPAddress gatewayIP = IPAddress(192, 168, 2, 1);
IPAddress netMask = IPAddress(255, 255, 255, 0);

#else
// Use static IP

#if (_ESPASYNC_WIFIMGR_LOGLEVEL_ > 3)
#warning Using static IP
#endif

#ifdef ESP32
IPAddress stationIP = IPAddress(192, 168, 2, 232);
#else
IPAddress stationIP = IPAddress(192, 168, 2, 186);
#endif

IPAddress gatewayIP = IPAddress(192, 168, 2, 1);
IPAddress netMask = IPAddress(255, 255, 255, 0);
#endif

////////////////////////////////////////////


#define USE_CONFIGURABLE_DNS true

IPAddress dns1IP = gatewayIP;
IPAddress dns2IP = IPAddress(8, 8, 8, 8);

#define USE_CUSTOM_AP_IP false

IPAddress APStaticIP = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN = IPAddress(255, 255, 255, 0);

#include <ESPAsync_WiFiManager.h>  //https://github.com/khoih-prog/ESPAsync_WiFiManager

// Redundant, for v1.10.0 only
//#include <ESPAsync_WiFiManager-Impl.h>          //https://github.com/khoih-prog/ESPAsync_WiFiManager

// SSID and PW for Config Portal
String ssid = "ESP_" + String(ESP_getChipId(), HEX);
String password;

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

String host = "async-esp32fs";

#define HTTP_PORT 80

AsyncWebServer server(HTTP_PORT);
//AsyncDNSServer dnsServer;

AsyncEventSource events("/events");

String http_username = "admin";
String http_password = "admin";

String separatorLine = "===============================================================";

///////////////////////////////////////////
// New in v1.4.0
/******************************************
   // Defined in ESPAsync_WiFiManager.h
  typedef struct
  {
  IPAddress _ap_static_ip;
  IPAddress _ap_static_gw;
  IPAddress _ap_static_sn;

  }  WiFi_AP_IPConfig;

  typedef struct
  {
  IPAddress _sta_static_ip;
  IPAddress _sta_static_gw;
  IPAddress _sta_static_sn;
  #if USE_CONFIGURABLE_DNS
  IPAddress _sta_static_dns1;
  IPAddress _sta_static_dns2;
  #endif
  }  WiFi_STA_IPConfig;
******************************************/

WiFi_AP_IPConfig WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;


#define SDA_PIN 33
#define SCL_PIN 35
#define APA102_SDI_PIN 38
#define APA102_CLK_PIN 37
#define OLED_RST_PIN 45
#define CS_MAX1_PIN 13
#define CS_MAX2_PIN 12
#define MISO_PIN 9
#define MOSI_PIN 11
#define CLK_PIN 7

#define MOS_CTRL1_PIN 5
#define MOS_CTRL2_PIN 6
#define SSR1_PIN 1
#define SSR2_PIN 2
#define SERVO_PIN 3
#define BUZZER_PIN 15
#define BTN1_PIN 36
#define BTN2_PIN 39
#define BTN3_PIN 40

#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

#define WIDTH 128
#define HEIGHT 64
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/OLED_RST_PIN, /* clock=*/SCL_PIN, /* data=*/SDA_PIN);

double pid_setpoint, pid_input, pid_output;
double Kp = 25, Ki = 0.3, Kd = 0.05;
double Kpr = 50, Kir = 0.1, Kdr = 0;
PID pid(&pid_input, &pid_output, &pid_setpoint, Kp, Ki, Kd, DIRECT);
PID pid_reflow(&pid_input, &pid_output, &pid_setpoint, Kpr, Kir, Kdr, DIRECT);


int dutyCycle = 100;

const int BuzzerPWMFreq = 2000;
const int BuzzerPWMChannel = 1;
const int BuzzerPWMResolution = 8;

const int PWMFreq = 20000;  //50hz
const int PWMChannel = 0;
const int PWMResolution = 10;  //12 bits 0-4095


MAX6675 thermocouple1(CLK_PIN, CS_MAX2_PIN, MISO_PIN);
MAX6675 thermocouple2(CLK_PIN, CS_MAX1_PIN, MISO_PIN);

float current_temperature1 = 0.0;
float current_temperature2 = 0.0;

float pwm_out1 = 0.0;
float pwm_out2 = 0.0;

bool running_reflow = false;
bool init_reflow = false;

bool running_const = false;
bool init_const = false;

bool rampup_reflow = true;
bool rampup_const = true;
bool rampup_const_close = true;
bool preheat_done = false;
bool reflow_done = false;
bool buzzer_state = true;

// Soldering profile
float temperature_setpoint_reflow = 0.0;
float temperature_setpoint_const = 100.0;

float temperature_off = 30.0;
float temperature_soak1 = 150.0;
float temperature_soak2 = 160.0;
float temperature_reflow = 220.0;
float temperature_cooling = 210.0;

int profile_counter = 0;
Preferences preferences;

// int temp_reflow_profile1[] = {
//   30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,
//   78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,
//   120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,150,150,150,150,150,150,
//   150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,
//   150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,150,
//   150,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,
//   185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,211,212,212,213,213,214,214,215,215,
//   216,216,217,217,218,218,219,219,220,220,219,218,217,216,215,214,213,212,211,210,209,208,207,206,205,204,203,202,201,200,
// };

#define N_PROFILES 10.0
float selected_profile = 0.0;
float profile_temp[10][10];  // initial, soak1, soak2, reflow, cooling
float profile_time[] = { 0.0, 120.0, 200.0, 260.0, 300.0 };

int temp_reflow_individual[300];

void store_profile() {
  String key = "profile" + String(int(selected_profile)) + "_";
  preferences.putFloat((key + "soak1").c_str(), temperature_soak1);
  preferences.putFloat((key + "soak2").c_str(), temperature_soak2);
  preferences.putFloat((key + "reflow").c_str(), temperature_reflow);
  preferences.putFloat((key + "cooling").c_str(), temperature_cooling);
}

void read_all_profiles() {
  // preferences.putFloat((name + "_0").c_str(), acs_offset_zero);
  for (int i = 0; i < int(N_PROFILES); i++) {
    String key = "profile" + String(i) + "_";
    profile_temp[i][1] = preferences.getFloat((key + "soak1").c_str());
    profile_temp[i][2] = preferences.getFloat((key + "soak2").c_str());
    profile_temp[i][3] = preferences.getFloat((key + "reflow").c_str());
    profile_temp[i][4] = preferences.getFloat((key + "cooling").c_str());
  }
}

void calculate_profile_individual() {
  profile_temp[int(selected_profile)][0] = temperature_off;
  profile_temp[int(selected_profile)][1] = temperature_soak1;
  profile_temp[int(selected_profile)][2] = temperature_soak2;
  profile_temp[int(selected_profile)][3] = temperature_reflow;
  profile_temp[int(selected_profile)][4] = temperature_cooling;

  store_profile();

  for (int i = 0; i < 300; i++) {
    if (i >= profile_time[3]) temp_reflow_individual[i] = (profile_temp[int(selected_profile)][4] - profile_temp[int(selected_profile)][3]) / (profile_time[4] - profile_time[3]) * (i - profile_time[3]) + profile_temp[int(selected_profile)][3];
    else if (i >= profile_time[2]) temp_reflow_individual[i] = (profile_temp[int(selected_profile)][3] - profile_temp[int(selected_profile)][2]) / (profile_time[3] - profile_time[2]) * (i - profile_time[2]) + profile_temp[int(selected_profile)][2];
    else if (i >= profile_time[1]) temp_reflow_individual[i] = (profile_temp[int(selected_profile)][2] - profile_temp[int(selected_profile)][1]) / (profile_time[2] - profile_time[1]) * (i - profile_time[1]) + profile_temp[int(selected_profile)][1];
    else if (i >= profile_time[0]) temp_reflow_individual[i] = (profile_temp[int(selected_profile)][1] - profile_temp[int(selected_profile)][0]) / (profile_time[1] - profile_time[0]) * (i - profile_time[0]) + profile_temp[int(selected_profile)][0];
  }
}


// Set used fonts with width and height of each character
struct fonts {
  const uint8_t *font;
  int width;
  int height;
};

fonts font_xl = { u8g2_font_profont29_mf, 16, 19 };
fonts font_m = { u8g2_font_profont17_mf, 9, 11 };
fonts font_s = { u8g2_font_profont12_mf, 6, 8 };
fonts font_xs = { u8g2_font_profont10_mf, 5, 6 };

// Custom type to set up a page on the OLED display. Baisc structure is a page of three fields.
// A top one with the power, a left one with the voltage and a right one with the current.
struct display_page {
  String title;
  float *value;
  String msg;
  bool is_value;
};

struct display_page_int {
  String title;
  int *value;
  String msg;
  bool is_value;
};

float val1 = 0.0;
float placeholder = 0.0;
String msg2display_top = "";
String msg2display_bot = "";

display_page page_start_reflow = { "Start Reflow", &placeholder, "Start Reflow", false };
display_page page_start_const = { "Start Const", &placeholder, "Start Const", false };
display_page page_select_profile = { "Set Profile", &selected_profile, "", true };
display_page page_const_temp = { "Const Temp", &temperature_setpoint_const, "", true };
display_page page_reflow_temp = { "Reflow Temp", &temperature_reflow, "", true };
display_page page_soak1_temp = { "Soak1 Temp", &temperature_soak1, "", true };
display_page page_soak2_temp = { "Soak2 Temp", &temperature_soak2, "", true };
display_page page_cooling_temp = { "Cooling Temp", &temperature_cooling, "", true };
display_page page_manual_out1 = { "Set PWM1", &pwm_out1, "", true };
display_page page_manual_out2 = { "Set PWM2", &pwm_out2, "", true };
display_page page_i2cscan = { "Start I2C Scan", &placeholder, "Start I2C Scan", false };

// Menu and Pages
#define N_PAGES 11
int current_page = 0;
bool new_page = true;
unsigned long t_new_page = 0;

struct page {
  int p;
  display_page &page;
};

page pages[N_PAGES] = {
  { 0, page_start_reflow },
  { 1, page_select_profile },
  { 2, page_start_const },
  { 3, page_soak1_temp },
  { 4, page_soak2_temp },
  { 5, page_reflow_temp },
  { 6, page_cooling_temp },
  { 7, page_const_temp },
  { 8, page_manual_out1 },
  { 9, page_manual_out2 },
  { 10, page_i2cscan },
};

// Alignment for the OLED display
enum box_alignment {
  Center,
  Left,
  Right
};

// Custom type for setting up a box which can be drawn to the oled display. It is defined by
// the top left corner and the width and height of the box.
struct box {
  int32_t left, top, width, height;
  int32_t radius;
  bool has_frame;

  void draw(bool fill) {
    int frame = has_frame ? 4 : 0;
    if (fill) u8g2.drawRBox(left + frame, top + frame, width - (2 * frame), height - (2 * frame), radius);
    else u8g2.drawRFrame(left, top, width, height, radius);
  }

  void print_text(String str, fonts f, box_alignment align, float pos) {
    u8g2.setFont(f.font);
    int x = 0;
    if (align == Center) x = left + 0.5 * (width - (f.width * str.length() - 1));
    else if (align == Left) x = left + 5;
    else if (align == Right) x = left + width - f.width * str.length() - 5;
    u8g2.setCursor(x, top + pos * height + f.height / 2);
    u8g2.print(str);
  }
};

// Initialize the boxes for the OLED
box top_left = { 0, 0, 36, 10, 0, false };
box top = { top_left.width, 0, 56, 10, 0, false };
box top_right = { top.left + top.width, 0, top_left.width, 10, 0, false };
box mid_main = { 0, top.height, 128, 34, 0, false };
box bot = { 0, mid_main.top + mid_main.height, 128, 10, 0, false };

// Update the OLED display with the three boxes according to the given pages
void update_display() {
  u8g2.clearBuffer();
  mid_main.draw(false);

  if (new_page) {
    if (millis() <= t_new_page + 750) mid_main.print_text(pages[current_page].page.title, font_m, Center, 0.5);
    else new_page = false;
  }

  if (!new_page) {
    if (pages[current_page].page.is_value) {
      if (current_page == 1) {  //set profile page
        mid_main.print_text(String(*pages[current_page].page.value, 0), font_m, Center, 0.5);
        msg2display_bot = String(profile_temp[int(selected_profile)][1], 0) + ", " + String(profile_temp[int(selected_profile)][2], 0) + ", " + String(profile_temp[int(selected_profile)][3], 0) + ", " + String(profile_temp[int(selected_profile)][4], 0);
      } else mid_main.print_text(String(*pages[current_page].page.value, 1) + "C", font_s, Center, 0.5);
    } else {
      mid_main.print_text(("Cur Temp: " + String(current_temperature1) + " C"), font_s, Left, 0.21);
      mid_main.print_text(("Set Temp: " + String(pid_setpoint) + " C"), font_s, Left, 0.5);
      mid_main.print_text(("Progress: " + String(float(profile_counter) / 3.0, 0) + " %"), font_s, Left, 0.8);
    }

    top_left.print_text(String(current_temperature1, 0) + "C", font_xs, Center, 0.5);
    top_right.print_text(String(current_temperature2, 0) + "C", font_xs, Center, 0.5);
    bot.print_text(msg2display_bot, font_xs, Center, 0.5);
    top.print_text(msg2display_top, font_xs, Center, 0.5);
  }
  u8g2.sendBuffer();
}


void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig) {
  in_WM_AP_IPconfig._ap_static_ip = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig) {
  in_WM_STA_IPconfig._sta_static_ip = stationIP;
  in_WM_STA_IPconfig._sta_static_gw = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn = netMask;
#if USE_CONFIGURABLE_DNS
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig) {
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, F(", gatewayIP ="), in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, F(", dns2IP ="), in_WM_STA_IPconfig._sta_static_dns2);
#endif
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig) {
#if USE_CONFIGURABLE_DNS
  // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn,
              in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);
#else
  // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
  WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
#endif
}

///////////////////////////////////////////

uint8_t connectMultiWiFi() {
#if ESP32
  // For ESP32, this better be 0 to shorten the connect time.
  // For ESP32-S2/C3, must be > 500
#if (USING_ESP32_S2 || USING_ESP32_C3)
#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 500L
#else
  // For ESP32 core v1.0.6, must be >= 500
#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 800L
#endif
#else
  // For ESP8266, this better be 2200 to enable connect the 1st time
#define WIFI_MULTI_1ST_CONNECT_WAITING_MS 2200L
#endif

#define WIFI_MULTI_CONNECT_WAITING_MS 500L

  uint8_t status;

  //WiFi.mode(WIFI_STA);

  LOGERROR(F("ConnectMultiWiFi with :"));

  if ((Router_SSID != "") && (Router_Pass != "")) {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass);
    LOGERROR3(F("* Add SSID = "), Router_SSID, F(", PW = "), Router_Pass);
    wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
  }

  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ((String(WM_config.WiFi_Creds[i].wifi_ssid) != "")
        && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE)) {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw);
    }
  }

  LOGERROR(F("Connecting MultiWifi..."));

  //WiFi.mode(WIFI_STA);

#if !USE_DHCP_IP
  // New in v1.4.0
  configWiFi(WM_STA_IPconfig);
  //////
#endif

  int i = 0;
  status = wifiMulti.run();
  delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

  while ((i++ < 20) && (status != WL_CONNECTED)) {
    status = WiFi.status();

    if (status == WL_CONNECTED)
      break;
    else
      delay(WIFI_MULTI_CONNECT_WAITING_MS);
  }

  if (status == WL_CONNECTED) {
    LOGERROR1(F("WiFi connected after time: "), i);
    LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
    LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP());
    ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  } else {
    LOGERROR(F("WiFi not connected"));

    ESP.restart();
  }

  return status;
}

//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

#if USE_ESP_WIFIMANAGER_NTP

void printLocalTime() {
#if ESP8266
  static time_t now;

  now = time(nullptr);

  if (now > 1451602800) {
    Serial.print("Local Date/Time: ");
    Serial.print(ctime(&now));
  }

#else
  struct tm timeinfo;

  getLocalTime(&timeinfo);

  // Valid only if year > 2000.
  // You can get from timeinfo : tm_year, tm_mon, tm_mday, tm_hour, tm_min, tm_sec
  if (timeinfo.tm_year > 100) {
    Serial.print("Local Date/Time: ");
    Serial.print(asctime(&timeinfo));
  }

#endif
}

#endif

void heartBeatPrint() {
#if USE_ESP_WIFIMANAGER_NTP
  printLocalTime();
#else
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print(F("H"));  // H means connected to WiFi
  else
    Serial.print(F("F"));  // F means not connected to WiFi

  if (num == 80) {
    Serial.println();
    num = 1;
  } else if (num++ % 10 == 0) {
    Serial.print(F(" "));
  }

#endif
}

void check_WiFi() {
  if ((WiFi.status() != WL_CONNECTED)) {
    Serial.println(F("\nWiFi lost. Call connectMultiWiFi in loop"));
    connectMultiWiFi();
  }
}

void check_status() {
  static ulong checkstatus_timeout = 0;
  static ulong checkwifi_timeout = 0;

  static ulong current_millis;

#define WIFICHECK_INTERVAL 1000L

#if USE_ESP_WIFIMANAGER_NTP
#define HEARTBEAT_INTERVAL 60000L
#else
#define HEARTBEAT_INTERVAL 10000L
#endif

  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0)) {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0)) {
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }
}

int calcChecksum(uint8_t *address, uint16_t sizeToCalc) {
  uint16_t checkSum = 0;

  for (uint16_t index = 0; index < sizeToCalc; index++) {
    checkSum += *(((byte *)address) + index);
  }

  return checkSum;
}

bool loadConfigData() {
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));

  memset((void *)&WM_config, 0, sizeof(WM_config));

  // New in v1.4.0
  memset((void *)&WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  //////

  if (file) {
    file.readBytes((char *)&WM_config, sizeof(WM_config));

    // New in v1.4.0
    file.readBytes((char *)&WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));

    if (WM_config.checksum != calcChecksum((uint8_t *)&WM_config, sizeof(WM_config) - sizeof(WM_config.checksum))) {
      LOGERROR(F("WM_config checksum wrong"));

      return false;
    }

    // New in v1.4.0
    displayIPConfigStruct(WM_STA_IPconfig);
    //////

    return true;
  } else {
    LOGERROR(F("failed"));

    return false;
  }
}

void saveConfigData() {
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));

  if (file) {
    WM_config.checksum = calcChecksum((uint8_t *)&WM_config, sizeof(WM_config) - sizeof(WM_config.checksum));

    file.write((uint8_t *)&WM_config, sizeof(WM_config));

    displayIPConfigStruct(WM_STA_IPconfig);

    // New in v1.4.0
    file.write((uint8_t *)&WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    //////

    file.close();
    LOGERROR(F("OK"));
  } else {
    LOGERROR(F("failed"));
  }
}

void i2cscan() {
  byte error, address;
  int nDevices;

  Serial.println("Scanning...");

  nDevices = 0;
  for (address = 1; address < 127; address++) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      //USBSerial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  } else
    Serial.println("done\n");
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN1_PIN, INPUT);
  pinMode(BTN2_PIN, INPUT);
  pinMode(BTN3_PIN, INPUT);
  pinMode(CS_MAX1_PIN, OUTPUT);
  pinMode(CS_MAX2_PIN, OUTPUT);
  digitalWrite(CS_MAX1_PIN, LOW);
  digitalWrite(CS_MAX2_PIN, LOW);
  pinMode(SSR1_PIN, OUTPUT);
  pinMode(SSR2_PIN, OUTPUT);
  digitalWrite(SSR1_PIN, LOW);
  digitalWrite(SSR2_PIN, LOW);
  FastLED.addLeds<APA102, APA102_SDI_PIN, APA102_CLK_PIN, BGR>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  leds[0] = CRGB::Black;
  FastLED.show();
  ledcSetup(PWMChannel, PWMFreq, PWMResolution);
  ledcWrite(PWMChannel, dutyCycle);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  ledcSetup(BuzzerPWMChannel, BuzzerPWMFreq, BuzzerPWMResolution);
  ledcAttachPin(BUZZER_PIN, BuzzerPWMChannel);
  ledcWriteTone(BuzzerPWMChannel, 2000);
  delay(200);
  ledcWriteTone(BuzzerPWMChannel, 0);
  ledcWrite(PWMChannel, 0);
  preferences.begin("reflowprofiles", false);
  read_all_profiles();
  u8g2.begin();
  t_new_page = millis();
  update_display();

  Serial.print(F("\nStarting Async_ESP32_FSWebServer using "));
  Serial.print(FS_Name);
  Serial.print(F(" on "));
  Serial.println(ARDUINO_BOARD);
  Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION);

#if defined(ESP_ASYNC_WIFIMANAGER_VERSION_INT)

  if (ESP_ASYNC_WIFIMANAGER_VERSION_INT < ESP_ASYNC_WIFIMANAGER_VERSION_MIN) {
    Serial.print("Warning. Must use this example on Version later than : ");
    Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION_MIN_TARGET);
  }

#endif

  Serial.setDebugOutput(false);

  if (FORMAT_FILESYSTEM)
    FileFS.format();

  // Format FileFS if not yet
  if (!FileFS.begin(true)) {
    Serial.println(F("SPIFFS/LittleFS failed! Already tried formatting."));

    if (!FileFS.begin()) {
      // prevents debug info from the library to hide err message.
      delay(100);

#if USE_LITTLEFS
      Serial.println(F("LittleFS failed!. Please use SPIFFS or EEPROM. Stay forever"));
#else
      Serial.println(F("SPIFFS failed!. Please use LittleFS or EEPROM. Stay forever"));
#endif

      while (true) {
        delay(1);
      }
    }
  }

  File root = FileFS.open("/");
  File file = root.openNextFile();

  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    file = root.openNextFile();
  }

  Serial.println();

  unsigned long startedAt = millis();

  // New in v1.4.0
  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);
  //////

  //Local intialization. Once its business is done, there is no need to keep it around
  // Use this to default DHCP hostname to ESP8266-XXXXXX or ESP32-XXXXXX
  //ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer);
  // Use this to personalize DHCP hostname (RFC952 conformed)
#if (USING_ESP32_S2 || USING_ESP32_C3)
  ESPAsync_WiFiManager ESPAsync_wifiManager(&server, NULL, "AsyncESP32-FSWebServer");
#else
  AsyncDNSServer dnsServer;

  ESPAsync_WiFiManager ESPAsync_wifiManager(&server, &dnsServer, "AsyncESP32-FSWebServer");
#endif

#if USE_CUSTOM_AP_IP
  //set custom ip for portal
  // New in v1.4.0
  ESPAsync_wifiManager.setAPStaticIPConfig(WM_AP_IPconfig);
  //////
#endif

  ESPAsync_wifiManager.setMinimumSignalQuality(-1);

  // Set config portal channel, default = 1. Use 0 => random channel from 1-13
  ESPAsync_wifiManager.setConfigPortalChannel(0);
  //////

#if !USE_DHCP_IP
  // Set (static IP, Gateway, Subnetmask, DNS1 and DNS2) or (IP, Gateway, Subnetmask). New in v1.0.5
  // New in v1.4.0
  ESPAsync_wifiManager.setSTAStaticIPConfig(WM_STA_IPconfig);
  //////
#endif

  // New from v1.1.1
#if USING_CORS_FEATURE
  ESPAsync_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // We can't use WiFi.SSID() in ESP32as it's only valid after connected.
  // SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
  // Have to create a new function to store in EEPROM/SPIFFS for this purpose
  Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
  Router_Pass = ESPAsync_wifiManager.WiFi_Pass();

  //Remove this line if you do not want to see WiFi password printed
  Serial.println("ESP Self-Stored: SSID = " + Router_SSID + ", Pass = " + Router_Pass);

  // SSID to uppercase
  ssid.toUpperCase();
  password = "My" + ssid;

  bool configDataLoaded = loadConfigData();

  if (configDataLoaded) {
#if USE_ESP_WIFIMANAGER_NTP

    if (strlen(WM_config.TZ_Name) > 0) {
      LOGERROR3(F("Saving current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);

#if ESP8266
      configTime(WM_config.TZ, "pool.ntp.org");
#else
      //configTzTime(WM_config.TZ, "pool.ntp.org" );
      configTzTime(WM_config.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
#endif
    } else {
      Serial.println(F("Current Timezone is not set. Enter Config Portal to set."));
    }

#endif
  } else {
    // From v1.1.0, Don't permit NULL password
    if ((Router_SSID == "") || (Router_Pass == "")) {
      Serial.println(F("We haven't got any access point credentials, so get them now"));

      initialConfig = true;

      Serial.print(F("Starting configuration portal @ "));

#if USE_CUSTOM_AP_IP
      Serial.print(APStaticIP);
#else
      Serial.print(F("192.168.4.1"));
#endif

      Serial.print(F(", SSID = "));
      Serial.print(ssid);
      Serial.print(F(", PWD = "));
      Serial.println(password);

#if DISPLAY_STORED_CREDENTIALS_IN_CP
      // New. Update Credentials, got from loadConfigData(), to display on CP
      ESPAsync_wifiManager.setCredentials(WM_config.WiFi_Creds[0].wifi_ssid, WM_config.WiFi_Creds[0].wifi_pw,
                                          WM_config.WiFi_Creds[1].wifi_ssid, WM_config.WiFi_Creds[1].wifi_pw);
#endif

      // Starts an access point
      if (!ESPAsync_wifiManager.startConfigPortal((const char *)ssid.c_str(), password.c_str()))
        Serial.println(F("Not connected to WiFi but continuing anyway."));
      else
        Serial.println(F("WiFi connected...yeey :)"));

      // Stored  for later usage, from v1.1.0, but clear first
      memset(&WM_config, 0, sizeof(WM_config));

      for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
        String tempSSID = ESPAsync_wifiManager.getSSID(i);
        String tempPW = ESPAsync_wifiManager.getPW(i);

        if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
          strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
        else
          strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);

        if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
          strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
        else
          strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);

        // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
        if ((String(WM_config.WiFi_Creds[i].wifi_ssid) != "")
            && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE)) {
          LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw);
          wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        }
      }

#if USE_ESP_WIFIMANAGER_NTP
      String tempTZ = ESPAsync_wifiManager.getTimezoneName();

      if (strlen(tempTZ.c_str()) < sizeof(WM_config.TZ_Name) - 1)
        strcpy(WM_config.TZ_Name, tempTZ.c_str());
      else
        strncpy(WM_config.TZ_Name, tempTZ.c_str(), sizeof(WM_config.TZ_Name) - 1);

      const char *TZ_Result = ESPAsync_wifiManager.getTZ(WM_config.TZ_Name);

      if (strlen(TZ_Result) < sizeof(WM_config.TZ) - 1)
        strcpy(WM_config.TZ, TZ_Result);
      else
        strncpy(WM_config.TZ, TZ_Result, sizeof(WM_config.TZ_Name) - 1);

      if (strlen(WM_config.TZ_Name) > 0) {
        LOGERROR3(F("Saving current TZ_Name ="), WM_config.TZ_Name, F(", TZ = "), WM_config.TZ);

#if ESP8266
        configTime(WM_config.TZ, "pool.ntp.org");
#else
        //configTzTime(WM_config.TZ, "pool.ntp.org" );
        configTzTime(WM_config.TZ, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
#endif
      } else {
        LOGERROR(F("Current Timezone Name is not set. Enter Config Portal to set."));
      }

#endif

      // New in v1.4.0
      ESPAsync_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
      //////

      saveConfigData();
    } else {
      wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
    }
  }

  startedAt = millis();

  if (!initialConfig) {
    // Load stored data, the addAP ready for MultiWiFi reconnection
    if (!configDataLoaded)
      loadConfigData();

    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++) {
      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ((String(WM_config.WiFi_Creds[i].wifi_ssid) != "")
          && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE)) {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw);
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("ConnectMultiWiFi in setup"));

      connectMultiWiFi();
    }
  }

  Serial.print(F("After waiting "));
  Serial.print((float)(millis() - startedAt) / 1000L);
  Serial.print(F(" secs more in setup(), connection result is "));

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("connected. Local IP: "));
    Serial.println(WiFi.localIP());
  } else
    Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));

  if (!MDNS.begin(host.c_str())) {
    Serial.println(F("Error starting MDNS responder!"));
  }

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", HTTP_PORT);

  //SERVER INIT
  events.onConnect([](AsyncEventSourceClient *client) {
    client->send("hello!", NULL, millis(), 1000);
  });

  server.addHandler(&events);

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.addHandler(new SPIFFSEditor(FileFS, http_username, http_password));
  server.serveStatic("/", FileFS, "/").setDefaultFile("index.htm");

  server.onNotFound([](AsyncWebServerRequest *request) {
    Serial.print(F("NOT_FOUND: "));

    if (request->method() == HTTP_GET)
      Serial.print(F("GET"));
    else if (request->method() == HTTP_POST)
      Serial.print(F("POST"));
    else if (request->method() == HTTP_DELETE)
      Serial.print(F("DELETE"));
    else if (request->method() == HTTP_PUT)
      Serial.print(F("PUT"));
    else if (request->method() == HTTP_PATCH)
      Serial.print(F("PATCH"));
    else if (request->method() == HTTP_HEAD)
      Serial.print(F("HEAD"));
    else if (request->method() == HTTP_OPTIONS)
      Serial.print(F("OPTIONS"));
    else
      Serial.print(F("UNKNOWN"));

    Serial.println(" http://" + request->host() + request->url());

    if (request->contentLength()) {
      Serial.println("_CONTENT_TYPE: " + request->contentType());
      Serial.println("_CONTENT_LENGTH: " + request->contentLength());
    }

    int headers = request->headers();
    int i;

    for (i = 0; i < headers; i++) {
      AsyncWebHeader *h = request->getHeader(i);
      Serial.println("_HEADER[" + h->name() + "]: " + h->value());
    }

    int params = request->params();

    for (i = 0; i < params; i++) {
      AsyncWebParameter *p = request->getParam(i);

      if (p->isFile()) {
        Serial.println("_FILE[" + p->name() + "]: " + p->value() + ", size: " + p->size());
      } else if (p->isPost()) {
        Serial.println("_POST[" + p->name() + "]: " + p->value());
      } else {
        Serial.println("_GET[" + p->name() + "]: " + p->value());
      }
    }

    request->send(404);
  });

  server.onFileUpload([](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
                         size_t len, bool final) {
    (void)request;

    if (!index)
      Serial.println("UploadStart: " + filename);

    Serial.print((const char *)data);

    if (final)
      Serial.println("UploadEnd: " + filename + "(" + String(index + len) + ")");
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    (void)request;

    if (!index)
      Serial.println("BodyStart: " + total);

    Serial.print((const char *)data);

    if (index + len == total)
      Serial.println("BodyEnd: " + total);
  });

  server.begin();

  //////

  Serial.print(F("HTTP server started @ "));
  Serial.println(WiFi.localIP());

  Serial.println(separatorLine);
  Serial.print("Open http://");
  Serial.print(WiFi.localIP());
  Serial.println("/edit to see the file browser");
  Serial.println("Using username = " + http_username + " and password = " + http_password);
  Serial.println(separatorLine);
}

unsigned long t_thermo = millis();
unsigned long t_display = millis() + 100;
unsigned long t_pid_on = millis() + 400;
unsigned long t_start_reflow = millis();
unsigned long t_reflow_pid = millis() + 300;
unsigned long t_reflow_control = millis() + 500;

unsigned long t_const_pid = millis() + 300;
unsigned long t_const_control = millis() + 500;
unsigned long t_rampup = millis() + 600;
unsigned long t_profile_counter = millis() + 800;
unsigned long t_reflow_finish = millis() + 900;

void loop() {
  if (digitalRead(BTN1_PIN) == 1) {
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);

    if (pages[current_page].page.is_value) {
      if (current_page == 1) {
        if (selected_profile > 0)
          *pages[current_page].page.value = *pages[current_page].page.value - 1.0;
      } else *pages[current_page].page.value = *pages[current_page].page.value - 5.0;
    }
    update_display();

    unsigned long t_btn_pressed = millis();
    int wait_time_increase = 300;
    bool set_process = false;

    while (digitalRead(BTN1_PIN) == 1) {
      if (millis() >= t_btn_pressed + wait_time_increase && !set_process) {
        wait_time_increase = 80;
        t_btn_pressed = millis();

        if (pages[current_page].page.is_value) {
          leds[0] = CRGB::Orange;
          FastLED.show();
          if (current_page == 1) {
            if (selected_profile > 0)
              *pages[current_page].page.value = *pages[current_page].page.value - 1.0;
          } else *pages[current_page].page.value = *pages[current_page].page.value - 5.0;
        } else {
          leds[0] = CRGB::Green;
          FastLED.show();
          if (current_page == 0) {
            running_reflow = !running_reflow;
            init_reflow = true;
            msg2display_top = running_reflow ? "Reflow" : "";
          } else if (current_page == 1) {
            running_const = !running_const;
            init_const = true;
            msg2display_top = running_const ? "Const" : "";
          }
          set_process = true;
        }
        update_display();
      }
      delay(20);
    }
    leds[0] = CRGB::Black;
    FastLED.show();
  } else if (digitalRead(BTN2_PIN) == 1) {
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);
    if (current_page == 10) {
      i2cscan();
      current_page = 0;
    }
    if (pages[current_page].page.is_value) {
      if (current_page == 1) {
        if (selected_profile < N_PROFILES - 1)
          *pages[current_page].page.value = *pages[current_page].page.value + 1.0;
      } else *pages[current_page].page.value = *pages[current_page].page.value + 5.0;
    }

    update_display();
    unsigned long t_btn_pressed = millis();
    int wait_time_increase = 300;
    bool set_process = false;

    while (digitalRead(BTN2_PIN) == 1) {
      if (millis() >= t_btn_pressed + wait_time_increase && !set_process) {
        leds[0] = CRGB::Orange;
        FastLED.show();

        wait_time_increase = 80;
        t_btn_pressed = millis();

        if (pages[current_page].page.is_value) {
          leds[0] = CRGB::Orange;
          FastLED.show();
          if (current_page == 1) {
            if (selected_profile < N_PROFILES - 1)
              *pages[current_page].page.value = *pages[current_page].page.value + 1.0;
          } else *pages[current_page].page.value = *pages[current_page].page.value + 5.0;
        } else {
          leds[0] = CRGB::Green;
          FastLED.show();
          if (current_page == 0) {
            running_reflow = !running_reflow;
            init_reflow = true;
            msg2display_top = running_reflow ? "Reflow" : "";
          } else if (current_page == 1) {
            running_const = !running_const;
            init_const = true;
            msg2display_top = running_const ? "Const" : "";
          }
          set_process = true;
        }
        update_display();
      }
      delay(20);
    }
    leds[0] = CRGB::Black;
    FastLED.show();
  } else if (digitalRead(BTN3_PIN) == 1) {
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);
    t_new_page = millis();
    new_page = true;
    current_page = (++current_page) % N_PAGES;
    msg2display_bot = "";
    update_display();

    unsigned long t_btn_pressed = millis();
    while (digitalRead(BTN3_PIN) == 1) {
      if (millis() >= t_btn_pressed + 800) {
        leds[0] = CRGB::Orange;
        FastLED.show();
      }
      delay(20);
    }
    leds[0] = CRGB::Black;
    FastLED.show();
  }

  if (init_reflow) {
    init_reflow = false;

    if (running_reflow) {
      t_start_reflow = millis();
      profile_counter = 0;
      preheat_done = false;
      reflow_done = false;
      calculate_profile_individual();
      pid_setpoint = temp_reflow_individual[0];

      pid_reflow.SetOutputLimits(0, 500);
      pid_reflow.SetMode(AUTOMATIC);
      leds[0] = CRGB::Yellow;
      FastLED.show();
    } else {
      msg2display_bot = "";
      digitalWrite(SSR1_PIN, LOW);
      leds[0] = CRGB::Black;
      FastLED.show();
    }
  }


  if (running_reflow && !preheat_done) {
    if (current_temperature1 < 30) {
      if (millis() >= t_reflow_control + 500) {
        t_reflow_control = millis();
        digitalWrite(SSR1_PIN, !digitalRead(SSR1_PIN));
        leds[0] = digitalRead(SSR1_PIN) ? CRGB::Blue : CRGB::Yellow;
        FastLED.show();
        t_pid_on = millis();
      }
    } else {
      preheat_done = true;
      t_profile_counter = millis();
    }
  }

  while (reflow_done) {
    if (millis() > t_reflow_finish + 1000) {
      t_reflow_finish = millis();
      buzzer_state = !buzzer_state;
      ledcWrite(PWMChannel, buzzer_state ? 500 : 0);
      leds[0] = CRGB::Green;
      FastLED.show();
      msg2display_bot = "Press any button!";
      update_display();
    }

    if (digitalRead(BTN1_PIN) == 1 || digitalRead(BTN2_PIN) == 1 || digitalRead(BTN3_PIN) == 1) {
      reflow_done = false;
      ledcWrite(PWMChannel, 0);
      leds[0] = CRGB::Black;
      FastLED.show();

      profile_counter = 0;
      pid_setpoint = temp_reflow_individual[0];

      while (digitalRead(BTN1_PIN) == 1 || digitalRead(BTN2_PIN) == 1 || digitalRead(BTN3_PIN) == 1) {
        delay(100);
      }
    }
  }

  if (running_reflow && preheat_done) {
    int pid_error = int(pid_setpoint) - int(current_temperature1);

    if (millis() >= t_reflow_pid + 200) {
      t_const_pid = millis();
      pid_setpoint = temp_reflow_individual[profile_counter];
      pid_input = current_temperature1;
      pid_reflow.Compute();
      msg2display_bot = String(pid_output, 0) + ", " + String(pid_setpoint, 0) + ", " + String(pid_error) + ", " + String(profile_counter) + "/300";  // + ", " + String(window_start_time);
    }

    if (millis() >= t_reflow_control + 500) {
      t_reflow_control = millis();

      if (pid_output > 50) {
        digitalWrite(SSR1_PIN, HIGH);
        leds[0] = CRGB::Red;
        FastLED.show();
        t_pid_on = millis();
      }
    }

    if (millis() > t_pid_on + pid_output && digitalRead(SSR1_PIN)) {
      digitalWrite(SSR1_PIN, LOW);
      leds[0] = CRGB::Yellow;
      FastLED.show();
    }

    if (millis() >= t_profile_counter + 1000) {
      t_profile_counter += 1000;
      profile_counter++;
      if (profile_counter > 300) {
        ledcWrite(PWMChannel, 500);
        msg2display_top = "";
        buzzer_state = true;
        running_reflow = false;
        t_reflow_finish = millis();
        reflow_done = true;
        digitalWrite(SSR1_PIN, LOW);
        leds[0] = CRGB::Yellow;
        FastLED.show();
      }
    }
  }

  if (init_const) {
    init_const = false;

    if (running_const) {
      pid_setpoint = temperature_setpoint_const;
      rampup_const = true;
      pid.SetOutputLimits(0, 1000);
      pid.SetMode(AUTOMATIC);
      leds[0] = CRGB::Yellow;
      FastLED.show();
    } else {
      msg2display_bot = "";
      digitalWrite(SSR1_PIN, LOW);
      leds[0] = CRGB::Black;
      FastLED.show();
    }
  }

  if (running_const) {
    int pid_error = int(pid_setpoint) - int(current_temperature1);

    if (pid_error < 5 && pid_error > -5 && rampup_const_close) {
      rampup_const_close = false;
      t_rampup = millis();
    }

    if (millis() >= t_const_pid + 200) {
      t_const_pid = millis();
      pid_input = current_temperature1;
      pid.Compute();
      msg2display_bot = String(pid_output, 0) + ", " + String(pid_error, 0) + ", " + String(rampup_const, 0);  // + ", " + String(window_start_time);
    }

    if (millis() >= t_const_control + 2000) {
      t_const_control = millis();

      if (rampup_const) {
        if (pid_error < 0) {
          rampup_const = false;
          pid.SetOutputLimits(0, 300);
          ledcWrite(PWMChannel, 500);
          delay(200);
          ledcWrite(PWMChannel, 0);
        } else if (pid_error < 10) {
          pid.SetOutputLimits(0, 150);
        } else if (pid_error < 20) {
          pid.SetOutputLimits(0, 300);
        }

        if (millis() > t_rampup + 10000 && !rampup_const_close) {
          rampup_const = false;
          pid.SetOutputLimits(0, 300);

          ledcWrite(PWMChannel, 500);
          delay(200);
          ledcWrite(PWMChannel, 0);
        }
      }

      if (pid_output > 50) {
        digitalWrite(SSR1_PIN, HIGH);
        leds[0] = CRGB::Red;
        FastLED.show();
        t_pid_on = millis();
      }
    }

    if (millis() > t_pid_on + pid_output && digitalRead(SSR1_PIN)) {
      digitalWrite(SSR1_PIN, LOW);
      leds[0] = CRGB::Yellow;
      FastLED.show();
    }
  }

  if (millis() >= t_display + 500) {
    t_display = millis();
    update_display();
  }

  if (millis() >= t_thermo + 200) {
    t_thermo = millis();
    current_temperature1 = thermocouple1.readCelsius();  //random(50, 300);//
    current_temperature2 = thermocouple2.readCelsius();  //random(50, 300);//thermocouple2.readCelsius();
    val1 = float(random(50, 300));
  }
  check_status();
  ArduinoOTA.handle();
}
