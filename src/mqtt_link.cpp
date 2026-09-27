#include "mqtt_link.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String mqttClientID = "HubESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);

String topicRelay1Set = "evhub/relay/1/set";
String topicRelay2Set = "evhub/relay/2/set";
String topicRelay1State = "evhub/relay/1/state";
String topicRelay2State = "evhub/relay/2/state";

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  // RÖLE 1
  // NOT: Sunucu kartında fiziksel röle pin bağlantısı yoktur;
  //      röle kontrolü ESP01 cihazlara HTTP ile iletilir.
  //      Bu callback lokal LED/test amaçlıdır.
  if (String(topic) == topicRelay1Set) {
    if (msg == "1" || msg == "ON") {
      // Relay 1 -> HTTP üzerinden gönder (lokal pin yok)
      mqttClient.publish(topicRelay1State.c_str(), "1", true);
      Serial.println("[MQTT] Relay1 ON komutu alındı");
    } else if (msg == "0" || msg == "OFF") {
      mqttClient.publish(topicRelay1State.c_str(), "0", true);
      Serial.println("[MQTT] Relay1 OFF komutu alındı");
    }
  }

  // RÖLE 2
  if (String(topic) == topicRelay2Set) {
    if (msg == "1" || msg == "ON") {
      mqttClient.publish(topicRelay2State.c_str(), "1", true);
      Serial.println("[MQTT] Relay2 ON komutu alındı");
    } else if (msg == "0" || msg == "OFF") {
      mqttClient.publish(topicRelay2State.c_str(), "0", true);
      Serial.println("[MQTT] Relay2 OFF komutu alındı");
    }
  }
}

void reconnectMQTT() {
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 3000) return;
  lastTry = millis();

  if (mqttClient.connect(mqttClientID.c_str())) {
    mqttClient.subscribe(topicRelay1Set.c_str());
    mqttClient.subscribe(topicRelay2Set.c_str());
  }
}

void mqttSetup() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
}

void mqttLoop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  } else {
    mqttClient.loop();  // Sadece bagliysa loop calissin
  }
}
