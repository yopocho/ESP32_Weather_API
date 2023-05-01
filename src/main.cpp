#include <Arduino.h>
#include "main.hpp"

void setup() 
{

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

  /* Stepper task setup */
  xTaskCreatePinnedToCore(
    displayPrecipitation,   /* Task function. */
    "stepperTask",          /* name of task. */
    10000,                  /* Stack size of task */
    NULL,                   /* parameter of the task */
    1,                      /* priority of the task */
    &stepperTask,           /* Task handle to keep track of created task */
    0                       /* pin task to core 0 */   
  );
  vTaskSuspend(stepperTask);

  #ifdef DEBUG
    Serial.println("Initialized function displayPrecipitation()");
  #endif

  /* Precipitation Stepper */
  stepper.setMaxSpeed(400); 
  stepper.setAcceleration(30000); 
  stepper.moveTo(300); 
  
  /* Initial update to weather reports */
  updateWeatherReports();

  #ifdef DEBUG
    Serial.println("Weatherstation initialization completed");
  #endif

}

void loop() 
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
        break;
      case 49:
        weatherStation.targetTemp = 19.0f;
        break;
      case 50:
        weatherStation.targetTemp = 21.0f;
        break;
      case 51:
        weatherStation.targetTemp = 23.0f;
        break;
      case 52:
        weatherStation.targetTemp = 25.0f;
        break;
      case 53:
        weatherStation.targetTemp = 30.0f;
        break;
      case 54:
        weatherStation.targetTemp = 35.0f;
        break;
      case 55:
        weatherStation.targetTemp = 40.0f;
        break;
    };
  }

  /* Handle updating current weather report */
  if(APITimeout(weatherStation.APITimeout)) updateWeatherReports();

  /* Handle updating temperature reading */
  if(tempTimeout(TEMP_TIMEOUT)) updateTemperature();

  /* Update relevant flags */
  if(digitalRead(buttonCurrentWeather)) 
  {
    esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
    flagCurrentWeather = true;
  }

  if(digitalRead(buttonUpcomingWeather))
  {
    esp_timer_start_once(timerWeatherFlags, weatherStation.timeout * 1000);
    flagUpcomingWeather = true;
  }

  /* Execute state */
  if(flagCurrentWeather) displayWeather(&weatherReportCurrent);
  else if(flagUpcomingWeather) displayWeather(&weatherReportUpcoming);
  else idle();
}

/*
 * TODO:
 *  
 *  Add getPrecipitation (Rain OR snow OR hail)
 *    Add relevant haracteristic to weatherstation object
 *    Add map from mm(?) to stepper speed
 * 
 *  Add getWindSpeed
 *    Add relevant haracteristic to weatherstation object
 *    Add map from m/s to fan PWM dutycycle % (RPM) (Use ledc for easy PWM) 
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
 *  Add vibration motor logic
 * 
 * 
 */