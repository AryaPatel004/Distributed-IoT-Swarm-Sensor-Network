#include <Arduino.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#undef DEBUG

char ssid[] = "No";
char pass[] = "idontknowcall911";
const int MY_ESP_ID = 3; // Use 1, 2, or 3 for each ESP
/*
MY_ESP_ID = 1 (ESP with COM5) (RED)
MY_ESP_ID = 2 (ESP with COM7) (GREEN)
MY_ESP_ID = 3 (ESP with COM14) (BLUE)
*/
const int PWM_Pin = 23; 
const int analog_read_pin = 34;
const int LED_builtin = 2;
const int RESOLUTION = 12;
int pwmInterval = 0;
int barInterval = 0;
const int ledPins[] = {13, 14, 27, 26, 25, 33, 32, 18, 19, 21}; //for LED Bar graph
const int numLeds = 10;

#define VERSIONNUMBER 28
#define SWARMSIZE 5
#define SWARMTOOOLD 30000

int mySwarmID = 0;

unsigned long mainLoopPreviousMillis = 0;
const long MAIN_LOOP_INTERVAL = 50;

StaticJsonDocument<128> jsonDoc; 
char jsonPacket[128];           

// Packet Types
#define LIGHT_UPDATE_PACKET 0
#define RESET_SWARM_PACKET 1
#define CHANGE_TEST_PACKET 2
#define RESET_ME_PACKET 3
#define DEFINE_SERVER_LOGGER_PACKET 4
#define LOG_TO_SERVER_PACKET 5
#define MASTER_CHANGE_PACKET 6

unsigned int localPort = 2910;

boolean masterState = true;
int swarmClear[SWARMSIZE];
int swarmVersion[SWARMSIZE];
int swarmState[SWARMSIZE];
long swarmTimeStamp[SWARMSIZE];

IPAddress serverAddress = IPAddress(0, 0, 0, 0);
int swarmAddresses[SWARMSIZE];

int clearColor = 0;

const int PACKET_SIZE = 14;
const int BUFFERSIZE = 1024;
byte packetBuffer[BUFFERSIZE];

WiFiUDP udp;
IPAddress localIP;

unsigned long lastBroadcastMillis = 0;
unsigned long lastPiSendMillis = 0;
const long BROADCAST_INTERVAL = 100; 
const long PI_SEND_INTERVAL = 1000;
unsigned long lastMasterStateChange = 0;


// FUNCTION DECLARATIONS
unsigned long sendLightUpdatePacket(IPAddress & address);
void checkAndSetIfMaster();
int setAndReturnMySwarmIndex(int incomingID);


void setup()
{
  Serial.begin(115200);
  Serial.println("\n\n--------------------------");
  Serial.println("LightSwarm ESP32");
  Serial.print("Version "); Serial.println(VERSIONNUMBER);
  
  pinMode(PWM_Pin, OUTPUT);
  pinMode(LED_builtin, OUTPUT);
  analogReadResolution(RESOLUTION);
  for (int i = 0; i < numLeds; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW); // Ensure off at start
  }
  
  digitalWrite(LED_builtin, HIGH);
  delay(500);
  digitalWrite(LED_builtin, LOW);
  delay(500);
  digitalWrite(LED_builtin, HIGH);
  
  randomSeed(analogRead(analog_read_pin));

  mySwarmID = 0;
  Serial.print("ESP ID: ");
  Serial.println(MY_ESP_ID);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(localPort);

  for (int i = 0; i < SWARMSIZE; i++)
  {
    swarmAddresses[i] = 0;
    swarmClear[i] = 0;
    swarmTimeStamp[i] = -1;
    swarmVersion[i] = 0;
    swarmState[i] = 0;
  }
  
  localIP = WiFi.localIP();
  
  swarmAddresses[mySwarmID] = MY_ESP_ID;
  swarmClear[mySwarmID] = 0;
  swarmTimeStamp[mySwarmID] = 1;
  clearColor = swarmClear[mySwarmID];
  swarmVersion[mySwarmID] = VERSIONNUMBER;
  swarmState[mySwarmID] = masterState;
  
  Serial.print("MySwarmID (Index) = ");
  Serial.println(mySwarmID);
}

void loop()
{
  unsigned long currentMillis_loop = millis();
  if (currentMillis_loop - mainLoopPreviousMillis < MAIN_LOOP_INTERVAL) {
    return;
  }
  mainLoopPreviousMillis = currentMillis_loop;

  // Reading the Sensor
  clearColor = analogRead(analog_read_pin);
  swarmClear[mySwarmID] = clearColor;
  pwmInterval = map(clearColor, 400, 3000, 0, 255);
  analogWrite(PWM_Pin, pwmInterval); //PWM
  static unsigned long lastBarPrint = 0;
  if (millis() - lastBarPrint > 1000) {
    lastBarPrint = millis();
  int barInterval = map(clearColor, 400, 3000, 0, numLeds); 
// LED bar graph logic
  for (int i = 0; i < barInterval; i++) {
    digitalWrite(ledPins[i], HIGH);
  }
  for (int i = barInterval; i < numLeds; i++) {
    digitalWrite(ledPins[i], LOW);
  }
  }


  // Checking the master status periodically
  // Checking every 0.5 seconds to guarantee detection is ALWAYS under 3 seconds
  static unsigned long lastMasterCheck = 0;
  if (millis() - lastMasterCheck > 500) { 
    lastMasterCheck = millis();
    if (millis() - lastMasterStateChange < 200) { 
      return; // Skip checking if a change just happened
  }
    checkAndSetIfMaster();
  }

  // Printing status every 2 seconds
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 2000) {
    lastDebugPrint = millis();
    Serial.print("ESP#");
    Serial.print(MY_ESP_ID);
    Serial.print(" Light:");
    Serial.print(clearColor);
    Serial.print(" Master:");
    Serial.print(masterState ? "YES" : "NO");
    Serial.print(" On-Board LED:");
    Serial.print(digitalRead(LED_builtin) == LOW ? "ON" : "OFF");
    Serial.print(" | Known ESPs: ");
    for (int i = 0; i < SWARMSIZE; i++) {
      if (swarmAddresses[i] != 0 && swarmTimeStamp[i] > 0) {
        Serial.print("#");
        Serial.print(swarmAddresses[i]);
        Serial.print("(");
        Serial.print(swarmClear[i]);
        Serial.print(") ");
      }
    }
    Serial.println();
  }

  // Handling the incoming Packets
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udp.read(packetBuffer, BUFFERSIZE);

    if (packetBuffer[1] == LIGHT_UPDATE_PACKET)
    {
      int incomingID = packetBuffer[2];
      Serial.print("LIGHT_UPDATE from ESP #");
      Serial.println(incomingID);
      
      int incomingIndex = setAndReturnMySwarmIndex(incomingID);

      swarmClear[incomingIndex] = packetBuffer[5] * 256 + packetBuffer[6];
      swarmVersion[incomingIndex] = packetBuffer[4];
      swarmState[incomingIndex] = packetBuffer[3];
      swarmTimeStamp[incomingIndex] = millis();

      checkAndSetIfMaster();
    }

    if (packetBuffer[1] == RESET_SWARM_PACKET)
    {
      Serial.println(">>> RESET_SWARM_PACKET Received - RESETTING ESP32!");
      delay(100);
      ESP.restart();
    }
    
    if (packetBuffer[1] == RESET_ME_PACKET)
    {
      if (packetBuffer[2] == MY_ESP_ID) {
        Serial.println(">>> RESET_ME_PACKET for me!");
        ESP.restart();
      }
    }

    if (packetBuffer[1] == DEFINE_SERVER_LOGGER_PACKET)
    {
      Serial.println(">>> DEFINE_SERVER_LOGGER_PACKET");
      serverAddress = IPAddress(packetBuffer[4], packetBuffer[5], packetBuffer[6], packetBuffer[7]);
      Serial.print("Server: ");
      Serial.println(serverAddress);
    }
  }
  // Broadcasting to other ESPs
  unsigned long currentMillis = millis();
  if (currentMillis - lastBroadcastMillis > BROADCAST_INTERVAL) {
    lastBroadcastMillis = currentMillis;
    IPAddress broadcastAddress(255, 255, 255, 255);
    sendLightUpdatePacket(broadcastAddress);
  }

  // Sending to RPi if Master
  if (masterState == true && serverAddress != IPAddress(0, 0, 0, 0)) {
    if (currentMillis - lastPiSendMillis > PI_SEND_INTERVAL) {
      lastPiSendMillis = currentMillis;
      
      jsonDoc.clear();
      jsonDoc["id"] = MY_ESP_ID;
      jsonDoc["light"] = clearColor;

      size_t len = serializeJson(jsonDoc, jsonPacket, sizeof(jsonPacket));

      Serial.print("Sending to Pi: ");
      Serial.println(jsonPacket);

      udp.beginPacket(serverAddress, localPort);
      udp.write((uint8_t*)jsonPacket, len);
      udp.endPacket();
    }
  }
}

unsigned long sendLightUpdatePacket(IPAddress & address)
{
  memset(packetBuffer, 0, PACKET_SIZE);
  packetBuffer[0] = 0xF0;
  packetBuffer[1] = LIGHT_UPDATE_PACKET;
  packetBuffer[2] = MY_ESP_ID;
  packetBuffer[3] = masterState;
  packetBuffer[4] = VERSIONNUMBER;
  packetBuffer[5] = (clearColor & 0xFF00) >> 8;
  packetBuffer[6] = (clearColor & 0x00FF);
  packetBuffer[7] = 0;
  packetBuffer[8] = 0;
  packetBuffer[9] = 0;
  packetBuffer[10] = 0;
  packetBuffer[11] = 0;
  packetBuffer[12] = 0;
  packetBuffer[13] = 0x0F;

  udp.beginPacket(address, localPort);
  udp.write(packetBuffer, PACKET_SIZE);
  udp.endPacket();
  
  return millis();
}

void checkAndSetIfMaster()
{
  // Age out old swarm members
  unsigned long currentTime = millis();
  for (int i = 0; i < SWARMSIZE; i++) {
    if (i == mySwarmID) continue;
    if (swarmTimeStamp[i] > 0 && (currentTime - swarmTimeStamp[i]) > SWARMTOOOLD) {
      Serial.print("Swarm member ");
      Serial.print(swarmAddresses[i]);
      Serial.println(" timed out");
      swarmTimeStamp[i] = -1;
      swarmAddresses[i] = 0;
    }
  }

  boolean setMaster = true;

  // Checking against ALL active swarm members 
  for (int i = 0; i < SWARMSIZE; i++)
  {
    // Skiping inactive slots
    if (swarmTimeStamp[i] <= 0) continue;
    
    // Skiping comparing to myself
    if (i == mySwarmID) continue;

    // Comparing light values 
    if (swarmClear[mySwarmID] > swarmClear[i]) {
      // I have MORE light, continue checking
      continue;
    } 
    else if (swarmClear[mySwarmID] == swarmClear[i]) {
      // TIE: Use ID as tie-breaker (LOWER ID wins)
      if (MY_ESP_ID < swarmAddresses[i]) {
        continue;
      } else {
        setMaster = false;
        break;
      }
    } 
    else {
      setMaster = false;
      break;
    }
  }
  
  // Updating master state ONLY if it changed
  if (setMaster != masterState) {
    if (setMaster == true) {
      Serial.println(">>> I BECAME Master");
      Serial.print("My light: ");
      Serial.print(swarmClear[mySwarmID]);
      Serial.print(" | Known lights: ");
      for (int i = 0; i < SWARMSIZE; i++) {
        if (swarmTimeStamp[i] > 0) {
          Serial.print("#");
          Serial.print(swarmAddresses[i]);
          Serial.print("=");
          Serial.print(swarmClear[i]);
          Serial.print(" ");
        }
      }
      Serial.println();
    } else {
      Serial.println(">>> I LOST Master");
      Serial.print("My light: ");
      Serial.print(swarmClear[mySwarmID]);
      Serial.print(" | Known lights: ");
      for (int i = 0; i < SWARMSIZE; i++) {
        if (swarmTimeStamp[i] > 0) {
          Serial.print("#");
          Serial.print(swarmAddresses[i]);
          Serial.print("=");
          Serial.print(swarmClear[i]);
          Serial.print(" ");
        }
      }
      Serial.println();
    }
    
    lastMasterStateChange = millis();
    masterState = setMaster;
  }

  swarmState[mySwarmID] = masterState;
  
  // Updating LED (Master = ON, Non-Master = OFF)
  if (masterState == true) {
    digitalWrite(LED_builtin, HIGH); // Master LED ON
  } else {
    digitalWrite(LED_builtin, LOW);  // Non-Master LED OFF
  }
}

int setAndReturnMySwarmIndex(int incomingID)
{
  // Checking if already in system
  for (int i = 0; i < SWARMSIZE; i++) {
    if (swarmAddresses[i] == incomingID) {
      return i;
    }
  }
  
  // Finding empty slot
  for (int i = 0; i < SWARMSIZE; i++) {
    if (swarmAddresses[i] == 0) {
      swarmAddresses[i] = incomingID;
      Serial.print("New ESP ID ");
      Serial.print(incomingID);
      Serial.print(" at index ");
      Serial.println(i);
      return i;
    }
  }
  
  // Swarm full - find oldest
  unsigned long oldestTime = millis();
  int oldestIndex = SWARMSIZE - 1;
  
  for (int i = 0; i < SWARMSIZE; i++) {
    if (i == mySwarmID) continue;
    if (swarmTimeStamp[i] < oldestTime) {
      oldestTime = swarmTimeStamp[i];
      oldestIndex = i;
    }
  }
  
  Serial.print("Swarm full, replacing index ");
  Serial.println(oldestIndex);
  swarmAddresses[oldestIndex] = incomingID;
  return oldestIndex;
}