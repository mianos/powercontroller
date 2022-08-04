#include <Arduino.h>

#include <ESPDateTime.h>
#include <WiFiManager.h>

WiFiManager wifiManager;

static volatile int loops;
uint8_t GPIO_Pin = D1;

static const int ipins[] = {D2, D7, D5, D6};
const int ipins_size = (sizeof (ipins) / sizeof (int));

IRAM_ATTR void zcisr() {
  // 5 per uSec so 200 per mS
  // so 100ms is 100 * 200

  // notes:
  // 100hz, two waves
  // 10mS of time
  // 100 divisions?
  // 500 divisions? then simple 0-500 for the timer
  // 
  for (auto ii = 0; ii < ipins_size; ii++) {
    digitalWrite(ipins[ii], HIGH);
  }
  timer1_write(100 * 200); // (2500000 / 5) ticks per us from TIM_DIV16 == 500,000 us interval  
}

void IRAM_ATTR onTimerISR() {
  for (auto ii = 0; ii < ipins_size; ii++) {
    digitalWrite(ipins[ii], LOW);
  }
  loops++;
}

void setup() {
  Serial.begin(115200);
  DateTime.setTimeZone("AEST-10AEDT,M10.1.0,M4.1.0/3");
  tzset();

  auto res = wifiManager.autoConnect("portal");
  if (!res) {
    Serial.printf("Failed to connect");
  }
  pinMode(GPIO_Pin, INPUT_PULLUP);
  for (auto ii = 0; ii < ipins_size; ii++) {
    pinMode(ipins[ii], OUTPUT);
  }
  attachInterrupt(digitalPinToInterrupt(GPIO_Pin), zcisr, RISING);
  //Initialize Ticker every 0.5s
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  /* Dividers:
  TIM_DIV1 = 0,   //80MHz (80 ticks/us - 104857.588 us max)
  TIM_DIV16 = 1,  //5MHz (5 ticks/us - 1677721.4 us max)
  TIM_DIV256 = 3  //312.5Khz (1 tick = 3.2us - 26843542.4 us max)
  Reloads:
  TIM_SINGLE	0 //on interrupt routine you need to write a new value to start the timer again
  TIM_LOOP	1 //on interrupt the counter will start with the same value again
  */

}

void loop() {
  Serial.printf("counts %d\n", loops);
  delay(1000);
  // put your main code here, to run repeatedly:
}
