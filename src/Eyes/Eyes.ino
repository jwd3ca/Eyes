// Arduino/Gits/Eyes/Eyes.ino

#include <M5Unified.h>
#include <Adafruit_GFX.h>  // Needed for sprites! Ensure this is included
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT31.h>
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_SSLClient.h>
#include <ArduinoJson.h>  // Include ArduinoJson library

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
int eyeY = 118;

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
int eyeOffset = 0;

int currentValue = 10;             // Initial value
int previousValue = currentValue;  // Store the initial value
int crashCounter;
int crashLimit = 25000;

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

  currentValue = pm1_0;
  // Serial.printf("currentValue: %d, previousValue: %d \n", currentValue, previousValue);

  if (currentValue != 0 && currentValue == previousValue) {
    ++crashCounter;  // crashed?
  }
  previousValue = currentValue;

  if (crashCounter >= crashLimit) {
    Serial.printf("crashCounter is %d\n", crashCounter);
    send_to_pushover();
    crashCounter = 0;
  }

  get_SHT30data();
  // Serial.printf("temperature: %.2f, humidity: %.2f\n",temperature, humidity );

  // comfortLevel: 0-3, 0 is best, 3 is bad

  // pm1_0 = 40; // for testing colours

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
  EyesSprite.drawRect(0, 0, 320, 240, pupilColor);
  updateDisplay(comfortLevel);
  EyesSprite.pushSprite(0, 0);  // Update display

  sendToInfluxDB();
  // Sends data every 60 seconds using millis
}

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
    int choice = random(1, 8);  // upper bound is exclusive!
    // Serial.printf("choice is %d\n", choice);
    switch (choice) {
      case 1:  // look right
        eyeOffset = 15;
        eyeY = 118;
        break;
      case 2:  // look left
        eyeOffset = -15;
        eyeY = 118;
        break;
      case 3:  // look center
        eyeOffset = 0;
        eyeY = 118;
        break;
      case 4:  // look up & right
        eyeOffset = 15;
        eyeY = 90;
        break;
      case 5:  // look down & right
        eyeOffset = 15;
        eyeY = 140;
        break;
      case 6:  // look up & left
        eyeOffset = -15;
        eyeY = 90;
        break;
      case 7:  // look down & left
        eyeOffset = -15;
        eyeY = 140;
        break;
    }
  }

  int eyelidHeight = isBlinking ? 120 : comfortLevel * 40;

  if (comfortLevel == 3) eyelidHeight = 85;  // Almost closed

  drawEyes(eyelidHeight, eyeY);
}


//---------------------------------------------------------------------
void drawEyes(int eyelidHeight, int eyeY) {
  const int leftEyeX = 94;
  const int rightEyeX = 222;

  const int eyeRadius = 65;  // 70 more puts eyes closer together
  const int pupilSize = 35;
  const int eyeVerticalRadius = eyeRadius * 1.4;  // Make eyes longer vertically
  char buff[10];

  EyesSprite.fillEllipse(leftEyeX, 118, eyeRadius, eyeVerticalRadius, 0x61c4db);
  EyesSprite.fillEllipse(rightEyeX, 118, eyeRadius, eyeVerticalRadius, 0x61c4db);

  // pupils:
  // display left pupil slightly to the right of the center for cartoonish effect
  EyesSprite.fillCircle(leftEyeX + eyeOffset, eyeY, pupilSize, pupilColor);
  // display right pupil slightly to the left of the center for cartoonish effect
  EyesSprite.fillCircle(rightEyeX + eyeOffset, eyeY, pupilSize, pupilColor);

  // Top of the eyelid is based on the vertical radius
  int eyeTop = 118 - eyeVerticalRadius;
  // now blink
  // EyesSprite.fillRect(10, eyeTop - 30, 290, eyelidHeight, TFT_BLACK);
  EyesSprite.fillRect(10, eyeTop, 290, eyelidHeight, TFT_BLACK);

  // Set text properties for the string
  EyesSprite.setTextColor(TFT_BLACK);
  EyesSprite.setFreeFont(MyFont);  // Set custom font
  EyesSprite.setTextDatum(MC_DATUM);

  // display the text at the center of the pupils
  snprintf(buff, sizeof(buff), "%.1f C", temperature);
  EyesSprite.drawString(buff, leftEyeX + eyeOffset, eyeY);

  snprintf(buff, sizeof(buff), "%d %%", int(humidity + 0.5f));
  EyesSprite.drawString(buff, rightEyeX + eyeOffset, eyeY);

  // display the pm1_0 count at the top of the sprite
  snprintf(buff, sizeof(buff), "%d ", pm1_0);
  EyesSprite.setTextColor(pupilColor);
  EyesSprite.drawString(buff, 155, 22);
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

//----------------------------------------------------------------
void send_to_pushover() {
  char String_buffer[128];
  ESP_SSLClient ssl_client;
  WiFiClient basic_client;

  ssl_client.setInsecure();
  ssl_client.setBufferSizes(1024, 512);
  ssl_client.setDebugLevel(0);
  ssl_client.setSessionTimeout(120);
  ssl_client.setClient(&basic_client);

  Serial.println("---------------------------------");
  Serial.print("Connecting to Pushover...");

  snprintf(String_buffer, sizeof(String_buffer), "crashLimit (%d) exceeded\n", crashLimit);

  // Construct JSON payload using ArduinoJson
  StaticJsonDocument<512> doc;
  doc["token"] = PUSHOVER_API_TOKEN;
  doc["user"] = PUSHOVER_USER_KEY;
  // doc["message"] = "Test message from ESP32!";
  doc["message"] = String_buffer;
  doc["title"] = "Air Quaility Crash Alert";

  String payload;
  serializeJson(doc, payload);

  if (ssl_client.connect("api.pushover.net", 443)) {
    Serial.println(" ok");
    Serial.println("Send POST request to Pushover...");
    ssl_client.print("POST /1/messages.json HTTP/1.1\r\n");
    ssl_client.print("Host: api.pushover.net\r\n");
    ssl_client.print("Content-Type: application/json\r\n");
    ssl_client.print("Content-Length: ");
    ssl_client.print(payload.length());
    ssl_client.print("\r\n\r\n");
    ssl_client.print(payload);

    Serial.print("Read response...");

    unsigned long ms = millis();
    while (!ssl_client.available() && millis() - ms < 3000) {
      delay(0);
    }
    Serial.println();

    while (ssl_client.available()) {
      Serial.print((char)ssl_client.read());
    }
    Serial.println();
  } else
    Serial.println(" failed\n");

  ssl_client.stop();

  Serial.println();
}
