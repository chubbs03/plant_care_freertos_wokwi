#include <Arduino_FreeRTOS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// - Potentiometer 1 = Soil moisture sensor
// - Potentiometer 2 = Temperature sensor
// - Potentiometer 3 = Humidity sensor

// Sensors
const int SOIL_MOISTURE_PIN = A0;
const int TEMP_PIN          = A1;
const int HUMIDITY_PIN      = A2;

// Actuators
const int WATER_PUMP_PIN = 2;
const int LIGHT_PIN      = 3;
const int FAN_PIN        = 4;

// Manual control buttons
const int PUMP_BUTTON_PIN  = 5;
const int LIGHT_BUTTON_PIN = 6;
const int FAN_BUTTON_PIN   = 7;

// Sensor trigger indicator LEDs
const int SOIL_LED_PIN     = 8;
const int TEMP_LED_PIN     = 9;
const int HUMIDITY_LED_PIN = 10;

// Mode button and mode LED
const int MODE_BUTTON_PIN = 11;
const int MODE_LED_PIN    = 13;   // ON = Manual mode, OFF = Auto mode

// LCD screen cycle button
const int DISPLAY_BUTTON_PIN = 12;

// THRESHOLDS 
const int DRY_SOIL_THRESHOLD = 400;  // Soil value below 400 means dry
const int HOT_TEMP_THRESHOLD = 30;   // Temperature above 30 C means hot
const int LOW_HUMIDITY_LIMIT = 40;   // Humidity below 40% means low humidity

// false = automatic mode
// true  = manual mode
volatile bool manualMode = false;

LiquidCrystal_I2C lcd(0x27, 16, 2);

int  g_soilMoisture  = 0;
int  g_temperatureC  = 0;
int  g_humidityPct   = 0;
bool g_soilIsDry     = false;
bool g_tempIsHot     = false;
bool g_humidityIsLow = false;

volatile uint8_t displayMode = 0;
const uint8_t DISPLAY_MODE_COUNT = 5;

// TASK PROTOTYPES 

void TaskPlantCare(void *pvParameters);
void updateLCD();

void setup() {
  Serial.begin(9600);

  // Actuator pins
  pinMode(WATER_PUMP_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // Indicator LED pins
  pinMode(SOIL_LED_PIN, OUTPUT);
  pinMode(TEMP_LED_PIN, OUTPUT);
  pinMode(HUMIDITY_LED_PIN, OUTPUT);
  pinMode(MODE_LED_PIN, OUTPUT);
  pinMode(PUMP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LIGHT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FAN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPLAY_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(WATER_PUMP_PIN, LOW);
  digitalWrite(LIGHT_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(SOIL_LED_PIN, LOW);
  digitalWrite(TEMP_LED_PIN, LOW);
  digitalWrite(HUMIDITY_LED_PIN, LOW);
  digitalWrite(MODE_LED_PIN, LOW);

  // I2C LCD init
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Plant Care Sys");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  xTaskCreate(
    TaskPlantCare,
    "PlantCare",
    240,
    NULL,
    1,
    NULL
  );
}

void loop() {
  
}

// LCD SCREEN UPDATE

void updateLCD() {
  lcd.clear();

  switch (displayMode) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Soil Moisture");
      lcd.setCursor(0, 1);
      lcd.print(g_soilMoisture);
      lcd.print(g_soilIsDry ? " DRY" : " OK");
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Temperature");
      lcd.setCursor(0, 1);
      lcd.print(g_temperatureC);
      lcd.print((char)223);
      lcd.print("C ");
      lcd.print(g_tempIsHot ? "HOT" : "OK");
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Humidity");
      lcd.setCursor(0, 1);
      lcd.print(g_humidityPct);
      lcd.print("% ");
      lcd.print(g_humidityIsLow ? "LOW" : "OK");
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Mode: ");
      lcd.print(manualMode ? "MANUAL" : "AUTO");
      lcd.setCursor(0, 1);
      lcd.print("Plant Care Sys");
      break;

    case 4:
    default:
      lcd.setCursor(0, 0);
      lcd.print("Pump:");
      lcd.print(digitalRead(WATER_PUMP_PIN) ? "ON " : "OFF");
      lcd.print(" Fan:");
      lcd.print(digitalRead(FAN_PIN) ? "ON " : "OFF");
      lcd.setCursor(0, 1);
      lcd.print("Light:");
      lcd.print(digitalRead(LIGHT_PIN) ? "ON" : "OFF");
      break;
  }
}

// COMBINED PLANT CARE TASK

void TaskPlantCare(void *pvParameters) {
  (void) pvParameters;

  bool    lastModeButtonState    = HIGH;
  bool    lastDisplayButtonState = HIGH;
  uint8_t lastDisplayMode        = 255;
  uint8_t lastActuatorState      = 0xFF;  
  bool    lcdNeedsRedraw         = true;
  uint8_t sensorTickCounter      = 10;  

  for (;;) {
    if (++sensorTickCounter >= 10) {
      sensorTickCounter = 0;

      int soilMoisture = analogRead(SOIL_MOISTURE_PIN);
      int rawTemp      = analogRead(TEMP_PIN);
      int rawHumidity  = analogRead(HUMIDITY_PIN);
      int temperatureC = map(rawTemp, 0, 1023, 0, 50);
      int humidityPct  = map(rawHumidity, 0, 1023, 0, 100);

      bool soilIsDry     = soilMoisture < DRY_SOIL_THRESHOLD;
      bool tempIsHot     = temperatureC > HOT_TEMP_THRESHOLD;
      bool humidityIsLow = humidityPct < LOW_HUMIDITY_LIMIT;

      g_soilMoisture  = soilMoisture;
      g_temperatureC  = temperatureC;
      g_humidityPct   = humidityPct;
      g_soilIsDry     = soilIsDry;
      g_tempIsHot     = tempIsHot;
      g_humidityIsLow = humidityIsLow;

      digitalWrite(SOIL_LED_PIN, soilIsDry ? HIGH : LOW);
      digitalWrite(TEMP_LED_PIN, tempIsHot ? HIGH : LOW);
      digitalWrite(HUMIDITY_LED_PIN, humidityIsLow ? HIGH : LOW);

      if (manualMode == false) {
        digitalWrite(WATER_PUMP_PIN, soilIsDry ? HIGH : LOW);
        digitalWrite(FAN_PIN, tempIsHot ? HIGH : LOW);
        digitalWrite(LIGHT_PIN, humidityIsLow ? HIGH : LOW);
      }

      // Serial monitor output
      Serial.println(F("========== PLANT CARE SYSTEM =========="));
      Serial.print(F("Mode: "));
      Serial.println(manualMode ? F("MANUAL") : F("AUTO"));

      Serial.print(F("Soil Moisture Raw: "));
      Serial.print(soilMoisture);
      Serial.println(soilIsDry ? F("  -> DRY, Soil LED ON") : F("  -> OK"));

      Serial.print(F("Temperature: "));
      Serial.print(temperatureC);
      Serial.println(tempIsHot ? F(" C  -> HOT, Temp LED ON") : F(" C  -> OK"));

      Serial.print(F("Humidity: "));
      Serial.print(humidityPct);
      Serial.println(humidityIsLow ? F(" %  -> LOW, Humidity LED ON") : F(" %  -> OK"));

      Serial.println(manualMode ? F("Auto actuator control: PAUSED")
                                : F("Auto actuator control: ACTIVE"));
      Serial.println();

      lcdNeedsRedraw = true;
    }
    bool currentModeButtonState = digitalRead(MODE_BUTTON_PIN);
    if (lastModeButtonState == HIGH && currentModeButtonState == LOW) {
      manualMode = !manualMode;

      digitalWrite(WATER_PUMP_PIN, LOW);
      digitalWrite(LIGHT_PIN, LOW);
      digitalWrite(FAN_PIN, LOW);

      Serial.print(F("Mode changed to: "));
      Serial.println(manualMode ? F("MANUAL") : F("AUTO"));

      lcdNeedsRedraw = true;
      vTaskDelay(250 / portTICK_PERIOD_MS); 
    }
    lastModeButtonState = currentModeButtonState;

    bool currentDisplayButtonState = digitalRead(DISPLAY_BUTTON_PIN);
    if (lastDisplayButtonState == HIGH && currentDisplayButtonState == LOW) {
      displayMode = (displayMode + 1) % DISPLAY_MODE_COUNT;

      Serial.print(F("Display mode: "));
      Serial.println(displayMode);

      vTaskDelay(250 / portTICK_PERIOD_MS);  
    }
    lastDisplayButtonState = currentDisplayButtonState;

    digitalWrite(MODE_LED_PIN, manualMode ? HIGH : LOW);

    // Manual actuator control 
    if (manualMode == true) {
      bool pumpButtonPressed  = digitalRead(PUMP_BUTTON_PIN) == LOW;
      bool lightButtonPressed = digitalRead(LIGHT_BUTTON_PIN) == LOW;
      bool fanButtonPressed   = digitalRead(FAN_BUTTON_PIN) == LOW;

      digitalWrite(WATER_PUMP_PIN, pumpButtonPressed ? HIGH : LOW);
      digitalWrite(LIGHT_PIN, lightButtonPressed ? HIGH : LOW);
      digitalWrite(FAN_PIN, fanButtonPressed ? HIGH : LOW);
    }
    if (displayMode != lastDisplayMode) {
      lastDisplayMode = displayMode;
      lcdNeedsRedraw = true;
    }
    uint8_t actuatorState = (digitalRead(WATER_PUMP_PIN) ? 0x01 : 0)
                          | (digitalRead(LIGHT_PIN)      ? 0x02 : 0)
                          | (digitalRead(FAN_PIN)        ? 0x04 : 0);
    if (actuatorState != lastActuatorState) {
      lastActuatorState = actuatorState;
      lcdNeedsRedraw = true;
    }
    if (lcdNeedsRedraw) {
      lcdNeedsRedraw = false;
      updateLCD();
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
