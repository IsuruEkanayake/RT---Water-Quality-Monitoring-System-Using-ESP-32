#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#define WIFI_SSID "Wifi Name" // Wifi name
#define WIFI_PASSWORD "Password" //Password

#define API_KEY "Your API Key"
#define DATABASE_URL "Your Database URL"

#define USER_EMAIL "Example@gmail.com" //Firebase Login
#define USER_PASSWORD "12345678"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

#define tempSensor 32
OneWire oneWire(tempSensor);
DallasTemperature sensors(&oneWire);

#define turbiditySensor 33

#define tdsSensor 34
#define vRef 3.3
#define sCount 30

#define pHSensor 35


typedef struct {
  float ph;
  float temp;
  int turbidity;
  int tds;
  int notification;

} waterParameters_t;

waterParameters_t sensorData = { 0 };
SemaphoreHandle_t dataMutex;

void setup() {

  Serial.begin(115200);
  analogReadResolution(12);
  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while (1)
      ;
  }
  sensors.begin();
  wifiConnect();
  dbConnect();

  xTaskCreatePinnedToCore(
    readTemp,    // function
    "ReadTemp",  // name
    2048,        // Stack size
    NULL,        // Parameters
    1,           // Priority
    NULL,        // Task handle
    1);

  xTaskCreatePinnedToCore(
    readTurbidity,
    "ReadTurbidity",
    2048,
    NULL,
    1,
    NULL,
    1);


  xTaskCreatePinnedToCore(
    readTDS,
    "ReadTds",
    2048,
    NULL,
    1,
    NULL,
    1);

  xTaskCreatePinnedToCore(
    readpH,
    "ReadpH",
    2048,
    NULL,
    1,
    NULL,
    1);

  xTaskCreatePinnedToCore(
    notification,
    "notification",
    2048,
    NULL,
    1,
    NULL,
    0);

  xTaskCreatePinnedToCore(
    passData,
    "PassData",
    12288,
    NULL,
    2,
    NULL,
    0);
}

void loop() {
}


void wifiConnect() {

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WIFI");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("Connected with IP : ");
  Serial.print(WiFi.localIP());
  Serial.println();
}

void dbConnect() {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi Connection failed. Reconnecting WiFi..");
    wifiConnect();
  }

  if (WiFi.status() == WL_CONNECTED) {

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (Firebase.ready()) {

      Serial.println("Signed in successfully!");
      Serial.printf("User UID: %s\n", auth.token.uid.c_str());
    }
  }
}

void passData(void *Parameters) {

  waterParameters_t receivedData;
  while (true) {

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      memcpy(&receivedData, &sensorData, sizeof(waterParameters_t));
      xSemaphoreGive(dataMutex);
    }

    if (Firebase.ready()) {
      FirebaseJson json;
      json.set("temperature", receivedData.temp);
      json.set("turbidity", receivedData.turbidity);
      json.set("tds", receivedData.tds);
      json.set("ph", receivedData.ph);
      json.set("notification", receivedData.notification);


      if (Firebase.RTDB.updateNode(&fbdo, "isurusandun/Tank%201", &json)) {
        Serial.println("Data sent successfully!");
      } else {
        Serial.print("Firebase error: ");
        Serial.println(fbdo.errorReason());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void readTemp(void *Parameters) {

  while (true) {
    sensors.requestTemperatures();
    float tempValue = sensors.getTempCByIndex(0);
    //Serial.println(sensorData.temp);
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      sensorData.temp = tempValue;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void readTDS(void *Parameters) {

  float temp;

  while (true) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      temp = sensorData.temp;
      xSemaphoreGive(dataMutex);
    } else {
      temp = 25;
    }

    int sum = 0;
    for (int i = 0; i < sCount; i++) {
      sum += analogRead(tdsSensor);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    float average = sum / (float)sCount;
    float voltage = average * (vRef / 4095.0);
    float compensationCoefficient = 1.0 + 0.02 * (temp - 25.0);
    float compensationVoltage = voltage / compensationCoefficient;
    float tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage - 255.86 * compensationVoltage * compensationVoltage + 857.39 * compensationVoltage) * 0.5;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      sensorData.tds = tdsValue;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void readpH(void *Parameters) {

  float vpH4 = 4.37;
  float vpH7 = 3.39;

  while (true) {
    int pHRawValue = analogRead(pHSensor);
    float voltage = pHRawValue * (3.3 / 4095.0);
    float slope = (7.0 - 4.0) / (vpH7 - vpH4);
    float intercept = 4.0 - slope * vpH4;
    float pH = slope * voltage + intercept;
    //Serial.println(pH, 2);
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      sensorData.ph = pH;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void readTurbidity(void *Parameters) {

  const float cleanWaterVoltage = 2.5;
  const float turbidWaterVoltage = 2.2;
  const int cleanWaterNTU = 0;
  const int turbidWaterNTU = 15;

  while (true) {
    int rawValue = analogRead(turbiditySensor);
    float voltage = rawValue * (vRef / 4095.0);
    float ntu = mapFloat(voltage, turbidWaterVoltage, cleanWaterVoltage, turbidWaterNTU, cleanWaterNTU);
    ntu = constrain(ntu, 0, 15);
    //Serial.println(ntu);
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      sensorData.turbidity = ntu;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}



void notification(void *Parameters) {

  waterParameters_t parameters;
  bool safe;
  bool prevSafe = true;
  while (true) {

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      memcpy(&parameters, &sensorData, sizeof(waterParameters_t));
      xSemaphoreGive(dataMutex);
    }

    safe = safeCheck(parameters.ph, parameters.tds, parameters.turbidity);

    if (prevSafe && !safe) {

      vTaskDelay(pdMS_TO_TICKS(60000));

      if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&parameters, &sensorData, sizeof(waterParameters_t));
        xSemaphoreGive(dataMutex);
      }
      safe = safeCheck(parameters.ph, parameters.tds, parameters.turbidity);

      if (!safe) {
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
          sensorData.notification = 1;  // water turning safe to unsafe
          xSemaphoreGive(dataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
          sensorData.notification = 0;  // no notify
          xSemaphoreGive(dataMutex);
        }
      }
    }

    else if (!prevSafe && safe) {

      vTaskDelay(pdMS_TO_TICKS(60000));

      if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&parameters, &sensorData, sizeof(waterParameters_t));
        xSemaphoreGive(dataMutex);
      }
      safe = safeCheck(parameters.ph, parameters.tds, parameters.turbidity);
      if (safe) {
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
          sensorData.notification = 2;  // water turning back to safe
          xSemaphoreGive(dataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
          sensorData.notification = 0;
          xSemaphoreGive(dataMutex);
        }
      }
    }
    prevSafe = safe;
    vTaskDelay(pdMS_TO_TICKS(1000));

  }
}

bool safeCheck(float phV, float tdsV, float turbidityV) {
  if ((phV >= 6.5 && phV <= 8.5) && tdsV <= 500 && turbidityV <= 5) {
    return true;
  } else {
    return false;
  }
}