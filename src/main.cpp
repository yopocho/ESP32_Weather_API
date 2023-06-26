#include <Arduino.h>
#include "main.hpp"

void setup() 
{
    /* Serial setup */
  Serial.begin(115200);

  #ifdef DEBUG
    Serial.println("ESP32 restarted");
  #endif

  /* Init neopixel status LED */
  FastLED.addLeds<WS2812B, neopixelPin, GRB>(statusNeoPixel, 1);
  setStatusLED(CRGB::Red);

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


  /* Init one-shot timers */
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
    setStatusLED(CRGB::DarkOrange);

  /* EEPROM Retrieval, user definable settings */
  preferences.begin("weatherstation", false);
  weatherStation.lattitude = preferences.getString("Lattitude", weatherStation.lattitude);
  weatherStation.longitude =  preferences.getString("Longitude", weatherStation.longitude);
  weatherStation.language = preferences.getString("Language", weatherStation.language);
  weatherStation.units = preferences.getString("Units", weatherStation.units);
  weatherStation.stepCount = preferences.getString("stepCount", weatherStation.stepCount);
  weatherStation.timeout = preferences.getInt("timeout_integer", weatherStation.timeout);
  weatherStation.SSID = preferences.getString("SSID", weatherStation.SSID);
  weatherStation.password = preferences.getString("Password", weatherStation.password);
  
  #ifdef DEBUG
    Serial.println("Known settings fetched from EEPROM:");
    Serial.print("lattitude: ");
    Serial.println(weatherStation.lattitude);
    Serial.print("longitude: ");
    Serial.println(weatherStation.longitude);
    Serial.print("Language: ");
    Serial.println(weatherStation.language);
    Serial.print("Units: ");
    Serial.println(weatherStation.units);
    Serial.print("Stepcount: ");
    Serial.println(weatherStation.stepCount);
    Serial.print("timeout: ");
    Serial.println(weatherStation.timeout);
    Serial.print("SSID: ");
    Serial.println(weatherStation.SSID);
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

  /* Wifi setup */
  if(preferences.getBool("wifi_possible") == true) {
    #ifdef DEBUG
      Serial.println("Initializing Wifi");
    #endif
    wifiInit();
      /* Confirm wifi connection established */
    delay(5000);
    if(WiFi.status() == WL_CONNECTED) {
      preferences.putBool("wifi_possible", true);
      /* Initial update to weather reports */
      #ifdef DEBUG
        Serial.println("Connected to the WiFi network");
      #endif 
      updateWeatherReports();
    }
    else {
      /* Wifi connection not established */
      preferences.putBool("wifi_possible", false);
      #ifdef DEBUG
        Serial.println("No WiFi connection established");
      #endif
      setStatusLED(CRGB::DarkOrange);
      esp_restart();
    }
  }
  else {
    #ifdef DEBUG
      Serial.println("No WiFi connection established");
    #endif
    setStatusLED(CRGB::DarkOrange);
  }

  /* Setup complete */
  #ifdef DEBUG
    Serial.println("Weatherstation initialization completed");
  #endif
}

void loop() 
{
  /* MAIN LOOP */
  if(WiFi.status() == WL_CONNECTED) setStatusLED(CRGB::Green);
  while(WiFi.status() == WL_CONNECTED)
  {
    if(weatherStation.measuredTemp <= (MIN_PELTIER_TEMP - 5.0) || weatherStation.measuredTemp >= MAX_PELTIER_TEMP + 8.0) {
      setStatusLED(CRGB::Red);
      flagUpcomingWeather = false;
      flagCurrentWeather = false;
      flagTrainingWeather = false;
      flagFault = true;
      break;
    }

    /* Handle updating current weather report */
    if(APITimeout(weatherStation.APITimeout)) updateWeatherReports();

    /* Handle updating temperature reading */
    if(tempTimeout(TEMP_TIMEOUT)) updateTemperature();

    /* Update relevant flags */
    if(digitalRead(buttonCurrentWeather) && !(digitalRead(buttonUpcomingWeather))) 
    {
      esp_timer_stop(timerWeatherFlags);
      esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
      flagCurrentWeather = true;
      flagUpcomingWeather = false;
      flagTrainingWeather = false;
      doOnceFlag = true;
      setStatusLED(CRGB::DarkOrange);
      /* Disable precipitation stepper */
      digitalWrite(enablePin, HIGH);
      vTaskSuspend(stepperTask);
      displayTimeoutCharacteristic.writeValue("0");
      digitalWrite(motorEnable, HIGH);
    }

   if(digitalRead(buttonUpcomingWeather) && !(digitalRead(buttonCurrentWeather)))
    {
      esp_timer_stop(timerWeatherFlags);
      esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
      flagUpcomingWeather = true;
      flagCurrentWeather = false;
      flagTrainingWeather = false;
      doOnceFlag = true;
      setStatusLED(CRGB::DarkOrange);
      
      /* Disable precipitation stepper */
      digitalWrite(enablePin, HIGH);
      vTaskSuspend(stepperTask);
      displayTimeoutCharacteristic.writeValue("0");
      digitalWrite(motorEnable, HIGH);
    }

    /* Handle BLE */
    BLE.poll();

    /* Execute state */
    if(flagTrainingWeather && !(digitalRead(buttonUpcomingWeather) || digitalRead(buttonCurrentWeather))) displayWeather(&weatherReportTraining);
    else if(flagUpcomingWeather && !(digitalRead(buttonUpcomingWeather) || digitalRead(buttonCurrentWeather))) displayWeather(&weatherReportUpcoming);
    else if(flagCurrentWeather && !(digitalRead(buttonUpcomingWeather) || digitalRead(buttonCurrentWeather))) displayWeather(&weatherReportCurrent);
    else idle();
  }

  while(flagFault) {
    Serial.println("Dangerous fault detected, blocking use of weatherstation!");
    delay(1000);
  }

  if(preferences.getBool("wifi_possible")) {
    preferences.putBool("wifi_possible", false);
    esp_restart();
  }

  /* Temperature handling */
  if(tempTimeout(TEMP_TIMEOUT)) updateTemperature();

  /* Handle BLE */
  BLE.poll();


  if(flagTrainingWeather) {
      /* Training mode without WiFi */
    flagCurrentWeather = false;
    flagUpcomingWeather = false;
    displayWeather(&weatherReportTraining);
  }
  else {
    /* Idle status if not displaying training mode */
    idle();
  }
}
/**
 * 
 * 
 * 
 * 
 * 
*/