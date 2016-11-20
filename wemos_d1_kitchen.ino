
#include <Arduino.h>
#include <Scheduler.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

int dim1_gpio = 4;
int pir_gpio = 5;
int calibrationTime = 30;

//from esp8266_mix
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
const char* outTopic = "/room/kitchen/wemos1/out";
const char* inTopic = "/room/kitchen/wemos1/in";


struct stats {
  int timer;
  bool detect;
  int c_dim;
  int n_dim;
  int dim_speed;
  bool light;
};
stats settings = { 0, false, 0 ,0 ,1, false };

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

//Begin!
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
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      client.subscribe(inTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
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

    Scheduler.begin();
}

void loop() {}
