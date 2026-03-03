#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

// ================= CONFIG =================
#define NUM_LEDS 12
#define LED_PIN D5
#define EEPROM_SIZE 128
#define DNS_PORT 53

// ================= TLS CERT =================
const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n"
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
"Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
"MrY=\n"
"-----END CERTIFICATE-----\n";

// ================= WIFI STORAGE =================
char wifiSSID[32] = "";
char wifiPASS[32] = "";
char serverAddress[64] = "";

// ================= MQTT =================
const char* mqtt_server = "s0169691.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_user = "Neki";
const char* mqtt_pass = "123499";
const char* publish_topic = "lamp/data";
const char* control_topic = "lamp/control";

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// ================= LED =================
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t ledStates[NUM_LEDS][3]; // per-LED RGB
uint8_t brightness = 255;        // overall brightness (0-255)

// ================= TIMER =================
unsigned long lampTimerStartTime = 0;
unsigned long lampTimerDuration = 0;
bool lampTimerRunning = false;
bool timerStarted = false;

// ================= SERVER =================
WebServer server(80);
DNSServer dnsServer;
bool wifiConnected = false;

// ================= LED BLINK =================
void blinkWhite(int times){
  for(int t=0; t<times; t++){
    for(int i=0;i<NUM_LEDS;i++)
      strip.setPixelColor(i, strip.Color(255,255,255));
    strip.show();
    delay(250);
    strip.clear();
    strip.show();
    delay(250);
  }
}

// ================= CAPTIVE PAGE =================
void handleRoot() {
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  page += "<style>body{font-family:Arial;text-align:center;}input{padding:10px;margin:10px;width:80%;}button{padding:10px 20px;background:black;color:white;border:none;}</style>";
  page += "</head><body>";
  page += "<h2>ESP32 WiFi Setup</h2>";
  page += "<form action='/save' method='POST'>";
  page += "<input name='ssid' placeholder='WiFi SSID'><br>";
  page += "<input name='pass' type='password' placeholder='WiFi Password'><br>";
  page += "<input name='server' placeholder='Server Address (optional)'><br>";
  page += "<button type='submit'>Connect</button>";
  page += "</form></body></html>";
  server.send(200, "text/html", page);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String serverAddr = server.arg("server");

  ssid.toCharArray(wifiSSID,32);
  pass.toCharArray(wifiPASS,32);
  serverAddr.toCharArray(serverAddress,64);

  EEPROM.put(0, wifiSSID);
  EEPROM.put(32, wifiPASS);
  EEPROM.put(64, serverAddress);
  EEPROM.commit();

  server.send(200,"text/html","<h2>Saved. Restarting...</h2>");
  delay(2000);
  ESP.restart();
}

// ================= START CAPTIVE PORTAL =================
void startPortal(){
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_LAMP_SETUP");
  delay(500);
  dnsServer.start(DNS_PORT,"*",WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/generate_204", handleRoot);
  server.on("/fwlink", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/connecttest.txt", handleRoot);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println("Captive Portal Started");
  Serial.println(WiFi.softAPIP());
}

// ================= CONNECT WIFI =================
bool connectToWiFi(){
  EEPROM.get(0,wifiSSID);
  EEPROM.get(32,wifiPASS);

  if(strlen(wifiSSID)==0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID,wifiPASS);

  int retries=20;
  while(WiFi.status()!=WL_CONNECTED && retries--) delay(500);

  if(WiFi.status()==WL_CONNECTED){
    wifiConnected=true;
    wifiClient.setCACert(ca_cert);
    blinkWhite(2);
    for(int i=0;i<NUM_LEDS;i++)
      strip.setPixelColor(i, strip.Color(255,255,255));
    strip.show();
    Serial.println("WiFi Connected: "+WiFi.localIP().toString());
    return true;
  }

  wifiConnected=false;
  blinkWhite(2);
  return false;
}

// ================= MQTT =================
void mqttCallback(char* topic, byte* payload, unsigned int length){
  String cmd = String(topic);
  
  if(cmd==control_topic && length >= NUM_LEDS*4){
    brightness = payload[3];
    for(int i=0;i<NUM_LEDS;i++){
      ledStates[i][0] = payload[i*4];
      ledStates[i][1] = payload[i*4+1];
      ledStates[i][2] = payload[i*4+2];
    }
    for(int i=0;i<NUM_LEDS;i++){
      uint8_t r = (ledStates[i][0]*brightness)/255;
      uint8_t g = (ledStates[i][1]*brightness)/255;
      uint8_t b = (ledStates[i][2]*brightness)/255;
      strip.setPixelColor(i, strip.Color(r,g,b));
    }
    strip.show();
  }

  if(cmd==control_topic && length >= 8 && payload[0]==0xF0){
    lampTimerDuration = ((unsigned long)payload[1]<<8 | payload[2])*60000UL;
    lampTimerStartTime = millis();
    lampTimerRunning = true;
    timerStarted = false; // to light all LEDs first
  }
}

void reconnectMQTT(){
  if(!wifiConnected) return;
  if(!mqttClient.connected()){
    mqttClient.setServer(mqtt_server,mqtt_port);
    mqttClient.setCallback(mqttCallback);
    if(mqttClient.connect("ESP32Lamp", mqtt_user, mqtt_pass)){
      mqttClient.subscribe(control_topic);
    }
  }
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  strip.begin();
  strip.show();

  if(!connectToWiFi()){
    startPortal();
  }
}

// ================= LOOP =================
void loop(){
  if(!wifiConnected){
    dnsServer.processNextRequest();
    server.handleClient();
    for(int i=0;i<NUM_LEDS;i++)
      strip.setPixelColor(i, strip.Color(255,255,255));
    strip.show();
  } else {
    reconnectMQTT();
    mqttClient.loop();

    if(lampTimerRunning){
      if(!timerStarted){
        // Light all LEDs first for timer
        for(int i=0;i<NUM_LEDS;i++){
          uint8_t r = ledStates[i][0];
          uint8_t g = ledStates[i][1];
          uint8_t b = ledStates[i][2];
          strip.setPixelColor(i, strip.Color(r,g,b));
        }
        strip.show();
        timerStarted = true;
      }

      unsigned long elapsed = millis() - lampTimerStartTime;
      if(elapsed >= lampTimerDuration){
        lampTimerRunning=false;
        for(int i=0;i<NUM_LEDS;i++){
          strip.setPixelColor(i,0);
          ledStates[i][0]=ledStates[i][1]=ledStates[i][2]=0;
        }
        strip.show();
        delay(2000); // wait 2 seconds
        blinkWhite(2); // blink twice
      } else {
        int ledsToLight = NUM_LEDS - (elapsed * NUM_LEDS / lampTimerDuration);
        for(int i=0;i<NUM_LEDS;i++){
          if(i<ledsToLight){
            uint8_t r = (ledStates[i][0]*brightness)/255;
            uint8_t g = (ledStates[i][1]*brightness)/255;
            uint8_t b = (ledStates[i][2]*brightness)/255;
            strip.setPixelColor(i, strip.Color(r,g,b));
          } else strip.setPixelColor(i,0);
        }
        strip.show();
      }
    }
  }
}
