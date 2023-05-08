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
  setStatusLED(CRGB::Orange);
  
  #ifdef DEBUG
    Serial.println("Connected to the WiFi network");
  #endif

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

  while(WiFi.status() != WL_CONNECTED) 
  {
    setStatusLED(CRGB::Red);
    delay(1000);
    #ifdef DEBUG
      Serial.println("Connecting to WiFi..");
    #endif
  }

  while(WiFi.status() == WL_CONNECTED) 
  {
    //Code to test temperature PoC
    int serialreadout = Serial.read();
    if(serialreadout > 0)
    {
      Serial.println(serialreadout);
      switch(serialreadout)
      {
        case 48:
          weatherStation.targetTemp = 17.0f;
          stepper.setMaxSpeed(50); 
          ledcWrite(windFanPWMChannel, 0);
          break;
        case 49:
          weatherStation.targetTemp = 19.0f;
          stepper.setMaxSpeed(100); 
          ledcWrite(windFanPWMChannel, 10);
          break;
        case 50:
          weatherStation.targetTemp = 21.0f;
          stepper.setMaxSpeed(200); 
          ledcWrite(windFanPWMChannel, 25);
          break;
        case 51:
          weatherStation.targetTemp = 23.0f;
          stepper.setMaxSpeed(400); 
          ledcWrite(windFanPWMChannel, 50);
          break;
        case 52:
          weatherStation.targetTemp = 25.0f;
          stepper.setMaxSpeed(800); 
          ledcWrite(windFanPWMChannel, 100);
          break;
        case 53:
          weatherStation.targetTemp = 30.0f;
          stepper.setMaxSpeed(1600); 
          ledcWrite(windFanPWMChannel, 160);
          break;
        case 54:
          weatherStation.targetTemp = 35.0f;
          stepper.setMaxSpeed(3200); 
          ledcWrite(windFanPWMChannel, 200);
          break;
        case 55:
          weatherStation.targetTemp = 40.0f;
          stepper.setMaxSpeed(6400); 
          ledcWrite(windFanPWMChannel, 255);
          break;
      };
    }

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
    }

    if(digitalRead(buttonUpcomingWeather) && !(digitalRead(buttonCurrentWeather)))
    {
      esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
      flagUpcomingWeather = true;
      doOnceFlag = true;
    }

    /* Execute state */
    if(flagCurrentWeather) displayWeather(&weatherReportCurrent);
    else if(flagUpcomingWeather) displayWeather(&weatherReportUpcoming);
    else idle();
  }
}

/*
 * TODO:
 *  
 *  DOUBLE CHECK PINMODES SMH
 *  
 *  Add temperature map
 *  
 *  Add to app: 
 *    WiFi connection listpicker (I guess select SSID and enter key? Has to be a better way maybe?)
 *    Training mode (Set temp, wind, precip, slipperiness)
 *    Actually use the selected language in the app
 *  
 *  Redo slipperiness map
 *  
 *  Add Doxygen documentation perhaps?
 * 
 *  Redo file/include structure (Seperate files cuz this sucks rn :'))
 * 
 *  Add statusled logic 
 * 
 */