#include <Arduino_FreeRTOS.h>
#include <semphr.h>
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

// Written by TaskSensor, read by TaskAutoControl (and updateLCD()).
volatile int  g_soilMoisture  = 0;
volatile int  g_temperatureC  = 0;
volatile int  g_humidityPct   = 0;
volatile bool g_soilIsDry     = false;
volatile bool g_tempIsHot     = false;
volatile bool g_humidityIsLow = false;

volatile uint8_t displayMode = 0;
const uint8_t DISPLAY_MODE_COUNT = 5;

// BINARY SEMAPHORE
SemaphoreHandle_t xModeSemaphore = NULL;

// TASK PROTOTYPES

void TaskManualControl(void *pvParameters);
void TaskSensor(void *pvParameters);
void TaskAutoControl(void *pvParameters);
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

  xModeSemaphore = xSemaphoreCreateBinary();

  // Priority 3 (highest)
  xTaskCreate(
    TaskManualControl,
    "Manual",
    88,
    NULL,
    3,
    NULL
  );

  // Priority 2: read sensors once a second.
  xTaskCreate(
    TaskSensor,
    "Sensor",
    64,
    NULL,
    2,
    NULL
  );

  // Priority 1: drive actuators in AUTO, refresh LCD and Serial.
  xTaskCreate(
    TaskAutoControl,
    "Auto",
    120,
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

// TASK 1: MANUAL CONTROL (priority 3, highest)

void TaskManualControl(void *pvParameters) {
  (void) pvParameters;

  bool lastModeButtonState    = HIGH;
  bool lastDisplayButtonState = HIGH;

  for (;;) {
    // MODE button toggles AUTO / MANUAL
    bool currentModeButtonState = digitalRead(MODE_BUTTON_PIN);
    if (lastModeButtonState == HIGH && currentModeButtonState == LOW) {
      manualMode = !manualMode;

      // Safety: turn OFF all actuators on mode change
      digitalWrite(WATER_PUMP_PIN, LOW);
      digitalWrite(LIGHT_PIN, LOW);
      digitalWrite(FAN_PIN, LOW);

      // Signal TaskAutoControl that a mode change happened. That task
      // is responsible for printing the "Mode changed to..." message
      // and refreshing the LCD.
      xSemaphoreGive(xModeSemaphore);

      vTaskDelay(250 / portTICK_PERIOD_MS);  // debounce
    }
    lastModeButtonState = currentModeButtonState;

    // DISPLAY button cycles the LCD screen
    bool currentDisplayButtonState = digitalRead(DISPLAY_BUTTON_PIN);
    if (lastDisplayButtonState == HIGH && currentDisplayButtonState == LOW) {
      displayMode = (displayMode + 1) % DISPLAY_MODE_COUNT;

      Serial.print(F("Display mode: "));
      Serial.println(displayMode);

      vTaskDelay(250 / portTICK_PERIOD_MS);  // debounce
    }
    lastDisplayButtonState = currentDisplayButtonState;

    // Mode LED ON = manual mode
    digitalWrite(MODE_LED_PIN, manualMode ? HIGH : LOW);

    // Manual actuator buttons only work in manual mode
    if (manualMode == true) {
      digitalWrite(WATER_PUMP_PIN, digitalRead(PUMP_BUTTON_PIN)  == LOW ? HIGH : LOW);
      digitalWrite(LIGHT_PIN,      digitalRead(LIGHT_BUTTON_PIN) == LOW ? HIGH : LOW);
      digitalWrite(FAN_PIN,        digitalRead(FAN_BUTTON_PIN)   == LOW ? HIGH : LOW);
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);  // 20 Hz button polling
  }
}

// TASK 2: SENSOR READING (priority 2)

void TaskSensor(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
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

    // Sensor indicator LEDs (active in both AUTO and MANUAL)
    digitalWrite(SOIL_LED_PIN,     soilIsDry     ? HIGH : LOW);
    digitalWrite(TEMP_LED_PIN,     tempIsHot     ? HIGH : LOW);
    digitalWrite(HUMIDITY_LED_PIN, humidityIsLow ? HIGH : LOW);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// TASK 3: AUTO CONTROL + LCD + SERIAL (priority 1, lowest)

void TaskAutoControl(void *pvParameters) {
  (void) pvParameters;

  uint8_t lastDisplayMode   = 255;
  uint8_t lastActuatorState = 0xFF;
  int     lastSoilMoisture  = -1;
  int     lastTemperatureC  = -1;
  int     lastHumidityPct   = -1;
  uint8_t serialTickCounter = 5;  // force a first print

  for (;;) {
    bool needsRedraw = false;

    // Mode-change signal from TaskManualControl. Non-blocking poll
    if (xSemaphoreTake(xModeSemaphore, 0) == pdTRUE) {
      Serial.print(F("Mode changed to: "));
      Serial.println(manualMode ? F("MANUAL") : F("AUTO"));
      needsRedraw = true;
    }

    // Automatic actuator control
    if (manualMode == false) {
      digitalWrite(WATER_PUMP_PIN, g_soilIsDry     ? HIGH : LOW);
      digitalWrite(FAN_PIN,        g_tempIsHot     ? HIGH : LOW);
      digitalWrite(LIGHT_PIN,      g_humidityIsLow ? HIGH : LOW);
    }

    if (displayMode != lastDisplayMode) {
      lastDisplayMode = displayMode;
      needsRedraw = true;
    }
    uint8_t actuatorState = (digitalRead(WATER_PUMP_PIN) ? 0x01 : 0)
                          | (digitalRead(LIGHT_PIN)      ? 0x02 : 0)
                          | (digitalRead(FAN_PIN)        ? 0x04 : 0);
    if (actuatorState != lastActuatorState) {
      lastActuatorState = actuatorState;
      needsRedraw = true;
    }
    if (g_soilMoisture != lastSoilMoisture ||
        g_temperatureC != lastTemperatureC ||
        g_humidityPct  != lastHumidityPct) {
      lastSoilMoisture = g_soilMoisture;
      lastTemperatureC = g_temperatureC;
      lastHumidityPct  = g_humidityPct;
      needsRedraw = true;
    }
    if (needsRedraw) {
      updateLCD();
    }

    // Serial monitor: print full status every ~1 s (5 * 200 ms)
    if (++serialTickCounter >= 5) {
      serialTickCounter = 0;

      Serial.println(F("========== PLANT CARE SYSTEM =========="));
      Serial.print(F("Mode: "));
      Serial.println(manualMode ? F("MANUAL") : F("AUTO"));

      Serial.print(F("Soil Moisture Raw: "));
      Serial.print(g_soilMoisture);
      Serial.println(g_soilIsDry ? F("  -> DRY, Soil LED ON") : F("  -> OK"));

      Serial.print(F("Temperature: "));
      Serial.print(g_temperatureC);
      Serial.println(g_tempIsHot ? F(" C  -> HOT, Temp LED ON") : F(" C  -> OK"));

      Serial.print(F("Humidity: "));
      Serial.print(g_humidityPct);
      Serial.println(g_humidityIsLow ? F(" %  -> LOW, Humidity LED ON") : F(" %  -> OK"));

      Serial.println(manualMode ? F("Auto actuator control: PAUSED")
                                : F("Auto actuator control: ACTIVE"));
      Serial.println();
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}
