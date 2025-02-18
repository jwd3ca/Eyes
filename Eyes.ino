// Just test fullscreen sprite M5Core2

#include <M5Unified.h>
#include <Adafruit_GFX.h>  // Needed for sprites! Ensure this is included
#include <HardwareSerial.h>
#include <Adafruit_SHT31.h>
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Create an SHT31 object
Adafruit_SHT31 sht30 = Adafruit_SHT31();

// WIFI_SSID, WIFI_PASSWORD, influxdb credentials, etc. all defined in github-hidden config file

#define MyFont &FreeSansBold9pt7b

#define PMS_RX_PIN 13  // RX from PMS5003
#define PMS_TX_PIN 14  // TX from PMS5003

HardwareSerial pms5003(2);  // Serial2 for PMS5003
uint8_t buffer[32];         // Buffer to store incoming data

#define DATA_LEN 32

LGFX_Sprite EyesSprite = LGFX_Sprite(&M5.Display);

const unsigned long eventTime_circle = 100;
unsigned long previousTime = 0;

// Blink timing
unsigned long nextBlinkTime = 0;
bool isBlinking = false;
unsigned long blinkStartTime = 0;
const unsigned long blinkDuration = 250;  // Total blink duration
  //: 0-3, 0 is best, 3 is bad

int pm1_0 = 0;
int pm2_5 = 0;
int pm10 = 0;
int point3sum = 0;
int point5sum = 0;
int point10sum = 0;

float temperature = 0;
float humidity = 0;

int iterations = 0;

int pupilColor = 0x000000;

const unsigned long eventTime_1_post = 60000;  // interval in ms
unsigned long previousTime_1 = 0;


//===================================================================
void setup() {
  auto cfg = M5.config();

  M5.begin(cfg);
  Serial.begin(115200);  // Must do this here for M5Unified!
  delay(2000);

  Serial.println("M5Unified Initialized!");
  delay(500);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");

  // Try creating a 320x240 sprite
  EyesSprite.setPsram(true);          // force using psram
  EyesSprite.createSprite(320, 240);  // Create sprite buffer
  Serial.printf("Sprite Width: %d, Height: %d\n", EyesSprite.width(), EyesSprite.height());
  Serial.printf("Total PSRAM: %d, Free PSRAM: %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  // Initialize PMS5003 serial communication
  pms5003.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);
  Serial.println("PMS5003 initialized!");


  // Initialize I²C explicitly with the Core2 default pins:
  Wire.begin(21, 22);  // SDA=21, SCL=22

  // Initialize the SHT30 sensor at address 0x44
  if (!sht30.begin(0x44)) {
    Serial.println("Couldn't find SHT30 sensor at 0x44");
    while (1) {
      delay(1);
    }
  }
}

//====================================================================
void loop() {
  int comfortLevel;

  readPMSData();
  // Serial.printf("PM1_0 : %d µg/m³\n", pm1_0);

  get_SHT30data();
  // Serial.printf("temperature: %.2f, humidity: %.2f\n",temperature, humidity );

  // comfortLevel: 0-3, 0 is best, 3 is bad

  // pm1_0 = 40;

  if (pm1_0 > 151) {
    comfortLevel = 3;
    pupilColor = TFT_RED;
  } else if (pm1_0 > 81) {
    comfortLevel = 2;
    pupilColor = TFT_YELLOW;
  } else if (pm1_0 > 31) {
    comfortLevel = 1;
    pupilColor = TFT_DARKGREEN;
  } else {
    comfortLevel = 0;
    pupilColor = TFT_BLUE;
  }

  EyesSprite.fillSprite(TFT_BLACK);  // Clear screen before drawing
  EyesSprite.drawRect(0,0,320,240,pupilColor);
  updateDisplay(comfortLevel);
  EyesSprite.pushSprite(0, 0);  // Update display

  sendToInfluxDB();
  // delay(60000);  // Send data every 60 seconds
}

int eyeOffset = 0;

//---------------------------------------------------------------------
void updateDisplay(int comfortLevel) {
  unsigned long currentMillis = millis();


  if (currentMillis >= nextBlinkTime && !isBlinking) {
    isBlinking = true;
    blinkStartTime = currentMillis;
    nextBlinkTime = currentMillis + random(3000, 7000);
  }

  if (isBlinking && (currentMillis - blinkStartTime >= blinkDuration)) {
    isBlinking = false;

    // choose eyeOffset randomly
    int choice = random(1, 4); // upper bound is exclusive!
    switch (choice) {
      case 1:
        eyeOffset = 15;
        break;
      case 2:
        eyeOffset = -15;
        break;
      case 3:
        eyeOffset = 0;
        break;
    }
  }

  int eyelidHeight = isBlinking ? 120 : comfortLevel * 40;

  if (comfortLevel == 3) eyelidHeight = 85;  // Almost closed

  drawEyes(eyelidHeight);
}

//---------------------------------------------------------------------
void drawEyes(int eyelidHeight) {
  const int leftEyeX = 94;
  const int rightEyeX = 222;
  const int eyeY = 118;
  const int eyeRadius = 63; // 70 more puts eyes closer together
  const int pupilSize = 35;
  const int eyeVerticalRadius = eyeRadius * 1.4;  // Make eyes longer vertically
  char buff[10];

  EyesSprite.fillEllipse(leftEyeX, eyeY, eyeRadius, eyeVerticalRadius, 0x61c4db);
  EyesSprite.fillEllipse(rightEyeX, eyeY, eyeRadius, eyeVerticalRadius, 0x61c4db);

  // pupils

  // Draw left pupil slightly to the right of the center for cartoonish effect
  EyesSprite.fillCircle(leftEyeX  + eyeOffset, eyeY, pupilSize, pupilColor);
  // Draw right pupil slightly to the left of the center for cartoonish effect
  EyesSprite.fillCircle(rightEyeX + eyeOffset, eyeY, pupilSize, pupilColor);

  // Top of the eyelid is based on the vertical radius
  int eyeTop = eyeY - eyeVerticalRadius;
  EyesSprite.fillRect(0, eyeTop - 30, 340, eyelidHeight, TFT_BLACK);


   // Set text properties for the string
  EyesSprite.setTextColor(TFT_BLACK);
  EyesSprite.setFreeFont(MyFont);  // Set custom font
  EyesSprite.setTextDatum(MC_DATUM);

  // Draw the text at the center of the pupils
  snprintf(buff, sizeof(buff), "%.1f C", temperature);
  EyesSprite.drawString(buff, leftEyeX + eyeOffset, eyeY);

  snprintf(buff, sizeof(buff), "%d %%", int(humidity + 0.5f));
  EyesSprite.drawString(buff, rightEyeX + eyeOffset, eyeY);

}

//===================================================================
static bool get_SHT30data() {
  // Read temperature in °C and relative humidity in %
  temperature = sht30.readTemperature();
  // the proximity to the base seems to result in high temp reading. Reduce by some % here...
  temperature *= 0.820;
  humidity = sht30.readHumidity();

  // Check if readings are valid (not NaN)
  if (!isnan(temperature) && !isnan(humidity)) {
    return (true);
  } else {
    Serial.println("Failed to read from SHT30 sensor!");
    return (false);
  }
}

//===================================================================
// Function to read PMS5003 data
bool readPMSData() {
  uint8_t buffer[DATA_LEN];
  int16_t values[16];

  if (pms5003.available() >= DATA_LEN) {
    int bytesRead = pms5003.readBytes(buffer, DATA_LEN);

    if (bytesRead == DATA_LEN && buffer[0] == 0x42 && buffer[1] == 0x4D) {
      // Process data (16-bit values in Big Endian format)
      for (int i = 0, j = 0; i < DATA_LEN; i += 2, j++) {
        values[j] = (buffer[i] << 8) | buffer[i + 1];
      }

      // Assign values to variables
      pm1_0 = values[5];
      pm2_5 = values[6];
      pm10 = values[7];
      point3sum = values[8];
      point5sum = values[9];
      point10sum = values[10];


      return true;
    }
  }
  return false;
}

//----------------------------------------------------------------
void sendToInfluxDB() {
  unsigned long currentTime = millis();

  if (currentTime - previousTime_1 >= eventTime_1_post) {
    previousTime_1 = currentTime;

    // Format the data in InfluxDB line protocol
    String data_low = String("data_point,group=low pm1_0=") + pm1_0 + ",pm2_5=" + pm2_5 + ",pm10=" + pm10;
    String data_high = String("data_point,group=high point3sum=") + point3sum + ",point5sum=" + point5sum + ",point10sum=" + point10sum;
    String SHT30string = String("data_point,group=SHT30 temperature=") + temperature + ",humidity=" + humidity;
    Serial.println();
    Serial.println(data_low);
    Serial.println(data_high);
    Serial.println(SHT30string);

    // Send data to InfluxDB
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      String url = String(INFLUXDB_URL) + "?org=" + INFLUXDB_ORG + "&bucket=" + INFLUXDB_BUCKET + "&precision=s";
      http.begin(url);
      http.addHeader("Authorization", String("Token ") + INFLUXDB_TOKEN);
      http.addHeader("Content-Type", "text/plain");

      int httpResponseCode1 = http.POST(data_low);
      if (httpResponseCode1 > 0) {
        Serial.printf("Data_low sent successfully! HTTP response code: %d\n", httpResponseCode1);
      } else {
        Serial.printf("Error sending data. HTTP response code: %d\n", httpResponseCode1);
      }

      int httpResponseCode2 = http.POST(data_high);
      if (httpResponseCode2 > 0) {
        Serial.printf("Data_high sent successfully! HTTP response code: %d\n", httpResponseCode2);
      } else {
        Serial.printf("Error sending data. HTTP response code: %d\n", httpResponseCode2);
      }

      int httpResponseCode3 = http.POST(SHT30string);
      if (httpResponseCode3 > 0) {
        Serial.printf("SHT30string sent successfully! HTTP response code: %d\n", httpResponseCode3);
      } else {
        Serial.printf("Error sending data. HTTP response code: %d\n", httpResponseCode3);
      }

      http.end();

    }  // WL_CONNECTED
    else {
      Serial.println("WiFi not connected, cannot send data.");
    }
  }
}
