#include <Arduino.h>
#include "main.hpp"

void setup() 
{
    /* Init neopixel status LED */
  FastLED.addLeds<WS2812B, neopixelPin, GRB>(statusNeoPixel, 1);
  setStatusLED(CRGB::Red);


  /* Serial setup */
  Serial.begin(115200);

  /* Bluetooth setup */
  if (!BLE.begin()) {
    #ifdef DEBUG
      Serial.println("starting Bluetooth® Low Energy module failed!");
    #endif
  }
  else
  {
    #ifdef DEBUG
      Serial.println("starting Bluetooth® Low Energy module succesful!");
    #endif
  }

  if(!initBLE())
  {
    #ifdef DEBUG
      Serial.println(" Bluetooth initialization failed");
    #endif
  }
  else
  {
    #ifdef DEBUG
      Serial.println("Bluetooth succesfully initialized");
      Serial.print("Local BLE MAC-Address is: ");
      Serial.println(BLE.address());
    #endif
  }

  /* Wifi setup */
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    
    #ifdef DEBUG
      Serial.println("Connecting to WiFi..");
    #endif
  }
  setStatusLED(CRGB::DarkOrange);
  
  #ifdef DEBUG
    Serial.println("Connected to the WiFi network");
  #endif 

  WiFi.onEvent(WifiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
 
  /* Init one-shot timers for display timeout */
  initTimers();

  int statusCode = 0;
  if(!(statusCode = initGPIO())) 
  {
    #ifdef DEBUG
      Serial.print("Error: ");
      Serial.println(statusCode);
    #endif
  }
  else 
  {
    #ifdef DEBUG
      Serial.println("Succesful pin initialization");
    #endif
  }

  /* EEPROM Retrieval, user definable settings */
  preferences.begin("weatherstation", false);
  weatherStation.lattitude = preferences.getString("Lattitude", weatherStation.lattitude);
  weatherStation.longitude =  preferences.getString("Longitude", weatherStation.longitude);
  weatherStation.language = preferences.getString("Language", weatherStation.language);
  weatherStation.units = preferences.getString("Units", weatherStation.units);
  weatherStation.stepCount = preferences.getString("stepCount", weatherStation.stepCount);
  weatherStation.timeout = preferences.getInt("timeout", weatherStation.timeout);
  
  #ifdef DEBUG
    Serial.println("Known settings fetched from EEPROM");
  #endif

  /* GPIO init. values */
  digitalWrite(peltierCool, LOW);
  digitalWrite(peltierHeat, LOW);
  digitalWrite(motorEnable, LOW);
  ledcWrite(peltierFanPWMChannel, 0);

  /* Stepper task setup */
  xTaskCreatePinnedToCore(
    displayPrecipitation,   /* Task function. */
    "stepperTask",          /* name of task. */
    10000,                  /* Stack size of task */
    NULL,                   /* parameter of the task */
    0,                      /* priority of the task */
    &stepperTask,           /* Task handle to keep track of created task */
    0                       /* pin task to core 0 */   
  );
  vTaskSuspend(stepperTask);

  #ifdef DEBUG
    Serial.println("Initialized function displayPrecipitation()");
  #endif

  /* Precipitation Stepper */
  stepper.setMaxSpeed(6400); 
  stepper.setAcceleration(30000); 
  stepper.moveTo(300); 
  
  /* Initial update to weather reports */
  updateWeatherReports();

  #ifdef DEBUG
    Serial.println("Weatherstation initialization completed");
  #endif

  setStatusLED(CRGB::Green);

}

void loop() 
{

  if(WiFi.status() != WL_CONNECTED) 
  {
    delay(1000);
    #ifdef DEBUG
      Serial.println("Connecting to WiFi..");
    #endif
  }

  while(WiFi.status() == WL_CONNECTED) //TODO: Hier moet wel iets mee wss
  {
    /* Handle updating current weather report */
    if(APITimeout(weatherStation.APITimeout)) updateWeatherReports();

    /* Handle updating temperature reading */
    if(tempTimeout(TEMP_TIMEOUT)) updateTemperature();

    /* Update relevant flags */
    if(digitalRead(buttonCurrentWeather) && !(digitalRead(buttonUpcomingWeather))) 
    {
      esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
      flagCurrentWeather = true;
      doOnceFlag = true;
      setStatusLED(CRGB::DarkOrange);
    }

   if(digitalRead(buttonUpcomingWeather) && !(digitalRead(buttonCurrentWeather)))
    {
      esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
      flagUpcomingWeather = true;
      doOnceFlag = true;
      setStatusLED(CRGB::DarkOrange);
    }

    /* Execute state */
    if(flagCurrentWeather) displayWeather(&weatherReportCurrent);
    else if(flagUpcomingWeather) displayWeather(&weatherReportUpcoming);
    else if(flagTrainingWeather) displayWeather(&weatherReportTraining);
    else idle();
  }
}

/*
 * TODO:
 *  
 *  idee van gastcollegegast: ipv direct Serial.println enzo, gooi alles in een queue met een timestamp en laat een task het uitlezen wanneer er tijd is (Dit saved runtime zelfs tijdens production!)
 *  
 *  DOUBLE CHECK PINMODES SMH
 *  
 *  Add to app: 
 *    WiFi connection listpicker (I guess select SSID and enter key? Has to be a better way maybe?)
 *    Training mode (Set temp, wind, precip, slipperiness)
 *    Actually use the selected language in the app
 *  
 *  Redo slipperiness map?
 *  
 *  Add Doxygen documentation perhaps?
 * 
 *  Redo file/include structure (Seperate files cuz this sucks rn :'))?
 * 
 */