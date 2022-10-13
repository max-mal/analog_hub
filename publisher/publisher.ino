/*
  ESP-01 Firmware
  Requests ADC data from atmega8 MCU and publishes it to MQTT server
  API:
  
  GET / # Firmware update 
  GET /reset # Reset settings
  GET /analog # get ADC from atmega8
  GET /disable_sleep # disables sleep after data published
  GET /enable_sleep  # enables sleep after data published
  GET /set_sleep?period=<sleep period ms> # sets sleep period

  MQTT:
  analog_hub/voltage # ESP-01 voltage
  analog_hub/p<pin_number> # ADC readings
*/

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>

String buffer = "";

std::pair<int, int> serialReadAnalogItem();
std::vector<std::pair<int, int>> serialReadAnalogData();
void serialGoSleep();
bool serialCheckConnection();
bool serialToggleSleep(bool value);
bool serialSetSleepPeriod(int period);
bool serialSetDutyPeriods(int periods);


std::unique_ptr<ESP8266WebServer> server;
WiFiManager wifiManager;

bool sleep_disabled = false;

struct Settings {
  String server;
  String port;
  String token;
};

Settings settings = {
  "",
  "",
  "default_token",
};

bool shouldSaveConfig = false;
void saveConfigCallback () {  
  shouldSaveConfig = true;
}

bool shouldReset = false;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

long lastReconnectAttempt = 0;
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

ADC_MODE(ADC_VCC);


void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";
  for (uint8_t i = 0; i < server->args(); i++) {
    message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
  }
  server->send(404, "text/plain", message);
}

void initLittleFs() {
  if (!LittleFS.begin()) {    
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed");
    }
  }
}

bool writeFile(const char * path, const char * message) {
  File file = LittleFS.open(path, "w");
  if (!file) {    
    return false;
  }
  if (!file.print(message)) {
    file.close();
    return false;
  }

  file.close();
  return true;
}

String readFile(const char * path) {
  File file = LittleFS.open(path, "r");
  if (!file) {    
    return "";
  }
  String data = file.readString();
  file.close();
  return data;
}

void initWifiManager() {
  char mqtt_server[40];
  char mqtt_port[6] = "8080";
  char api_token[34] = "default_token";

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_api_token("apikey", "API token", api_token, 34);
  

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_api_token);
  
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);    
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(api_token, custom_api_token.getValue());

  if (shouldSaveConfig) {
    Serial.println("Writing settings...");
    writeFile("/config_server", mqtt_server);
    writeFile("/config_port", mqtt_port);
    writeFile("/config_key", api_token);
    settings.port = String(mqtt_port);
    settings.server = String(mqtt_server);
    settings.token = String(api_token);
  } else {
    settings.port = readFile("/config_port");
    settings.server = readFile("/config_server");
    settings.token = readFile("/config_key");
  }
}

void reportMqtt() {
  mqttClient.publish("analog_hub/voltage", String(ESP.getVcc() /1024.00f).c_str(), true);
  auto analogReadings = serialReadAnalogData();
  for (auto reading : analogReadings) {
    String topic = "analog_hub/p_" + String(reading.first);
    mqttClient.publish(topic.c_str(), String(reading.second).c_str(), true);
  }
}

bool reconnectMqtt() {  
  Serial.print("Attempting MQTT connection...");  
  String clientId = "analog-hub";      
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("connected");      
    reportMqtt();
    return true;
  } 

  return false;
}

bool isAuthenticated() {
  return server->authenticate("admin", settings.token.c_str());
}

void setup() {
  Serial.begin(9600);  
  initLittleFs();
  initWifiManager();

  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + settings.server);
  Serial.println("\tmqtt_port : " + settings.port);
  Serial.println("\tapi_token : " + settings.token);
  
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/reset", []() {
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }    
    server->send(200, "text/plain", "Resetting settings. MCU shoud restart");
    shouldReset = true;    
  });

  server->on("/analog", []() {
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }    
    auto readings = serialReadAnalogData();
    String response = "";
    for (auto pair : readings)  {
      response += "pin " + String(pair.first) + ": " + String(pair.second) + "<br>";
    }
    server->send(200, "text/plain", response + " \nbuffer: " + buffer);
  });

  server->on("/set_sleep", []() {
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }    
    int period = String(server->arg(0)).toInt();
    if (period <= 0) {
      server->send(200, "text/plain", "Wrong period!");
      return;
    }
    bool result = serialSetSleepPeriod(period);
    server->send(200, "text/plain", "result = " + String(result));
  });

  server->on("/disable_sleep", []() { 
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }       
    auto result = serialToggleSleep(false);
    sleep_disabled = true;
    server->send(200, "text/plain", "result = " + String(result));
  });

  server->on("/enable_sleep", []() {    
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }    
    auto result = serialToggleSleep(true);
    sleep_disabled = false;
    server->send(200, "text/plain", "result = " + String(result));
  });

  server->on("/sleep", []() {    
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    }        
    server->send(200, "text/plain", "going sleep");
    serialGoSleep();
  });

  server->on("/", HTTP_GET, []() {
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    } 
    server->sendHeader("Connection", "close");
    server->send(200, "text/html", serverIndex);
  });
  server->on("/update", HTTP_POST, []() {
    if (!isAuthenticated()) {
      return server->requestAuthentication();
    } 
    server->sendHeader("Connection", "close");
    server->send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });

  server->onNotFound(handleNotFound);

  server->begin();
  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());

  mqttClient.setServer(settings.server.c_str(), settings.port.toInt());
  lastReconnectAttempt = 0;

}

long lastMqttReport = 0;
int reportsCount = 0;
void loop() {
  // put your main code here, to run repeatedly:
  server->handleClient();
  
  long now = millis();

  if (!mqttClient.connected()) {        
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnectMqtt()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient.loop();
    if (now - lastMqttReport > 5000) {
      reportMqtt();
      lastMqttReport = now;
      ++reportsCount;
    }
  }
  

  if (shouldReset) {
    wifiManager.resetSettings();
    ESP.restart();
  }

  if (!sleep_disabled && reportsCount > 5) {
    serialGoSleep();
    reportsCount = 0;
  }
}

