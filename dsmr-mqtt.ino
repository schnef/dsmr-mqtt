// -*- mode: c++ -*-

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "credentials.h"

/**
 * The next three defines are used to define the constatnts used
 * in for inverting the ESP8266's hardware UART. The register
 * base address, the UART control register and the invert bit 
 * are defined.
 */
// See: https://github.com/esp8266/Arduino/issues/4896
#define ESP8266_REG(addr) *((volatile uint32_t *)(0x60000000+(addr)))
#define U0C0        ESP8266_REG(0x020) //CONF0
#define UCRXI       19 //Invert RX

#define LED_RED     0
#define LED_BLUE    2

#define P1_DATA_REQ 4
#define BAUD_RATE   115200
#define OFF         HIGH
#define ON          LOW

#ifndef STASSID
#define STASSID     "XXXX"
#define STAPSK      "XXXX"
#endif

#ifndef MQTT_USER
#define MQTT_USER   "YYYY"
#define MQTT_PASSWD "YYYY"
#define MQTT_SERVER "FQDN or IP address"
#define MQTT_FP     "F7 ... 31"
#endif
#define MQTT_PORT   8883
#define CLIENT_ID   "dsmr"
#define INTERVAL    2000
#define MQTT_BUFSIZE 1450

/*
 * The dsmr telegram may contain a maximal 1024 bytes payload in case of a firmware update. 
 * Allow for a maximum of 1280 bytes per telegram.
 */
#define BUFSIZE     1280
#define CRCSIZE     4

char buffer[BUFSIZE];
char crc[] = "1234";

// ============================ Wifi ============================

const char* ssid = STASSID;
const char* password = STAPSK;

// Initialise the WiFi Client object
WiFiClientSecure wifiClient;

// ============================ TLS ============================

const char fingerprint[] PROGMEM = MQTT_FP;

// ============================ MQTT ============================

const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = MQTT_PORT;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWD;
const char* client_id = CLIENT_ID;
const char* dsmr_topic = "dsmr";

// Initialise the MQTT Client object
PubSubClient pubsubClient(wifiClient); 

// For (re)connecting, a non-blocking wait is used.
unsigned long previousMillis = 0;
unsigned long interval = INTERVAL;

// ============================ Setup ============================

void setup() {
  // setup LEDs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(P1_DATA_REQ, OUTPUT);
  digitalWrite(LED_RED, OFF);
  digitalWrite(LED_BLUE, ON);
  digitalWrite(P1_DATA_REQ, LOW);
  
  // setup Serial. Invert receive (RX)
  Serial.begin(BAUD_RATE, SERIAL_8N1);
  U0C0 |= BIT(UCRXI); // Inverse RX

  // setup the Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(5000);
    ESP.restart();
  }

  // Setup TLS fingerprint
  wifiClient.setFingerprint(fingerprint);
  // wifiClient.setInsecure();

  // Set MQTT broker, port to use and the internal send/receive buffer size
  pubsubClient.setServer(mqtt_server, mqtt_port);
  pubsubClient.setBufferSize(MQTT_BUFSIZE);
  
  // setup the OTA
  ArduinoOTA.onStart([]() {
    digitalWrite(LED_BLUE, ON);
    digitalWrite(LED_RED, OFF);
    digitalWrite(P1_DATA_REQ, LOW);
    pubsubClient.disconnect();
  });
  ArduinoOTA.onEnd([]() {
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  });
  ArduinoOTA.onError([](ota_error_t error) {
  });
  ArduinoOTA.begin();

  // Enable reading
  digitalWrite(P1_DATA_REQ, HIGH);

  digitalWrite(LED_RED, ON);
  digitalWrite(LED_BLUE, OFF);
}

// ============================ Loop ============================

void loop() {

  char c;
  size_t n, m;
  uint16_t crc1, crc2;
  unsigned long currentMillis;
  
  ArduinoOTA.handle();

  if (!pubsubClient.connected()) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      reconnect();
      previousMillis = currentMillis;
    }
  } else {
    pubsubClient.loop();
    if (Serial.available() > 0) {
      c = Serial.read();
      if (c == '/') {
        digitalWrite(LED_BLUE, ON);
        n = Serial.readBytesUntil('!', buffer, BUFSIZE);
        yield(); // TODO: is this needed?
        if (n < BUFSIZE) {
          // The '!' was consumed, so continue with the CRC itself
          buffer[n] = '!';
          m = Serial.readBytes(crc, CRCSIZE);
          crc1 = strtol(crc, NULL, 16);
          crc2 = crc16(buffer, n + 1);
          if (crc1 == crc2) {
            pubsubClient.publish(dsmr_topic, buffer, n);
          } else {
            pubsubClient.publish(dsmr_topic, "ERRCRC");
          }
        } else {
          pubsubClient.publish(dsmr_topic, "ERROVR");
        }
        digitalWrite(LED_BLUE, OFF);
      }
    }
  }
}

/*
 * Start by making a secure tcp connection with the server running the mqtt broker.
 * If that worked, start the mqtt client.
 */
void reconnect() {
  // Attempt to connect to the mqtt broker
  if (pubsubClient.connect(client_id, mqtt_user, mqtt_password)) {
    // Once connected, publish an announcement...
    pubsubClient.publish(dsmr_topic, "CONNECTED");
  }
}

/*
 * Calculate crc16-ibm value. This version uses a lookup table,
 * processing the buffer character by character.
 * NB: The calculation starts off with 0xDC41 for the initial CRC 
 * value, which is the crc value for the '/' character which is not
 * included in the buffer but must be accounted for.
 */
 /*
const uint16_t crc16ibm[] =
  {0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
   0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
   0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
   0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
   0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
   0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
   0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
   0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
   0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
   0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
   0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
   0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
   0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
   0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
   0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
   0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
   0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
   0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
   0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
   0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
   0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
   0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
   0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
   0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
   0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
   0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
   0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
   0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
   0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
   0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
   0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
   0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
  };

uint16_t crc16(char buffer[], size_t len) {
  size_t n;
  char c;
  uint16_t crc = 0xDC41;

  for (size_t i = 0; i < len; i++) {
    c = buffer[i];
    n = (crc ^ c) & 0xFF;
    crc = (crc >> 8) ^ crc16ibm[n];
  }
  return crc;
}

/*
 * Alternative CRC16-ibm implementation which processes the buffer bit by bit
 */
uint16_t crc16(char buffer[], size_t len) {
  size_t n;
  char c;
  uint16_t crc = 0xDC41; // crc of char '/'

  for (size_t i = 0; i < len; i++) {
    c = buffer[i];
    crc ^= c;
    for (int j = 1; j <= 8; j++) {
      if ((crc & 0x0001) == 1) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}
