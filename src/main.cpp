#include <Arduino.h>

#include <ArduinoOTA.h>
#include <ESPDateTime.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <StringSplitter.h>

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);

const char *mqtt_server = "mqtt2.mianos.com";
const char* ntpServer = "ntp.mianos.com";

const char *dname = "pwr";

static volatile int loops;
uint8_t GPIO_Pin = D1;

static const int ipins[] = {D2, D7, D5, D6};
const int ipins_size = (sizeof (ipins) / sizeof (int));

static volatile int cycle_time = 0;

IRAM_ATTR void zcisr() {
  // 5 per uSec so 200 per mS
  // so 100ms is 100 * 200

  // notes:
  // 100hz, two waves
  // 10mS of time
  // 100 divisions?
  // 500 divisions? then simple 0-500 for the timer
  // 
  if (!cycle_time) {
    return;
  }
  for (auto ii = 0; ii < ipins_size; ii++) {
    digitalWrite(ipins[ii], HIGH);
  }
  timer1_write(cycle_time * 50); // (2500000 / 5) ticks per us from TIM_DIV16 == 500,000 us interval  
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
  auto splitter = StringSplitter(topic, '/', 4); // new StringSplitter(string_to_split, delimiter, limit)
  int itemCount = splitter.getItemCount();
  if (itemCount < 3) {
    Serial.printf("Item count less than 3 %d '%s'", itemCount, topic_str);
    return;
  }
  // cmnd/pwr/  {"power": 0-1000}
  if (splitter.getItemAtIndex(0) == "cmnd") {
    DynamicJsonDocument jpl(1024);
    auto err = deserializeJson(jpl, payload, length);
    if (err) {
      Serial.printf("deserializeJson() failed: '%s'\n", err.c_str());
    } else {
      String output;
      serializeJson(jpl, output);
      Serial.printf("payload '%s'\n", output.c_str());
    }
    auto dest = splitter.getItemAtIndex(2);
    Serial.printf("dest '%s'\n", dest.c_str());
    if (dest == "power") {
      if (jpl.containsKey("duty")) {
        auto duty = jpl["duty"].as<unsigned int>();
        Serial.printf("Duty cycle set to %d\n", duty);
        if (duty < 0 || duty > 1000) {
          Serial.printf("Invalid duty time %d\n", duty);  // TODO: maybe publish?
          return;
        }
        cycle_time = duty;
      }
    }
  }
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    // Create a random client ID
    String clientId = dname;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      // set date
      DateTime.begin(/* timeout param */);
      if (!DateTime.isTimeValid()) {
        Serial.println("Failed to get time from server.");
      } 

      // subscribe to commands for this device
      String cmnd_topic = String("cmnd/") + dname + "/#";
      client.subscribe(cmnd_topic.c_str());

      // publish an init with the existing state, if any
      StaticJsonDocument<200> doc;
      doc["version"] = 1;
      doc["time"] = DateTime.toISOString();
      doc["duty"] = cycle_time;

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

  auto res = wifiManager.autoConnect("wifiPortal");
  if (!res) {
    Serial.printf("Failed to connect");
  }

  DateTime.setTimeZone("AEST-10AEDT,M10.1.0,M4.1.0/3");
  DateTime.setServer(ntpServer);
  tzset();

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

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

void publish_count() {
    if (client.connected()) {
      StaticJsonDocument<200> doc;
      doc["version"] = 1;
      doc["time"] = DateTime.toISOString();
      doc["loops"] = loops;
      doc["duty"] = cycle_time;

      String status_topic = "tele/" + String(dname) + "/power";
      String output;
      serializeJson(doc, output);
      client.publish(status_topic.c_str(), output.c_str());
    }
}

void loop() {
  ArduinoOTA.handle();
  Serial.printf("counts %d\n", loops);
  publish_count();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(1000);
}
