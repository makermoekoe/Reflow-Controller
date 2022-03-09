#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <max6675.h>
#include <Smoothed.h>
#include <Preferences.h>
#include <PID_v1.h>

#define SDA_PIN         33
#define SCL_PIN         35
#define APA102_SDI_PIN  38
#define APA102_CLK_PIN  37
#define OLED_RST_PIN    45
#define CS_MAX1_PIN     13
#define CS_MAX2_PIN     12
#define MISO_PIN        9
#define MOSI_PIN        11
#define CLK_PIN         7

#define MOS_CTRL1_PIN   5
#define MOS_CTRL2_PIN   6
#define SSR1_PIN        1
#define SSR2_PIN        2
#define SERVO_PIN       3
#define BUZZER_PIN      15
#define BTN1_PIN        36
#define BTN2_PIN        39
#define BTN3_PIN        40

#define NUM_LEDS        1
CRGB leds[NUM_LEDS];

#define WIDTH 128
#define HEIGHT 64
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ OLED_RST_PIN, /* clock=*/ SCL_PIN, /* data=*/ SDA_PIN);

double Setpoint, Input, Output;
double Kp = 5, Ki = 0.02, Kd = 0.15;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

int dutyCycle = 0;

const int PWMFreq = 20000;                                //50hz
const int PWMChannel = 0;
const int PWMResolution = 10;                       //12 bits 0-4095


MAX6675 thermocouple1(CLK_PIN, CS_MAX2_PIN, MISO_PIN);
MAX6675 thermocouple2(CLK_PIN, CS_MAX1_PIN, MISO_PIN);
float current_temperature1 = 0.0;
float current_temperature2 = 0.0;

float temperature_setpoint = 0.0;
float temperature_setpoint_const = 50.0;

float temperature_reflow = 200.0;
float temperature_soak = 130.0;

bool start_reflow = false;
bool start_const = false;


// Set used fonts with width and height of each character
struct fonts {
  const uint8_t* font;
  int width;
  int height;
};

fonts font_xl = {u8g2_font_profont29_mf, 16, 19};
fonts font_m = {u8g2_font_profont17_mf, 9, 11};
fonts font_s = {u8g2_font_profont12_mf, 6, 8};
fonts font_xs = {u8g2_font_profont10_mf, 5, 6};

// Custom type to set up a page on the OLED display. Baisc structure is a page of three fields.
// A top one with the power, a left one with the voltage and a right one with the current.
struct display_page {
  String title;
  float *value;
  String msg;
  bool is_value;
};

float val1 = 0.0;
float placeholder = 0.0;
String msg2display_top = "";
String msg2display_bot = "";

display_page page_start_reflow = {"Start Reflow", &placeholder, "Start Reflow", false};
display_page page_const_temp = {"Const Temp", &temperature_setpoint_const, "", true};
display_page page_peak_temp = {"Peak Temp", &temperature_reflow, "", true};
display_page page_soak_temp = {"Soak Temp", &temperature_soak, "", true};
display_page page_start_const = {"Start Const", &placeholder, "Start Const", false};

// Menu and Pages
#define N_PAGES 5
int current_page = 0;
bool new_page = true;
unsigned long t_new_page = 0;

struct page {
  int p;
  display_page &page;
};

page pages[N_PAGES] = {
  {0, page_start_reflow},
  {1, page_const_temp},
  {2, page_peak_temp},
  {3, page_soak_temp},
  {4, page_start_const},
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
    if (fill) u8g2.drawRBox(left + frame, top + frame, width - (2*frame), height - (2*frame), radius);
    else u8g2.drawRFrame(left, top, width, height, radius);
  }

  void print_text(String str, fonts f, box_alignment align, float pos) {
    u8g2.setFont(f.font);
    int x = 0;
    if(align == Center) x = left + 0.5 * (width - (f.width * str.length() - 1));
    else if(align == Left) x = left + 5;
    else if(align == Right) x = left + width - f.width * str.length() - 5;
    u8g2.setCursor(x, top + pos * height + f.height / 2);
    u8g2.print(str);
  }
};

// Initialize the boxes for the OLED
box top_left = {0, 0, 36, 10, 0, false};
box top = {top_left.width, 0, 56, 10, 0, false};
box top_right = {top.left + top.width, 0, top_left.width, 10, 0, false};
box mid_main = {0, top.height, 128, 34, 0, false};
box bot = {0, mid_main.top + mid_main.height, 128, 10, 0, false};

// Update the OLED display with the three boxes according to the given pages
void update_display(){
  u8g2.clearBuffer();

  // top.draw(false);
  // top_left.draw(false);
  // top_right.draw(false);
  mid_main.draw(false);
  // bot.draw(false);

  if(new_page){
    if(millis() <= t_new_page + 800){
      mid_main.print_text(pages[current_page].page.title, font_m, Center, 0.5);
    }
    else{
      new_page = false;
    }
  }

  if(!new_page){
    top_left.print_text(String(current_temperature1, 0) + "C", font_xs, Center, 0.5);
    top_right.print_text(String(current_temperature2, 0) + "C", font_xs, Center, 0.5);
    bot.print_text(msg2display_bot, font_xs, Center, 0.5);

    if(!pages[current_page].page.is_value) mid_main.print_text(pages[current_page].page.msg, font_s, Center, 0.5);
    else mid_main.print_text(String(*pages[current_page].page.value, 1) + "C", font_s, Center, 0.5);

    if(false){ //reflow running
      top.print_text(msg2display_top, font_xs, Center, 0.5);
    }
    else{
      top.print_text(msg2display_top, font_xs, Center, 0.5);
    }
  }
  u8g2.sendBuffer();
}



void setup() {
  Serial.begin(115200);
  Serial.println("start");

  pinMode(BTN1_PIN, INPUT);
  pinMode(BTN2_PIN, INPUT);
  pinMode(BTN3_PIN, INPUT);

  pinMode(CS_MAX1_PIN, OUTPUT);
  pinMode(CS_MAX2_PIN, OUTPUT);
  digitalWrite(CS_MAX1_PIN, LOW);
  digitalWrite(CS_MAX2_PIN, LOW);
  

  delay(100);
  FastLED.addLeds<APA102, APA102_SDI_PIN, APA102_CLK_PIN, BGR>(leds, NUM_LEDS);
  FastLED.setBrightness(100);
  delay(100);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  ledcSetup(PWMChannel, PWMFreq, PWMResolution);
  ledcAttachPin(BUZZER_PIN, PWMChannel);
  ledcWrite(PWMChannel, dutyCycle);

  ledcWrite(PWMChannel, 500);
  delay(200);
  ledcWrite(PWMChannel, 0); 


  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  u8g2.begin();
  delay(100);
  u8g2.clearBuffer();
  mid_main.draw(false);
  u8g2.sendBuffer();
}

unsigned long t_thermo = millis();
unsigned long t_display = millis() + 100;

void loop() {
  if(digitalRead(BTN1_PIN) == 1){
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);

    if(pages[current_page].page.is_value){
      *pages[current_page].page.value = *pages[current_page].page.value - 5.0;
    }
    update_display();

    unsigned long t_btn_pressed = millis();
    int wait_time_increase = 300;
    bool set_process = false;
    while (digitalRead(BTN1_PIN) == 1){
      if(millis() >= t_btn_pressed + wait_time_increase && !set_process){
        wait_time_increase = 80;
        t_btn_pressed = millis();

        if(pages[current_page].page.is_value){
          leds[0] = CRGB::Orange;
          FastLED.show();
          *pages[current_page].page.value = *pages[current_page].page.value - 5.0;
        }
        else{
          leds[0] = CRGB::Green;
          FastLED.show();
          if(current_page == 0) {
            start_reflow = !start_reflow;
            msg2display_top = start_reflow ? "Reflow" : "";
          }
          else{
           start_const = !start_const;
           msg2display_top = start_const ? "Const" : "";
          }
          set_process = true;
        }
        update_display();
      }
      delay(20);
    } 
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  else if(digitalRead(BTN2_PIN) == 1){
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);

    if(pages[current_page].page.is_value){
      *pages[current_page].page.value = *pages[current_page].page.value + 5.0;
    }

    update_display();
    unsigned long t_btn_pressed = millis();
    int wait_time_increase = 300;
    bool set_process = false;
    while (digitalRead(BTN2_PIN) == 1){
      if(millis() >= t_btn_pressed + wait_time_increase && !set_process){
        leds[0] = CRGB::Orange;
        FastLED.show();

        wait_time_increase = 80;
        t_btn_pressed = millis();

        if(pages[current_page].page.is_value){
          *pages[current_page].page.value = *pages[current_page].page.value + 5.0;
        }
        else{
          leds[0] = CRGB::Green;
          FastLED.show();
          if(current_page == 0) {
            start_reflow = !start_reflow;
            msg2display_top = start_reflow ? "Reflow" : "";
          }
          else{
            start_const = !start_const;
            msg2display_top = start_const ? "Const" : "";
          }
          set_process = true;
        }
        update_display();
      }
      delay(20);
    } 
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  else if(digitalRead(BTN3_PIN) == 1){
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(50);
    t_new_page = millis();
    new_page = true;
    current_page = (++current_page) % N_PAGES;
    update_display();

    unsigned long t_btn_pressed = millis();
    while (digitalRead(BTN3_PIN) == 1){
      if(millis() >= t_btn_pressed + 800){
        leds[0] = CRGB::Orange;
        FastLED.show();
      }
      delay(20);
    } 
    leds[0] = CRGB::Black;
    FastLED.show();
  }


  if(millis() >= t_display + 500){
    t_display = millis();
    update_display();
  }

  
  if(millis() >= t_thermo + 500){
    t_thermo = millis();
    current_temperature1 = random(50, 300);//thermocouple1.readCelsius();
    current_temperature2 = random(50, 300);//thermocouple2.readCelsius();
    val1 = float(random(50,300));
  }
}