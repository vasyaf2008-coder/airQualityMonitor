#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "logo.h"

#define btn 12 // сенсорная кнопка

const char* ssid = ""; //логин wifi
const char* password = ""; //пароль wifi

// ==== сервер к которому идёт обращение ====
const char* serverUrl = "";

// ==== Интервал отправки ====
const unsigned long sendInterval = 5*60*1000; // мин*сек*микросек

//==== переменные wifi времени ========================================================================================================================
// ==== Таймер ====
unsigned long lastSendTime = 0;

// ==== WiFi reconnect таймер ====
unsigned long lastWifiCheck = 0;
const unsigned long wifiCheckInterval = 5000;

//uint16_t mainСolor = 

byte measure_cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
byte response[9]; 

//==== переменные кнопок
bool sensorState = 0; // состояниесенсора
bool sensorFlag = 0; // было-ли нажато монт назад?
bool longDone = 0; // было ли долгое нажатие?

unsigned long sensorMillis = 0; // сохран. времени нажатия
unsigned long pressTime = 0; // !сохран. времени нажатия! (модуль)

//==== переменные времени ========================================================================================================================
unsigned long co2_millis = 0;

//==== переменные состояния интерфейса ===========================================================================================================
#define N 5              // количество виджетов
int order[N] = {0,1,2,3,4};  // 0 = CO2; 1 = RH; 2 = Time; 3 = Press; 4 = Temp; порядок виджетов
String title[N] = {"CO2", "RH", "TVOC", "Press", "Temp"};
String value[N] = {"", "", "", "", ""};
String unit[N] = {"ppm", "%", "ppm", "hPa", "`c"};
uint16_t color[N] = {TFT_PURPLE, TFT_PURPLE, TFT_PURPLE, TFT_PURPLE, TFT_PURPLE};
int orderCoordX[N] = {10, 125, 125, 10, 10};
int orderCoordY[N] = {10, 120, 220, 220, 120};

//==== цветные индикаторы ========================================================================================================================

int co2 = 0;
int temp = 0;
int rh = 0;
float press = 0;
int tvoc = 0;

uint16_t co2Color(int ppm) {
  if (ppm < 700) return TFT_GREEN;
  if (ppm < 1000) return TFT_YELLOW;
  return TFT_RED;
}

// uint16_t tempColor(float temp) {
//   if (temp < 25) return TFT_YELLOW;
//   if (temp < 27) return TFT_GREEN;
//   if (temp < 30) return TFT_YELLOW;
//   return TFT_RED;
// }

// uint16_t pressColor(int press) {
//   if (press < 1004) return TFT_BLUE;
//   if (press < 1007) return TFT_GREEN;
//   return TFT_RED;
// }

// uint16_t RHColor(int rh) {
//   if (rh < 40) return TFT_YELLOW;
//   if (rh < 60) return TFT_GREEN;
//   return TFT_RED;
// }

// uint16_t tvocColor(int tvoc) {
//   if (tvoc < 1004) return TFT_BLUE;
//   if (tvoc < 1007) return TFT_GREEN;
//   return TFT_RED;
// }

bool page = 0; // homePage = 0 ; roomPage = 1

//==== дисплей внешнее ===========================================================================================================================
TFT_eSPI tft = TFT_eSPI();

//####################################################################################################################################################################################
//================================================================== void setup ======================================================================================================
//####################################################################################################################################################################################
void setup() {
//==== pinMode ===================================================================================================================================
  pinMode(btn, INPUT);

//==== инициализация дисплея =====================================================================================================================
  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);
  Serial.begin(115200);

  Serial2.begin(9600, SERIAL_8N1, 16, 17); //==== запуск I2C
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, 240, 320, logo);
  delay(3000);
  connectWiFi();

  updateScreen();
}
//####################################################################################################################################################################################
//===================================================================== WIFI ======================================================================================================
//####################################################################################################################################################################################
// ==== WIFI соединение ======================================================================================================
void connectWiFi() { 
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connect FAILED");
  }
}
// ==== Проверка WiFi ======================================================================================================
void handleWiFi() {
  if (millis() - lastWifiCheck > wifiCheckInterval) {
    lastWifiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost. Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}
// ==== Отправка данных ======================================================================================================
void sendData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skip send: no WiFi");
    return;
  }

  HTTPClient http;
  http.setTimeout(5000); // таймаут 5 сек

  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  // ==== JSON ====
  StaticJsonDocument<200> doc;
  doc["co2"] = co2;
  doc["temp"] = temp;
  doc["rh"] = rh;
  doc["press"] = press;
  doc["tvoc"] = tvoc;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  Serial.println("Sending:");
  Serial.println(jsonBuffer);

  int httpCode = http.POST((uint8_t*)jsonBuffer, strlen(jsonBuffer));

  if (httpCode > 0) {
    Serial.printf("HTTP code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
      String payload = http.getString();
      Serial.println("Response:");
      Serial.println(payload);
    } else {
      Serial.println("Server responded with error");
    }
  } else {
    Serial.printf("HTTP request failed: %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();
}
//####################################################################################################################################################################################
//===================================================================== графика ======================================================================================================
//####################################################################################################################################################################################
//==== рисование домашнего экрана с данными в реал. времени===========================================================================================================================
void drawHomePage() {
  //tft.fillScreen(TFT_GREEN);
  drawbigBlock(10, 10, title[order[0]], value[order[0]], unit[order[0]], color[order[0]]); // 0
    
  drawBlock(10, 120, title[order[4]], value[order[4]], unit[order[4]], color[order[4]]); // 4
    
  drawBlock(125, 120, title[order[1]], value[order[1]], unit[order[1]], color[order[1]]); // 1
    
  drawBlock(10, 220, title[order[3]], value[order[3]], unit[order[3]], color[order[3]]); // 3
    
  drawBlock(125, 220, title[order[2]], value[order[2]], unit[order[2]], color[order[2]]); // 2
  //{10, 120, 220, 220, 120}
}
//==== рисование экрана определённого параметра с прорисовкой графиков ===============================================================================================================
void drawRoomPage() {
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(title[order[0]], 0, 0, 4);
}
//==== прокрутка массива виджетов (прокрутка виджетов) ===============================================================================================================================
void rotateWidgets() {
  int last = order[N - 1];

  for (int i = N - 1; i > 0; i--) {
    order[i] = order[i - 1];
  }

  order[0] = last;
}
//==== рисование маленьких блоков на домашнем экране с 1 параметром ==================================================================================================================
void drawBlock(int x, int y, String title, String value, String unit, uint16_t color) {

  tft.drawRoundRect(x, y, 105, 90, 8, TFT_PURPLE);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(title, x+10, y+10, 2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(value, x+10, y+35, 4);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(unit, x+70, y+40, 2);

  tft.fillRoundRect(x+10, y+75, 85, 6, 3, color);
}

//==== рисование маленьких блоков на домашнем экране с 1 параметром ==================================================================================================================
void drawbigBlock(int x, int y, String title, String value, String unit, uint16_t color) {

  tft.drawRoundRect(x, y, 220, 100, 8, TFT_PURPLE);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(title, x+10, y+10, 2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(value, x+10, y+35, 4);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(unit, x+70, y+40, 2);

  tft.fillRoundRect(x+10, y+85, 200, 6, 3, color);
}
//==== обновление экрана =============================================================================================================================================================
void updateScreen() {
  tft.fillScreen(TFT_BLACK);
  if (page == 0) {
    drawHomePage();
  }
  if (page == 1) {
    drawRoomPage();
  }
}
void updateVigets(int object) {
  if (page != 0) return;

  for (int i = 0; i < N; i++) {
    if (order[i] == object) {
      tft.fillRect(orderCoordX[i] + 10, orderCoordY[i] + 35, 59, 26, TFT_BLACK);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(value[object], orderCoordX[i] + 10, orderCoordY[i] + 35, 4);
      if (i == 0) {
        tft.fillRoundRect(orderCoordX[i]+10, orderCoordY[i]+85, 200, 6, 3, color[object]);
      } else if (i != 0) {
        tft.fillRoundRect(orderCoordX[i]+10, orderCoordY[i]+75, 85, 6, 3, color[object]);
      }
      break;
    }
  }
}
//####################################################################################################################################################################################
//====================================================================== void loop ===================================================================================================
//####################################################################################################################################################################################
void loop() {
  //####################################################################################################################################################################################
  //================================================================= чтение датчиков ==================================================================================================
  //####################################################################################################################################################################################

  //==== CO2 ===========================================================================================================================================================================
  if (millis() - co2_millis > 5000) {
    co2_millis = millis();
    Serial2.write(measure_cmd, 9);
    if (Serial2.readBytes(response, 9) == 9 && response[0] == 0xFF && response[1] == 0x86) {
      co2 = (256 * (int)response[2]) + (int)response[3];
      
      // Обновляем структуру
      value[0] = String(co2); // Храним число для расчетов, если нужно
      color[0] = co2Color(co2); // Обновляем цвет в зависимости от значения
      updateVigets(0);
    }
  }
  //####################################################################################################################################################################################
  //================================================================= WIFI общение ==================================================================================================
  //####################################################################################################################################################################################
  handleWiFi();

  if (millis() - lastSendTime > sendInterval) {
    lastSendTime = millis();
    
    sendData();        // отправка
  }

  //####################################################################################################################################################################################
  //====================================================================== кнопка ======================================================================================================
  //####################################################################################################################################################################################
  sensorState = digitalRead(btn);

  if (sensorState && !sensorFlag) {
    sensorMillis = millis();
    sensorFlag = 1;
    longDone = 0;
  }
  if (sensorState && sensorFlag && !longDone) {
    if (millis() - sensorMillis > 500) {
      //==== длинное нажатие ===========================================================================================================================================================
      if (page == 0) {
        page = 1;
        updateScreen();
      }
      Serial.println("LONG");
      longDone = 1;
    }
  }
  if (!sensorState && sensorFlag) {
    pressTime = millis() - sensorMillis;

    if (!longDone && pressTime > 50 && pressTime < 400) {
      //==== короткое нажатие ==========================================================================================================================================================
      Serial.println("SHORT");
      if (page == 1) {
        page = 0;
        updateScreen();
      } else if (page == 0) {
        rotateWidgets();
        updateScreen();
      }
    }
  sensorFlag = 0;
  }
}