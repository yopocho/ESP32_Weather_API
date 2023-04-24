#ifndef MAIN_HPP_
#define MAIN_HPP_

#include <Arduino.h>
// #include <OneWire.h>
// #include <DallasTemperature.h>
// #include <NonBlockingDallas.h> 
#include <String.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <ArduinoBLE.h>
#include <Preferences.h>
#include <AccelStepper.h>
#include <max6675.h>
#include "esp_timer.h"
#include "sdkconfig.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
  #error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define DEBUG

#define ERR_NO_DATA "No data returned"
#define ERR_NO_WIFI "No wifi connection established"

#define NOT_SLIPPERY_POS 0
#define SLIGHTLY_SLIPPERY_POS 45
#define QUITE_SLIPPERY_POS 90
#define REALLY_SLIPPERY_POS 135
#define INSANELY_SLIPPERY_POS 180

#define CHARACTERISTIC_SIZE 512 //BLE

#define JSON_CAPACITY 2048 //JSON

#define TEMP_TIMEOUT 250 //MAX6675 readout interval

#define serviceUUID "1289b0a6-c715-11ed-afa1-0242ac120002"
#define languageCharacteristicUUID "1289b3f8-c715-11ed-afa1-0242ac120002"
#define locationCharacteristicUUID "1289b81c-c715-11ed-afa1-0242ac120002"

/* Weatherstation data-entry */
class weatherstationObject //Contains default values
{
  private:
    const String key = "63463198104ddccafc13815f6d61e642";
  public:
    const uint32_t APITimeout = 30000;
    float measuredTemp = 0.0f;
    float targetTemp = 20.0f; 
    uint32_t timeout = 999;
    const String endpointCurrent = "http://api.openweathermap.org/data/2.5/weather?";
    const String endpoint3hour5days = "http://api.openweathermap.org/data/2.5/forecast?";
    String lattitude = "52.0000";
    String longitude = "5.0000";
    String language = "nl";
    String units = "metric";
    String stepCount = "1";
    String callOpenWeatherMapAPI(String endpoint);
};
weatherstationObject weatherStation;

typedef struct
{
  private:
  public:
  int weatherID = 0;
  String weatherDescription = "";
  float windChillTemperature = 0.0;
  float actualTemperature = 0.0;
  float windSpeed = 0.0;
} weatherReportObject;
weatherReportObject weatherReportCurrent;
weatherReportObject weatherReportUpcoming;

/* WiFi def. */
const char* ssid = "Niels hotspot";
const char* password =  "Ikwilkaas";

/* MAX6675 Thermocouple def. */
const int thermocoupleCLK = 12;
const int thermocoupleCS = 10;
const int thermocoupleMISO = 11;
MAX6675 thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleMISO);
// const int oneWireBus = 4;  
// OneWire oneWire(oneWireBus);
// DallasTemperature sensors(&oneWire);
// NonBlockingDallas tempSensor(&sensors);

/* Pin def. for heatsink fan control */
const int peltierFanPWMPin = 6;

/* Pin def. for wind fan control */
const int windFanPWMPin = 7;

/* Pin def. for peltier H-Bridge */
const int peltierHeat = 9;
const int peltierCool = 46;

/* Pin def. for Neopixel data */
const int neopixelPin = 16;

/* Pin def. servo */
const int servoPin = 15;
Servo slipperinessDiscServo;

/* Precipitation Stepper */
const int directionPin = 38; 
const int stepPin = 39; 
const int enablePin = 40; 
AccelStepper stepper(AccelStepper::DRIVER, stepPin, directionPin);

/* Pin def. User Interface Buttons */
const int buttonCurrentWeather = 47;
const int buttonUpcomingWeather = 48;
const float tempHysteresis = 0.5;

/* Pin def. vibration motor */
const int motorEnable = 21;

/* API Time-out timer */
uint32_t previousMillisAPI = 0;

/* Temperature time-out timer */
uint32_t previousMillisTemp = 0;

/* Timed processes */
esp_timer_handle_t timerWeatherFlags;
volatile bool flagCurrentWeather = false;
volatile bool flagUpcomingWeather = false;

/* Millis delay */
uint32_t previousMillis = 0;

/* Bluetooth */
BLEService weatherstationService(serviceUUID); // create service
BLEStringCharacteristic languageCharacteristic(languageCharacteristicUUID, BLERead | BLEWrite | BLENotify, CHARACTERISTIC_SIZE);
BLEStringCharacteristic locationCharacteristic(locationCharacteristicUUID, BLERead | BLEWrite | BLENotify, CHARACTERISTIC_SIZE);

/* EEPROM */
Preferences preferences;

/* Task for multi-core utilization */
TaskHandle_t Task1;
bool displayPrecipitationFlag = false;

/* Function prototypes */
void setup();
void loop();
void setLocation(String location);
void setLanguage(String language);

/* Function definitions */
String weatherstationObject::callOpenWeatherMapAPI(String endpoint)
{
  if ((WiFi.status() == WL_CONNECTED)) 
  { //Check the current connection status

    HTTPClient http;

    String combinedEndpoint =  endpoint + "lang=" + language + '&' + "lat=" + lattitude + '&' + "lon=" + longitude + '&' + "units=" + units + '&' + "appid=" + key;
    if(endpoint.indexOf("forecast")) combinedEndpoint = combinedEndpoint + '&' + "cnt=" + weatherStation.stepCount;

    #ifdef DEBUG
      Serial.print("Endpoint: ");
      Serial.println(combinedEndpoint);
    #endif

    http.begin(combinedEndpoint); //Specify the URL
    int httpCode = http.GET();  //Make the request

    if (httpCode > 0) //Check for the returning code
    { 
        String payload = http.getString();
        http.end(); //Free the resources
        return payload;
    }
    else 
    {
      http.end(); //Free the resources
      return ERR_NO_DATA;  
    }  
  }
  return ERR_NO_WIFI;
}


int initGPIO()
{
    /* Peltier H-Bridge setup */
  pinMode(peltierCool, OUTPUT);
  pinMode(peltierHeat, OUTPUT);
  pinMode(buttonCurrentWeather, INPUT);
  pinMode(buttonUpcomingWeather, INPUT);

  /* Init servo */
	ESP32PWM::allocateTimer(2);
  slipperinessDiscServo.setPeriodHertz(50);
  if(!slipperinessDiscServo.attach(servoPin)) return -1;

  return 1;
}


DynamicJsonDocument parseJSON(String payload)
{
  DynamicJsonDocument weatherReport(JSON_CAPACITY);

  DeserializationError error = deserializeJson(weatherReport, payload);

  #ifdef DEBUG
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    }
  #endif

  return weatherReport;
}


void controlLoop(float temp)
{

  bool higherFlag = false;
  bool lowerFlag = false;

  if(temp > weatherStation.targetTemp + tempHysteresis) lowerFlag = true;
  else if(temp < weatherStation.targetTemp - tempHysteresis) higherFlag = true;
  else
  {
    higherFlag = false;
    lowerFlag = false;
  }

  if (higherFlag)
  {
    digitalWrite(peltierCool, LOW);
    digitalWrite(peltierHeat, HIGH);
    digitalWrite(peltierFanPWMPin, LOW); 
    higherFlag = false;
    #ifdef DEBUG
      Serial.println("Raising temperature");
    #endif
  }
  else if (lowerFlag)
  {
    digitalWrite(peltierHeat, LOW);
    digitalWrite(peltierCool, HIGH);
    digitalWrite(peltierFanPWMPin, HIGH); 
    lowerFlag = false;
    #ifdef DEBUG
      Serial.println("Lowering temperature");
    #endif
  }
  else if(!lowerFlag && !higherFlag)
  {
    digitalWrite(peltierCool, LOW);
    digitalWrite(peltierHeat, LOW);
    digitalWrite(peltierFanPWMPin, LOW); 
  }
}


void updateTemperature()
{
  weatherStation.measuredTemp = thermocouple.readCelsius();
  #ifdef DEBUG
    Serial.print("Measured temperature: ");
    Serial.println(weatherStation.measuredTemp);
  #endif
}


int setSlipperinessServo(int weatherID)
{
  /* Levels of slipperiness and their respective position on the tactile disc servo */
  uint16_t notSlippery = NOT_SLIPPERY_POS;
  uint16_t slightlySlippery = SLIGHTLY_SLIPPERY_POS;
  uint16_t quiteSlippery = QUITE_SLIPPERY_POS;
  uint16_t reallySlippery = REALLY_SLIPPERY_POS;
  uint16_t insanelySlippery = INSANELY_SLIPPERY_POS;

  switch(weatherID)
  {
    case 200:
    case 201:
    case 202:
    case 210:
    case 211:
    case 212:
    case 230:
    case 231:
    case 232:
    case 300:
    case 301:
    case 302:
    case 310:
    case 311:
    case 312:
    case 313:
    case 314:
    case 321:
    case 500:
    case 501:
    case 502:
    case 503:
    case 511:
    case 520:
    case 521:
    case 522:
    case 531:
      slipperinessDiscServo.write(slightlySlippery); //We define all rain as slightly slippery here (TBD)
      #ifdef DEBUG
        Serial.println("Setting disc to slightly slippery");
      #endif
      break;
    case 600:
    case 601:
    case 602:
    case 611:
    case 612:
    case 613:
    case 615:
    case 616:
    case 620:
    case 621:
    case 622:
      slipperinessDiscServo.write(quiteSlippery); //We define all snow here as quite slippery
      #ifdef DEBUG
        Serial.println("Setting disc to quite slippery");
      #endif
      break;
    case 701:
    case 711:
    case 721:
    case 731:
    case 741:
    case 751:
    case 761:
    case 762:
    case 771:
    case 781:
    case 800:
    case 801:
    case 802:
    case 803:
    case 804:
      slipperinessDiscServo.write(notSlippery);
      #ifdef DEBUG
        Serial.println("Setting disc to not slippery");
      #endif
      break;
    default:
      return -1;
      break;
  } 
  return 1;
}


void getSmoothness(void)
{

}


bool APITimeout(uint32_t delayAmount)
{
  uint32_t timeoutMillis = delayAmount;
  uint32_t currentMillis = millis();
  if (currentMillis > (previousMillisAPI + timeoutMillis))
  {
    previousMillisAPI = currentMillis;
    return true;
  }
  else return false;
}

bool tempTimeout(uint32_t delayAmount)
{
  uint32_t timeoutMillis = delayAmount;
  uint32_t currentMillis = millis();
  if (currentMillis > (previousMillisTemp + timeoutMillis))
  {
    previousMillisTemp = currentMillis;
    return true;
  }
  else return false;
}

uint16_t getWeatherID(DynamicJsonDocument weatherReport)
{
  int weatherID = 0;
  String weatherStrings[3];
  int stringIterator = 0;

  JsonArray weatherArray = weatherReport["weather"].as<JsonArray>();
  
  stringIterator = 0;
  for (JsonObject a : weatherArray) 
  {
    for (JsonPair kv : a) 
    {
        if(kv.value().is<int>()) weatherID = kv.value().as<int>();
        else weatherStrings[stringIterator++] = kv.value().as<const char*>();
    }
  }
  
  return weatherID;
}


String getWeatherDescription(DynamicJsonDocument weatherReport)
{
  String weatherStrings[3];
  int stringIterator = 0;

  JsonArray weatherArray = weatherReport["weather"].as<JsonArray>();
  
  stringIterator = 0;
  for (JsonObject a : weatherArray) 
  {
    for (JsonPair kv : a) 
    {
        if(kv.value().is<int>());
        else weatherStrings[stringIterator++] = kv.value().as<const char*>();
    }
  }

  return weatherStrings[1];
}


void dataHandlerLanguageCharacteristic(BLEDevice central, BLECharacteristic characteristic) {
  String language = languageCharacteristic.value();
  #ifdef DEBUG
    Serial.print("Characteristic event, written: ");
    Serial.println(language);
  #endif
  setLanguage(language);
}


void setLanguage(String language)
{
  preferences.putString("Language", language);
  weatherStation.language = preferences.getString("Language", "NaN");
  #ifdef DEBUG
    Serial.print("New language: ");
    Serial.println(preferences.getString("Language", "NaN"));
  #endif
}


void dataHandlerLocationCharacteristic(BLEDevice central, BLECharacteristic characteristic) {
  String location = locationCharacteristic.value();
  #ifdef DEBUG
    Serial.print("Characteristic event, written: ");
    Serial.println(location);
  #endif
  setLocation(location);
}


void setLocation(String location)
{
  preferences.putString("Lattitude", location.substring(0, location.indexOf(',')));
  preferences.putString("Longitude", location.substring((location.indexOf(',') + 1), (location.length() - 1)));
  weatherStation.lattitude = preferences.getString("Lattitude", "0.0000");
  weatherStation.longitude = preferences.getString("Longitude", "0.0000");
  #ifdef DEBUG
    Serial.print("New lattitude: ");
    Serial.println(preferences.getString("Lattitude", "NaN"));
    Serial.print("New longitude: ");
    Serial.println(preferences.getString("Longitude", "NaN"));
  #endif
}


void connectHandlerBLE(BLEDevice central) {
  #ifdef DEBUG
    Serial.print("Connected event, central: ");
    Serial.println(central.address());
  #endif
}


void disconnectHandlerBLE(BLEDevice central) {
  #ifdef DEBUG
    Serial.print("Disconnected event, central: ");
    Serial.println(central.address());
  #endif
}


bool initBLE()
{
  BLE.setLocalName("Tactiel Weerstation");
  BLE.setAdvertisedService(weatherstationService);
  weatherstationService.addCharacteristic(languageCharacteristic);
  weatherstationService.addCharacteristic(locationCharacteristic);
  BLE.setEventHandler(BLEConnected, connectHandlerBLE);
  BLE.setEventHandler(BLEDisconnected, disconnectHandlerBLE);
  languageCharacteristic.setEventHandler(BLEWritten, dataHandlerLanguageCharacteristic);
  locationCharacteristic.setEventHandler(BLEWritten, dataHandlerLocationCharacteristic);
  BLE.addService(weatherstationService);
  BLE.advertise();
  return true;
}


float getWindChillTemperature(DynamicJsonDocument weatherReport)
{
  return weatherReport["main"]["feels_like"];
}


void IRAM_ATTR ISRWeatherFlags(void *arg)
{
  flagCurrentWeather = false;
  flagUpcomingWeather = false;
}


void initTimers()
{
  esp_timer_create_args_t timerWeatherFlagsArgs = {};
  timerWeatherFlagsArgs.callback = &ISRWeatherFlags;
  esp_timer_create(&timerWeatherFlagsArgs, &timerWeatherFlags);
}

void idle()
{
  /* Ensure peltier is off */
  if(digitalRead(peltierCool)) digitalWrite(peltierCool, LOW);
  if(digitalRead(peltierHeat)) digitalWrite(peltierHeat, LOW);

  /* Disable precipitation stepper */
  digitalWrite(enablePin, HIGH);
  displayPrecipitationFlag = false;

  /* Handle BLE */
  BLE.poll();

  /* Keep temperature up-to-date */
  updateTemperature();

  //test
  Serial.print("idle() running on core ");
  Serial.println(xPortGetCoreID());
}

void displayPrecipitation(void *pvParameters)
{
  if(displayPrecipitationFlag)
  {
    digitalWrite(enablePin, LOW);
    if (stepper.distanceToGo() == 0) stepper.moveTo(-stepper.currentPosition()); 
    stepper.run(); 
  }
}

void displayWeather(weatherReportObject *weatherReport)
{
  /* Handle precipitation */
  displayPrecipitationFlag = true;

  /* Handle temperature */
  // weatherStation.targetTemp = weatherReport->windChillTemperature; GEVOEL IS NIET LINEAIR, MOET EEN f(x) FUNCTIE WORDEN
  updateTemperature();
  controlLoop(weatherStation.measuredTemp);
  #ifdef DEBUG
    Serial.print(weatherStation.measuredTemp);
    Serial.println("ºC");
  #endif
  
  // /* Handle slipperiness */
  // if (!setSlipperinessServo(weatherReport->weatherID)) 
  // {
  //   #ifdef DEBUG
  //     Serial.println("Error: weatherID not valid");
  //   #endif
  // }
}


void updateWeatherReports()
{
  /*TODO: IPV dat nu alleen het huidige weer opgehaald wordt, ook tegelijkertijd de 4uur weerbericht doen en in structs gooien*/
  // int weatherID = getWeatherID(parseJSON(callOpenWeatherAPI(endpoint, key))); //Condensed version of below ;)
  String weatherCurrentString = weatherStation.callOpenWeatherMapAPI(weatherStation.endpointCurrent);
  DynamicJsonDocument weatherCurrentJSON(JSON_CAPACITY);
  weatherCurrentJSON = parseJSON(weatherCurrentString);
  weatherReportCurrent.weatherID = getWeatherID(weatherCurrentJSON);
  weatherReportCurrent.weatherDescription = getWeatherDescription(weatherCurrentJSON);
  weatherReportCurrent.windChillTemperature = getWindChillTemperature(weatherCurrentJSON); //Gevoelstemperatuur heeft de meeste relevantie
  
  String weatherUpcomingString = weatherStation.callOpenWeatherMapAPI(weatherStation.endpoint3hour5days);
  DynamicJsonDocument weatherUpcomingJSON(JSON_CAPACITY);
  weatherUpcomingJSON = parseJSON(weatherUpcomingString);
  weatherReportUpcoming.weatherID = getWeatherID(weatherUpcomingJSON);
  weatherReportUpcoming.weatherDescription = getWeatherDescription(weatherUpcomingJSON);
  weatherReportUpcoming.windChillTemperature = getWindChillTemperature(weatherUpcomingJSON);

  #ifdef DEBUG
    Serial.println("-----Current weather-----");
    Serial.println(weatherCurrentString);
    Serial.print("Weather ID: ");
    Serial.println(weatherReportCurrent.weatherID);
    Serial.print("Het huidige weer is: ");
    Serial.println(weatherReportCurrent.weatherDescription);
    Serial.print("De gevoelstemperatuur is: ");
    Serial.println(weatherReportCurrent.windChillTemperature);

    Serial.println("\n-----Upcoming weather-----");
    Serial.println(weatherUpcomingString);
    Serial.print("Weather ID: ");
    Serial.println(weatherReportUpcoming.weatherID);
    Serial.print("het opkomende weer zal zijn: ");
    Serial.println(weatherReportUpcoming.weatherDescription);
    Serial.print("De gevoelstemperatuur zal zijn: ");
    Serial.println(weatherReportUpcoming.windChillTemperature);
  #endif
}


#endif

