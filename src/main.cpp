#include <Arduino.h>

#include <ESPDateTime.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);

const char *mqtt_server = "mqtt2.mianos.com";
const char* ntpServer = "ntp.mianos.com";

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

void callback(char *topic_str, byte *payload, unsigned int length) {
  auto topic = String(topic_str);
  Serial.printf("Message arrived, topic '%s'\n", topic_str);
}

const char *dname = "pwr";

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = dname;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {

      String cmnd_topic = String("cmnd/") + dname + "/#";
      client.subscribe(cmnd_topic.c_str());

      DateTime.begin(/* timeout param */);
      if (!DateTime.isTimeValid()) {
        Serial.println("Failed to get time from server.");
      } 
      Serial.println("connected");
      // Once connected, publish an announcement...
      StaticJsonDocument<200> doc;
      doc["version"] = 1;
      doc["time"] = DateTime.toISOString();

      String status_topic = "tele/" + String(dname) + "/init";
      String output;
      serializeJson(doc, output);
      client.publish(status_topic.c_str(), output.c_str());
      Serial.printf("/init %s\n", output.c_str());
 
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  Serial.begin(115200);

  auto res = wifiManager.autoConnect("portal");
  if (!res) {
    Serial.printf("Failed to connect");
  }

  DateTime.setTimeZone("AEST-10AEDT,M10.1.0,M4.1.0/3");
  DateTime.setServer(ntpServer);
  DateTime.begin(15 * 1000);
  tzset();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

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
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(1000);
}
