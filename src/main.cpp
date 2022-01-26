#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <Hash.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ESP_Mail_Client.h>

// DS18B20 sensor support
#include <OneWire.h>
#include <DallasTemperature.h>

#include <fs.h>

#include "trendlist.h"

#include <debug_print.h>

// Data wire is connected to GPIO 4
#define ONE_WIRE_BUS 4
#define RELAIS 5

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);

//
const byte led = LED_BUILTIN;
ESP8266WiFiMulti wifiMulti;

// sensor variables
DeviceAddress da1;
DeviceAddress da2;
float tempC1 = 0.0;
float tempC2 = 0.0;
String message = "";

int resolution = 10;
int delayInMillis;
uint32 lastTempRequest;



//mail config
bool sendWarning = false;
bool warningSent = false;

#define SMTP_HOST "smtp.1und1.de"
#define SMTP_PORT 465

/* The sign in credentials */
#define AUTHOR_EMAIL "hf@hfiergolla.de"
#define AUTHOR_PASSWORD "170167hf"

/* Recipient's email*/
#define RECIPIENT_EMAIL "holger.fiergolla@gmail.com"

/* The SMTP Session object used for Email sending */
SMTPSession smtp;

void smtpCallback(SMTP_Status status){
  /* Print the current status */
  debug_println(status.info());

  /* Print the sending result */
  if (status.success()){
    debug_println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failled: %d\n", status.failedCount());
    debug_println("----------------\n");
    struct tm dt;

    for (size_t i = 0; i < smtp.sendingResult.size(); i++){
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      time_t ts = (time_t)result.timestamp;
      localtime_r(&ts, &dt);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %d/%d/%d %d:%d:%d\n", dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min, dt.tm_sec);
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients);
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject);
    }
    debug_println("----------------\n");
  }
}
void initSMTP(){
  /** Enable the debug via Serial port
   * none debug or 0
   * basic debug or 1
  */
  smtp.debug(1);

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);
}



// Variables to store temperature values
String temperatureF = "";
String temperatureC = "";
String temperatureC1 = "";
String temperatureC2 = "";

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 5000;

// Replace with your network credentials
const char* ssid = "REPLACE_WITH_YOUR_SSID";
const char* password = "REPLACE_WITH_YOUR_PASSWORD";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(

)rawliteral";

char buffer[128];           

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char cmd[128];
    int val;

    debug_println((const char*)data);
    String sdata = String((char*)data);

    
    if (sdata.startsWith("SETRES")){
      String sval = sdata.substring(sdata.indexOf(":")+1);
      debug_println("sval: "+sval);
      
      val = atoi(sval.c_str());
      resolution = val;
      ws.textAll("RESOL:"+String(val));
    }
    
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    switch (type) {
      case WS_EVT_CONNECT:
        sprintf(buffer, "WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        debug_println(buffer);
        break;
      case WS_EVT_DISCONNECT:
        sprintf(buffer, "WebSocket client #%u disconnected\n", client->id());
        debug_println(buffer);
        break;
      case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
      case WS_EVT_PONG:
      case WS_EVT_ERROR:
        break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

bool initRun = true;
bool runError = false;

String updateTempMessage(TrendList &trendList)
{
  String res = "";
  float t1 = trendList.lastData();
  float t2 = trendList.trend();

  res = "TEMP:" + (t1 == -127.0 ? "--" :String(t1)) 
    + ":" + (t2 == -127.0 ? "--" :String(t2)) + " - "+ String(trendList.count()) ;
  //debug_println("Message = "+ res);
  return res;
}

float readDSTemperatureC(int idx) {
  // Call sensors.requestTemperatures() to issue a global temperature and Requests to all devices on the bus
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(idx);

  if (tempC == -127.00) {
    debug_println("Failed to read from DS18B20 sensor");
  } else {
    debug_print("Temperature Celsius: ");
    debug_println(tempC);
  }
  return tempC;
}


// Replaces placeholder with DS18B20 values
String processor(const String& var) {
  //debug_println(var);
  if (var == "TEMPERATUREC1") {
    return temperatureC1;
  }
  else if (var == "TEMPERATUREC2") {
    return temperatureC2;
  }
  else if (var == "TEMPERATUREF") {
    return temperatureF;
  }
  return String();
}

void initOTA() {
  ArduinoOTA.setHostname("ESP8266");
  //ArduinoOTA.setPassword("esp8266");

  ArduinoOTA.onStart([]() {
    debug_println("Start");
  });
  ArduinoOTA.onEnd([]() {
    debug_println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    sprintf(buffer,"Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    sprintf(buffer,"Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) debug_println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) debug_println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) debug_println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) debug_println("Receive Failed");
    else if (error == OTA_END_ERROR) debug_println("End Failed");
  });
  ArduinoOTA.begin();
  debug_println("OTA ready");
}

void connectWifi() {
  // Connect to Wi-Fi
  // wifiMulti.addAP("hfs21", "1234abcD");
  wifiMulti.addAP("HierKummstDuNetRein", "2649828801618895");
  debug_println("Connecting ...");
  
  pinMode(led, OUTPUT);
  digitalWrite(led, 1);

  while (wifiMulti.run() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
    delay(250);
    digitalWrite(led, !digitalRead(led));  // Change the state of the LED
    debug_print('.');
  }
  // Print ESP Local IP Address
  debug_println(WiFi.SSID());
  debug_println(WiFi.localIP());
}

void initSensors(){
  // Start up the DS18B20 library
  sensors.begin();
  sensors.getAddress(da1, 0);
  sensors.getAddress(da2, 1);

  sensors.setResolution(da1, resolution);  
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  delayInMillis = 750 / (1 << (12 - resolution)); 
  lastTempRequest = millis(); 
}

void initWebserver(){
  initWebSocket();
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/index.html", String(), false, processor);
  
  server.on("/server.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/server.css", "text/css");
  });
//    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperaturec", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/plain", temperatureC1.c_str());
  });
  server.on("/temperaturec1", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/plain", temperatureC1.c_str());
  });
  server.on("/temperaturec2", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/plain", temperatureC2.c_str());
  });
  server.on("/temperaturef", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/plain", temperatureF.c_str());
  });
  // Start server
  server.begin();

}

void initTemperatures(){
  float temp =  readDSTemperatureC(0);
  tempC1 = temp;
  sendWarning = tempC1 <= 59.0;

  if (tempC1 > 66.0){
    // sendWarning = false;
    warningSent = false;
  }

  temperatureC1 = String(tempC1);
  temperatureC2 = String(readDSTemperatureC(1));
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  debug_println();
  pinMode(RELAIS, OUTPUT);
  SPIFFS.begin();
  initSensors();
  connectWifi();
  initOTA();
  //initSMTP();
 
  initWebserver();
  initTemperatures();
 
}


void sendWarnMail(){
    debug_println("send Warning");
    return;

  /* Set the callback function to get the sending results */
  smtp.callback(smtpCallback);

  /* Declare the session config data */
  ESP_Mail_Session session;

  /* Set the session config */
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "";

  /* Declare the message class */
  SMTP_Message message;

  /* Set the message headers */
  message.sender.name = "ESP";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "Heizungswarnung";
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_high;
  message.addRecipient("Holger", RECIPIENT_EMAIL);
  
  /*Send HTML message*/
  String htmlMsg = R"rawliteral(
    <div style=\"color:#2f4468;\">
      <h1>Heizungs Warnung!</h1>
      <p>Kesseltemperatur: "+temperatureC1+"</p>
      <p>Heizung vermutlich wieder aus!</p>
      </div>
  )rawliteral";
  message.html.content = htmlMsg.c_str();
  message.html.content = htmlMsg.c_str();
  message.text.charSet = "us-ascii";
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  /* Set the custom message header */
  //message.addHeader("Message-ID: <abcde.fghij@gmail.com>");

  /* Connect to server with the session config */
  if (!smtp.connect(&session))
    return;

  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    debug_println("Error sending Email, " + smtp.errorReason());
  else
    warningSent = true;
}

unsigned long previousTime = millis();
unsigned long interval = 3000;
bool rising = false;

TrendList trendList;

void readSensors(){
  float temp = sensors.getTempCByIndex(0);
  sensors.requestTemperatures(); 
  lastTempRequest = millis(); 

  temperatureC1 = String(tempC1);
  // temperatureC2 = String(readDSTemperatureC(1));
  
  //debug_println("Updating sensor data ");
  trendList.add(temp);
  String message = updateTempMessage(trendList);
  
  if (temp != tempC1) {
    if (temp > tempC1)
      rising = temp <= 70.0;

    if (temp < tempC1 && rising){
      // sendWarnMail();
      rising = false;
    }
    
    tempC1 = temp;
  }
  ws.textAll(message);
}

void loop() {
  ArduinoOTA.handle();
  unsigned long diff = millis() - previousTime;
  if (diff > interval) {
    int status = digitalRead(led);
    digitalWrite(led, !status);  // Change the state of the LED
    digitalWrite(RELAIS, status);
    interval = (bool)status ? 100 : 4900;
    previousTime += diff;
  }

  if (millis() - lastTempRequest >= delayInMillis){
    readSensors();
  }
}