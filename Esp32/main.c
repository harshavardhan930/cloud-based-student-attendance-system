#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WebServer.h>

/* ---------- CONFIG BUTTON ---------- */
#define CONFIG_BUTTON 15

/* ---------- FLASH ---------- */
Preferences prefs;

/* ---------- WEB SERVER ---------- */
WebServer server(80);

/* ---------- WIFI + MQTT (FROM FLASH) ---------- */
String wifi_ssid;
String wifi_pass;
String mqtt_ip;
int    mqtt_port;

/* ---------- MQTT ---------- */
const char* topic_uid  = "rfid/uid";
const char* topic_resp = "rfid/response";

WiFiClient espClient;
PubSubClient client(espClient);

/* ---------- OLED ---------- */
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_SDA      21
#define OLED_SCL      22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ---------- RFID ---------- */
#define RFID_CS   5
#define RFID_RST  2
MFRC522 rfid(RFID_CS, RFID_RST);

/* ---------- LEDs ---------- */
#define GREEN_LED 27
#define RED_LED   26

/* ---------- DISPLAY HELPERS ---------- */
void showMessage(const char *msg)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 30);
  display.println(msg);
  display.display();
}

/* ---------- DO NOT REMOVE ---------- */
void showStudent(char *name, char *roll, char *cls)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("Attendance OK");

  display.setCursor(0, 14);
  display.print("Name : ");  display.println(name);
  display.print("Roll : ");  display.println(roll);
  display.print("Class: ");  display.println(cls);

  display.display();
}

/* ---------- SAVE CONFIG ---------- */
void saveConfig()
{
  prefs.putString("ssid", wifi_ssid);
  prefs.putString("pass", wifi_pass);
  prefs.putString("ip",   mqtt_ip);
  prefs.putInt("port",    mqtt_port);
}

/* ---------- LOAD CONFIG ---------- */
bool loadConfig()
{
  wifi_ssid = prefs.getString("ssid", "");
  wifi_pass = prefs.getString("pass", "");
  mqtt_ip   = prefs.getString("ip", "");
  mqtt_port = prefs.getInt("port", 1883);

  return wifi_ssid.length() > 0;
}

/* ---------- WEB PAGE ---------- */
void handleRoot()
{
  String page =
    "<h2>ESP32 RFID Configuration</h2>"
    "<form action='/save'>"
    "WiFi SSID:<br><input name='ssid'><br>"
    "WiFi Password:<br><input name='pass' type='password'><br>"
    "MQTT Broker IP:<br><input name='ip'><br>"
    "MQTT Port:<br><input name='port' value='1883'><br><br>"
    "<input type='submit' value='Save & Restart'>"
    "</form>";

  server.send(200, "text/html", page);
}

void handleSave()
{
  wifi_ssid = server.arg("ssid");
  wifi_pass = server.arg("pass");
  mqtt_ip   = server.arg("ip");
  mqtt_port = server.arg("port").toInt();

  saveConfig();

  server.send(200, "text/html",
              "<h3>Saved successfully! Restarting...</h3>");
  delay(1500);
  ESP.restart();
}

/* ---------- CONFIG AP MODE ---------- */
void startConfigMode()
{

    display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("connect to access point(attendence_Config)");
  display.setCursor(10, 30);
  display.println("192.168.4.1");
  display.setCursor(10, 40);
  display.println("Config Mode");

  display.display();

  WiFi.softAP("attendence_Config");
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.begin();

  while (true)
  {
    server.handleClient();
    delay(10);
  }
}

/* ---------- MQTT CALLBACK ---------- */
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  payload[length] = '\0';
  char data[128];
  strcpy(data, (char*)payload);

  if (strcmp(data, "UNKNOWN") == 0)
  {
    digitalWrite(RED_LED, HIGH);
    showMessage("Unknown Card");
    delay(2000);
    digitalWrite(RED_LED, LOW);
    showMessage("Scan your ID");
    return;
  }

  char *name = strtok(data, ",");
  char *roll = strtok(NULL, ",");
  char *cls  = strtok(NULL, ",");

  if (name && roll && cls)
  {
    digitalWrite(GREEN_LED, HIGH);
    showStudent(name, roll, cls);
    delay(3000);
    digitalWrite(GREEN_LED, LOW);
  }

  showMessage("Scan your ID");
}

/* ---------- MQTT CONNECT ---------- */
void connectMQTT()
{
  client.setServer(mqtt_ip.c_str(), mqtt_port);
  client.setCallback(mqttCallback);

  while (!client.connected())
  {
    showMessage("MQTT Connecting...");
    client.connect("ESP32_RFID");
    delay(1000);
  }

  client.subscribe(topic_resp);
  showMessage("Scan your ID");
}

/* ---------- SETUP ---------- */
void setup()
{
  Serial.begin(115200);

  pinMode(CONFIG_BUTTON, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  SPI.begin(18, 19, 23);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  showMessage("Starting...");

  rfid.PCD_Init();

  prefs.begin("config", false);

  if (digitalRead(CONFIG_BUTTON) == LOW)
  {
    startConfigMode();   // GPIO 15 pressed
  }

  if (!loadConfig())
  {
    startConfigMode();   // No saved config
  }

  showMessage("WiFi Connecting...");
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  connectMQTT();
}

/* ---------- LOOP ---------- */
void loop()
{
  client.loop();

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++)
    uid += String(rfid.uid.uidByte[i], HEX);

  showMessage("Checking ID...");
  client.publish(topic_uid, uid.c_str());

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(2000);
}
