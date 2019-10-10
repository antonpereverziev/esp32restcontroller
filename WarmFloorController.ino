#include <dummy.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "time.h"
#include <HTTPClient.h>
#include <Wire.h>
#include "RTClib.h"
 
RTC_DS3231 rtc;
 
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

Preferences prefs;

const char *ssid = "MikroTik-2";
const char *password = "06092014";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600*2;
const int   daylightOffset_sec = 3600;

WebServer server(80);

const int mainPageSize = 3000;
char* kitchenHostname = "warm.floor.kitchen";
char* successResponse = "<html><head><meta charset='UTF-8'/></head><body><b>Новое значение сохранено!</b><br><a href='/'><b>Назад</b></a></body></html>";
int power = 500;
int consumption = 650;
int intervalsQuantity = 10;
int begin_h;
int begin_m;
int end_h;
int end_m;
int fullCycleTime = 100;
int totalTimeSeconds = 0;
int calculatedIntervalSeconds = 100;
int calculatedHeatTimeSeconds = 0;
int now_h;
int now_m;
boolean isHeatTime;
int counter = 0;
int currentHeatingCycle = -1;
int next_h;
int next_m;
int next_s;

String token = "A3QqMemiiJWCKC9D038ST5MEEtWgqG";
String log_idvariable = "5d99edf1c03f974dca29a9f4";

typedef struct {
  uint16_t power;
  uint16_t consumption;
  uint16_t intervalsQuantity;
  uint16_t begin_h;
  uint16_t begin_m;
  uint16_t end_h;
  uint16_t end_m;
} app_params_t;

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint16_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void) {
  Serial.begin(115200);

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  logDeviceBooted();
  
  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getLocalTime();
  loadDataFromEeprom();
  calcIntervals();
  initAndBeginServer();
 
}

void loop(void) {
  if (counter++ == 500000) {
    counter = 0;
    getLocalTime();
    int secondsFromStart = (now_h-begin_h)*3600 + (now_m - begin_m)*60;
    Serial.println("Seconds From Start: " + String(secondsFromStart));
    int cyclesFromStart = -1;
    if (secondsFromStart>=0) {
      cyclesFromStart = secondsFromStart/fullCycleTime;
    }
    Serial.println("cyclesFromStart: " + String( cyclesFromStart));
    Serial.println("currentHeatingCycle: " + String(currentHeatingCycle));
    int absoluteEnableTime = begin_h*3600+begin_m*60+(currentHeatingCycle + 1) * fullCycleTime;
    Serial.println("currentHeatingCycle: " + String(absoluteEnableTime));
    next_h = absoluteEnableTime/3600;
    next_m = (absoluteEnableTime%3600)/60;
    next_s = absoluteEnableTime%60;
    
    if (cyclesFromStart > -1 && cyclesFromStart > currentHeatingCycle && cyclesFromStart < intervalsQuantity) {
      enableHeatingNow();
      currentHeatingCycle++;
      next_h = absoluteEnableTime/3600;
      next_m = (absoluteEnableTime%3600)/60;
      next_s = absoluteEnableTime%60;
    } else if (cyclesFromStart > intervalsQuantity || secondsFromStart < 0) {
      currentHeatingCycle = -1;
      next_h = begin_h;
      next_m = begin_m;
      next_s = 0;
    }
  }
  server.handleClient();
}

void  initAndBeginServer() {
  server.on("/", handleRoot);
  server.on("/powerinput", savePower);
  server.on("/consumptioninput", saveConsumption);
  server.on("/intervalsQuantityinput", saveInterval);
  server.on("/beginTimeinput", saveBeginTime);
  server.on("/endTimeinput", saveEndTime);
  server.on("/enableHeatingNow", enableHeating);
  server.on("/adjustTime", adjustTime);
  server.on("/error", []() {
    server.send(200, "text/plain", "Error!!");
  });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void handleRoot() {
  char temp[mainPageSize];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  DateTime now = rtc.now();

  snprintf(temp, mainPageSize,
"<html>\
 <head>\
  <title>Панель управления теплыми полами</title>\
  <meta charset='UTF-8'>\
  <script>\
    function hideChangeLink(elementId) {\
    var x = document.getElementById(elementId + 'Change');\
    x.style.display = 'none';\
    }\
    function addInput(elementId) {\
      var button = document.createElement('input');\
      var js_function = 'saveValue(\"' + elementId + '\")';\
      button.setAttribute('onclick', js_function);\
      button.type = 'submit';\
      button.id = elementId + 'button';\
      var para = document.createElement('input');\
      para.id = elementId + 'input';\
      para.type = 'text';\
      var element = document.getElementById(elementId);\
      element.appendChild(para);\
      element.appendChild(button);\
      hideChangeLink(elementId);\
    }\
function saveValue(parameter) {\
var x = document.getElementById(parameter + 'input');\
window.location.href = '/' + parameter + 'input?value=' + x.value;\
}\
</script>\
</head>\
<body>\
<h1>Панель управления теплыми полами</h1>\
<h3>Текущее время: %02d:%02d:%02d</h3>\
<h2>Теплый пол кухня. Состояние: %s<b>вкл</b>. Следующее включение в <b>%02d:%02d:%02d</b></h2>\
<div id='beginTime'>Время включения <b>%02d:%02d</b> <a id='beginTimeChange' href=\"javascript:addInput('beginTime');\">Изменить</a></div>\
<div id='endTime'>Время выключения <b>%02d:%02d</b> <a id='endTimeChange' href=\"javascript:addInput('endTime');\">Изменить</a></div>\
<div id='consumption'>Расход электроэнергии <b>%d</b> Вт*ч <a id='consumptionChange' href=\"javascript:addInput('consumption');\">Изменить</a></div>\
<div id='intervalsQuantity'>Количество включений <b>%d</b> раз в день <a id='intervalsQuantityChange' href=\"javascript:addInput('intervalsQuantity');\">Изменить</a></div>\
<div id='power'>Мощность нагревательного шнура <b>%d</b> Вт <a id='powerChange' href=\"javascript:addInput('power');\">Изменить</a></div>\
<div id='hostname'>Имя хоста контроллера <b>%s</b></div>\
<h3>Расчитанные параметры:</h3>\
<br/>Общее время работы %s\
<br/>Длительность охлаждения %s\
<br/>Длительность нагрева %s\
<br/><br/><a href='/enableHeatingNow'> Включить нагрев сейчас</a>\
<br/><br/><a href='/adjustTime'> Настроить время</a>\
</body>\
</html>",
           now.hour(),
           now.minute(),
           now.second(),
           String(isHeatTime),
           next_h,
           next_m,
           next_s,
           begin_h,
           begin_m,
           end_h,
           end_m,
           consumption,
           intervalsQuantity,
           power,
           kitchenHostname,
           convertToMinutes(totalTimeSeconds),
           convertToMinutes(calculatedIntervalSeconds),
           convertToMinutes(calculatedHeatTimeSeconds)
          );
  server.send(200, "text/html", temp);
}

void enableHeating () {
  enableHeatingNow();
  server.send(200, "text/plain", "Saved");
}

void adjustTime () {
  getLocalTime();
  rtc.adjust(DateTime(2019, 10, 5, now_h, now_m, 0));
}

void enableHeatingNow() {
  HTTPClient http;
  http.begin("http://" + String(kitchenHostname) + "/relay/0?turn=on&timer=" + calculatedHeatTimeSeconds);
  Serial.print("[HTTP] GET...\n http://" + String(kitchenHostname) + "/relay/0?turn=on&timer=" + calculatedHeatTimeSeconds);
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println(payload);
    }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void calcIntervals() {
  int totalEnablementTimeSeconds = consumption*60*60/power;
  totalTimeSeconds = (end_h-begin_h)*3600 + (end_m-begin_m)*60;
  if (totalTimeSeconds < 0) {
    totalTimeSeconds = 24*3600+totalTimeSeconds; 
  }
  calculatedHeatTimeSeconds = totalEnablementTimeSeconds/intervalsQuantity;
  fullCycleTime = totalTimeSeconds/intervalsQuantity;
  calculatedIntervalSeconds = fullCycleTime-calculatedHeatTimeSeconds;
  int secondsFromStart = (now_h-begin_h)*3600 + (now_m - begin_m)*60;
  if (secondsFromStart < 0) {
    currentHeatingCycle = -1;
  } else {
    currentHeatingCycle = secondsFromStart/fullCycleTime;
  }
  int lastCycleTime = secondsFromStart - fullCycleTime*currentHeatingCycle; 
}

void loadDataFromEeprom() {
  prefs.begin("app_params");
  size_t schLen = prefs.getBytesLength("app_params");
  char buffer[schLen]; // prepare a buffer for the data
  prefs.getBytes("app_params", buffer, schLen);
  if (schLen % sizeof(app_params_t)) { // simple check that data fits
    log_e("Data is not correct size!");
    return;
  }
  app_params_t *app_params = (app_params_t *) buffer; // cast the bytes into a struct ptr
  for (int x=0; x<schLen; x++) Serial.printf("%03X ", buffer[x]);
  Serial.printf("%d %d %d %02d:%02d %02d:%02d\n", 
  app_params[0].power, app_params[0].consumption,
  app_params[0].intervalsQuantity, app_params[0].begin_h, app_params[0].begin_m,app_params[0].end_h,app_params[0].end_h );
  power = app_params[0].power;
  consumption = app_params[0].consumption;
  intervalsQuantity = app_params[0].intervalsQuantity;
  begin_h=app_params[0].begin_h;
  begin_m = app_params[0].begin_m;
  end_h = app_params[0].end_h;
  end_m = app_params[0].end_m;
}

void saveDataToEeprom(){
  uint16_t content[] = {(uint16_t)power, (uint16_t)consumption, (uint16_t)intervalsQuantity, (uint16_t)begin_h, (uint16_t) begin_m, (uint16_t)end_h, (uint16_t) end_m};
  prefs.putBytes("app_params", content, sizeof(content));
  calcIntervals();
}

void savePower() {
  for (uint16_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "value") {
      power = server.arg(i).toInt();
      types(power);
      break;
    }
  }
  saveDataToEeprom();
  sendSuccessResponse();
}

void saveConsumption() {
  for (uint16_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "value") {
      consumption = server.arg(i).toInt();
      break;
    }
  }
  saveDataToEeprom();
  sendSuccessResponse();
}

void saveInterval() {
  for (uint16_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "value") {
      intervalsQuantity = server.arg(i).toInt();
      break;
    }
  }
  saveDataToEeprom();
  sendSuccessResponse();
}

void saveEndTime() {
  for (uint16_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "value") {
      String endTime = server.arg(i);
      end_h = endTime.substring(0,2).toInt();
      end_m = endTime.substring(3).toInt();
      break;
    }
  }
  saveDataToEeprom();
  sendSuccessResponse();
}

void saveBeginTime() {
  for (uint16_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "value") {
      String endTime = server.arg(i);
      begin_h = endTime.substring(0,2).toInt();
      begin_m = endTime.substring(3).toInt();
      break;
    }
  }
  saveDataToEeprom();
  sendSuccessResponse();
}

void sendSuccessResponse() {
  server.send(200, "text/html", successResponse);
}

void getLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  now_h = timeinfo.tm_hour;
  now_m = timeinfo.tm_min;
}

String convertToMinutes(int seconds) {
  char arr[6];
  snprintf(arr, 6, "%02d:%02d", (seconds/60), (seconds%60));
  return String(arr);
}

void logDeviceBooted() {
  ubiSave_value("1", log_idvariable); 
}
void logSwitchOn() {
}
void logSwitchOff() {
   ubiSave_value("0", log_idvariable);
}
void ubiSave_value(String value, String idvariable) {
  HTTPClient http;
  http.begin("http://things.ubidots.com/api/v1.6/variables/"+idvariable+"/values");
  http.addHeader("Host", "things.ubidots.com");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Auth-Token", token);
  int httpResponseCode = http.POST("{\"value\": " + String(value)+"}");
  if (httpResponseCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] POST... code: %d\n", httpResponseCode);
    // file found at server
    if (httpResponseCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println(payload);
    }
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void types(String a){Serial.println("it's a String");}
void types(int a)   {Serial.println("it's an int");}
void types(char* a) {Serial.println("it's a char*");}
void types(float a) {Serial.println("it's a float");}

