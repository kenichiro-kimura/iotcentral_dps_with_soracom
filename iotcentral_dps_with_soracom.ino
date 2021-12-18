/**
 * Azure IoT Central example for esp32-azure-kit.
 */
#include <WiFi.h>
#include <M5Stack.h>
#include "SparkFun_SCD4x_Arduino_Library.h"
#include "seeed_bme680.h"
#include "AzureIotHub.h"
#define TINY_GSM_MODEM_UBLOX
#include <TinyGsmClient.h>
#include <HTTPClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#define TELEMETRY_INTERVAL 10000
#define SENSOR_LOOP_DELAY 5000

SCD4x mySensor;
#define IIC_ADDR  uint8_t(0x76)
Seeed_BME680 bme680(IIC_ADDR);

// Please input the SSID and password of WiFi
const char *ssid = "************";
const char *password = "***********";

char connectionString[256];

IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle = NULL;
static int trackingId = 0;
static char propText[1024];
static char msgText[1024];

typedef struct EVENT_MESSAGE_INSTANCE_TAG
{
  IOTHUB_MESSAGE_HANDLE messageHandle;
  size_t messageTrackingId; // For tracking the messages within the user callback.
} EVENT_MESSAGE_INSTANCE_TAG;

static bool hasIoTHub = false;
static bool hasWifi = false;
static uint64_t send_interval_ms;

TinyGsm modem(Serial2); /* 3G board modem */
TinyGsmClient socket(modem);

static void sendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback);

static bool initIotHubClient(void)
{
  M5.Lcd.println(F("initIotHubClient Start!"));

  if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, MQTT_Protocol)) == NULL)
  {
    M5.Lcd.println(F("ERROR: iotHubClientHandle is NULL!"));
    return false;
  }

  IoTHubClient_LL_SetRetryPolicy(iotHubClientHandle, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF, 1200);
  bool traceOn = true;
  IoTHubClient_LL_SetOption(iotHubClientHandle, "logtrace", &traceOn);

  M5.Lcd.println(F("initIotHubClient End!"));

  return true;
}

static void closeIotHubClient()
{
  if (iotHubClientHandle != NULL)
  {
    IoTHubClient_LL_Destroy(iotHubClientHandle);
    platform_deinit();
    iotHubClientHandle = NULL;
  }
  M5.Lcd.println(F("closeIotHubClient!"));
}

static void sendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void *userContextCallback)
{
  EVENT_MESSAGE_INSTANCE_TAG *eventInstance = (EVENT_MESSAGE_INSTANCE_TAG *)userContextCallback;
  size_t id = eventInstance->messageTrackingId;

  M5.Lcd.print(F("Confirmation received for message tracking id = "));
  M5.Lcd.print(id);
  M5.Lcd.print(F(" with result = "));
  M5.Lcd.println(ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));

  IoTHubMessage_Destroy(eventInstance->messageHandle);
  free(eventInstance);
}

static void sendTelemetry(const char *payload)
{
  EVENT_MESSAGE_INSTANCE_TAG *thisMessage = (EVENT_MESSAGE_INSTANCE_TAG *)malloc(sizeof(EVENT_MESSAGE_INSTANCE_TAG));
  thisMessage->messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char *)payload, strlen(payload));

  if (thisMessage->messageHandle == NULL)
  {
    M5.Lcd.println(F("ERROR: iotHubMessageHandle is NULL!"));
    free(thisMessage);
    return;
  }

  thisMessage->messageTrackingId = trackingId++;

  MAP_HANDLE propMap = IoTHubMessage_Properties(thisMessage->messageHandle);

  (void)sprintf_s(propText, sizeof(propText), "PropMsg_%zu", trackingId);
  if (Map_AddOrUpdate(propMap, "PropName", propText) != MAP_OK)
  {
    M5.Lcd.println(F("ERROR: Map_AddOrUpdate Failed!"));
  }

  // send message to the Azure Iot hub
  if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle,
                                     thisMessage->messageHandle, sendConfirmationCallback, thisMessage) != IOTHUB_CLIENT_OK)
  {
    M5.Lcd.println(F("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!"));
    return;
  }

  IoTHubClient_LL_DoWork(iotHubClientHandle);
  M5.Lcd.println(F("IoTHubClient sendTelemetry completed!"));
}

void setup()
{
  M5.begin();
  M5.Power.begin();
  M5.Lcd.println(F("Initializing..."));
  M5.Lcd.println(F("ESP32 Device"));
  M5.Lcd.println(F("Initializing..."));

  M5.Lcd.println(F("> M5Stack + 3G Module"));

  M5.Lcd.print(F("modem.restart()"));
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  modem.restart();
  M5.Lcd.println(F("done"));

  M5.Lcd.print(F("getModemInfo:"));
  String modemInfo = modem.getModemInfo();
  M5.Lcd.println(modemInfo);

  M5.Lcd.print(F("waitForNetwork()"));
  while (!modem.waitForNetwork()) M5.Lcd.print(".");
  M5.Lcd.println(F("Ok"));

  M5.Lcd.print(F("gprsConnect(soracom.io)"));
  modem.gprsConnect("soracom.io", "sora", "sora");
  M5.Lcd.println(F("done"));

  M5.Lcd.print(F("isNetworkConnected()"));
  while (!modem.isNetworkConnected()) M5.Lcd.print(".");
  M5.Lcd.println(F("Ok"));
  
  HttpClient http = HttpClient(socket,"funk.soracom.io",80);
  M5.Lcd.println(F("try to provisioning.."));
  sprintf(msgText,"{}");
  http.post("/","application/json",msgText);
  int status_code = http.responseStatusCode();
  String response_body = http.responseBody();
  M5.Lcd.printf("http status:%d\n", status_code);

  if(status_code == 200){
    StaticJsonDocument<512> json_response;
    deserializeJson(json_response, response_body);
    const int statusCode = json_response["statusCode"];
    const char* body = json_response["body"];
    if(statusCode == 200){
      strcpy(connectionString,body);
    } else {
      M5.Lcd.println(F("fail to device provisioning"));
      M5.Lcd.printf("statusCode:%d\n", statusCode);
      M5.Lcd.printf("body:%s\n", body);
      while(1);
    }
  } else {
      M5.Lcd.println(F("fail to device provisioning"));
      while(1);
  }
  http.stop();
  M5.Lcd.println(F("success provisioning"));

  M5.Lcd.println(F(" > WiFi"));
  M5.Lcd.println(F("Starting connecting WiFi."));

  WiFi.mode(WIFI_AP);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Lcd.print(".");
    hasWifi = false;
  }
  hasWifi = true;

  M5.Lcd.println(F("WiFi connected"));
  M5.Lcd.println(F("IP address: "));
  M5.Lcd.println(WiFi.localIP());

  M5.Lcd.println(F("justify system clock by ntp.."));
  configTime(9 * 60 * 60, 0, "ntp.jst.mfeed.ad.jp", "ntp.nict.jp", "time.google.com");

  M5.Lcd.println(F(" > IoT Hub"));
  if (!initIotHubClient())
  {
    hasIoTHub = false;
    M5.Lcd.println(F("Initializing IoT hub failed."));
    while(1);
  }
  hasIoTHub = true;

  M5.Lcd.println(F(" > Initialize sensor device"));

  if (mySensor.begin() == false)
  {
    M5.Lcd.println(F("Sensor not detected. Please check wiring. Freezing..."));
    while (1);
  }

  M5.Lcd.print(F("Initializing BME680 sensor\n"));
  while (!bme680.init()) {
    M5.Lcd.print(F("bme680 init failed ! can't find device!\n"));
    delay(5000);
  }

  M5.Lcd.println(F("Start sending events."));
  send_interval_ms = millis();
  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(2);
}

void loop()
{
  if(mySensor.readMeasurement() && !bme680.read_sensor_data())
  {
    int color = WHITE;
    float temperature = mySensor.getTemperature();
    float humidity = mySensor.getHumidity();
    uint16_t co2 = mySensor.getCO2();
    float temperature2 = bme680.sensor_result_value.temperature;
    float humidity2 = bme680.sensor_result_value.humidity;
    float pressure = bme680.sensor_result_value.pressure / 100.0;
    float gas = bme680.sensor_result_value.gas / 1000.0;    

    if(co2 > 3500){
      color = PURPLE;
    } else if (co2 > 2500){
      color = RED;
    } else if (co2 > 1500){
      color = YELLOW;
    } else if (co2 > 1000){
      color = GREEN;
    }

    M5.Lcd.clear(color);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("TMP: %.2f / %.2f c\n", temperature,temperature2);
    M5.Lcd.printf("HUM: %.2f / %.2f %%\n", humidity, humidity2);
    M5.Lcd.printf("CO2: %d ppm\n", co2);
    M5.Lcd.printf("GAS:%.2f Kohms\n",gas);
    M5.Lcd.printf("PRS:%.2f hPa\n\n",pressure);
 
    if (hasWifi && hasIoTHub)
    {
      if ((int)(millis() - send_interval_ms) >= TELEMETRY_INTERVAL)
      {
        sprintf_s(msgText, sizeof(msgText),
                  "{\"Temperature\":%.2f,\"Humidity\":%.2f,\"CO2\":%d,\"Temperature2\":%.2f,\"Humidity2\":%.2f,\"Gas\":%.2f,\"Pressure\":%.2f}",
                  temperature, humidity, co2, temperature2, humidity2, gas, pressure);
        sendTelemetry(msgText);
        send_interval_ms = millis();
      }
    }
  }
  delay(SENSOR_LOOP_DELAY);
}