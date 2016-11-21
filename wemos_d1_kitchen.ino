#include <Arduino.h>
#include <Scheduler.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

int dim1_gpio = 4;
int pir_gpio = 5;
int calibrationTime = 30;

const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
const char* outTopic = "/room/kitchen/wemos1/out";
const char* topic_dim = "/room/kitchen/wemos1/dim";
const char* topic_timer = "/room/kitchen/wemos1/timer";

struct stats {
  int timer;
  bool detect;
  int c_dim;
  int n_dim;
  int dim_speed;
  bool light;
};
stats settings = { 0, false, 0 ,0 ,3, false };

void callback(char* topic, byte* payload, unsigned int length) {
  char mqttIn;
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  char* json;
  json = (char*) malloc(length + 1);
  memcpy(json, payload, length);
  json[length] = '\0';
  int value = atoi( json );
  free(json);
  if(strcmp(topic, "/room/kitchen/wemos1/dim") == 0) {
    if(0 <= value <= 100) {
      settings.n_dim = value * 10;
      Serial.println("dimming to ");
      Serial.print(value * 10);
    }    
  } else if (strcmp(topic, "/room/kitchen/wemos1/timer") == 0) {
    if(0 <= value <= 240) {
      settings.timer = value*60*1000;
      Serial.println("timer set to");
      Serial.print(value*60*1000);
    }
  } else { Serial.println("No valid topic"); }
}

class Timer : public Task {
protected:
    void loop() {
      //sanity for negative numbers
      if (settings.timer < 0) settings.timer = 0;
      
      if(settings.timer != 0){
        //start counting
        delay(1000);
        settings.timer -= 1000;
        Serial.print("timer count secs left:");
        Serial.println(settings.timer/1000);
        
      } else if(settings.timer == 0 && settings.light && !settings.detect) {
        settings.n_dim = 0;
      } else {
        delay(1000);
      }
    }
private:
    int start_time;
    bool start_clock = false;
} timer_task;

class Detector : public Task {
protected:
    void setup() {
        pinMode(pir_gpio, INPUT);
        digitalWrite(pir_gpio, LOW);
        //give the sensor some time to calibrate
        Serial.println("Calibrating sensor for 30 sec");
          for(int i = 0; i < calibrationTime; i++){
            delay(1000);
            }
        Serial.println("SENSOR ACTIVE");
    }
    void loop(){
      if(digitalRead(pir_gpio) == HIGH){
        settings.detect = true;
        if(settings.timer == 0){
          //set default timer 5min
          settings.timer = 5*60*1000;
        } else if(settings.timer < (3*60*1000)){
          //set 3mins for repeated events when below 3min
          settings.timer = 3*60*1000;
        } else {
          delay(100);
        }
        if(!settings.light){
            settings.n_dim = 1000;
        }
    }
      if(digitalRead(pir_gpio) == LOW){
        settings.detect = false;
        delay(100);
      }
    }

private:
    int default_timer = 10;
} detector_task;

class Dimmer1 : public Task {
protected:
    void setup() {
        state = LOW;
        pinMode(dim1_gpio, OUTPUT);
        pinMode(dim1_gpio, state);
    }

    void loop() {
        delay(10);
        // limit to 10bit (0-~1000)  
           
        if(settings.c_dim == 0){
          settings.light = false;
        } else {
          settings.light = true;
        }
        //how to handle dimming without timer from mqtt?
        //if(settings.timer != 0) {
          while(settings.c_dim != settings.n_dim){
            delay(settings.dim_speed);
            if(settings.n_dim > settings.c_dim){
              settings.c_dim += 1; 
            } else if(settings.n_dim < settings.c_dim) {
              settings.c_dim -= 1;
            }
          analogWrite(dim1_gpio, settings.c_dim);
          }
        //}
    }

private:
    uint8_t state;
    int duty;
} dimmer1_task;

class OTA : public Task {
protected:
    void setup() {
      //10 sec initial delay to let mqtt connect to wifi
      delay(10000);
      while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
      }
      ArduinoOTA.setHostname("Kitchen_wemos_1");
      ArduinoOTA.onStart([]() {
        Serial.println("Arduino OTA Start");
      });
      ArduinoOTA.onEnd([]() {
        Serial.println("Arduino OTA End");
      });
      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      });
      ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });
      ArduinoOTA.begin();
      Serial.println("OTA Ready!");

    }
    void loop(){
      ArduinoOTA.handle();
    }
} ota_task;

class MQTT : public Task {
public:

void setup_wifi() {

  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    for(int i = 0; i<500; i++){
      delay(1);
    }
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      client.subscribe(topic_timer);
      client.subscribe(topic_dim);
      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      for(int i = 0; i<5000; i++){
        delay(1);
      }
    }
  }
}
  void setup() {
      setup_wifi();
      client.setServer(mqtt_server, 1883);
      client.setCallback(callback);
    }
    void loop() {
      if (!client.connected()) {
        reconnect();
      }
      client.loop();
    }
} mqtt_task;


void setup() {
    Serial.begin(115200);
    Serial.println("Starting Scheduler");
    delay(100);
    Scheduler.start(&timer_task);
    Scheduler.start(&dimmer1_task);
    Scheduler.start(&mqtt_task);
    Scheduler.start(&detector_task);
    Scheduler.start(&ota_task);

    Scheduler.begin();
}

void loop() {}
