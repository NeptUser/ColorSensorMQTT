#include <arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
//#include <RTOS.h>

// Pins definitions for Color Sensor 0.1.0
#define PINNP 2
#define NUMPIXELS 1
#define PINLDR 32
//#define PINBUTTON 12

// Pins definitions for ky-40 encoder
#define CLK 18
#define DT 17
#define SW 16

// definitions for Oled 0.96 display

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3D // Common I2C address for SSD1306 displays

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Variables
unsigned long time1 = millis();

// Variables to store the average LDR values
int avg_Red;
int avg_Green;
int avg_Blue;
int avg_White;
int avg_Bypass;

// Variables for encoder
int counter = 0;
int currentStateCLK;
int lastStateCLK;
String currentDir = "";
unsigned long lastButtonPress = 0;

// Objects
Adafruit_NeoPixel led = Adafruit_NeoPixel(NUMPIXELS, PINNP, NEO_GRB + NEO_KHZ800);

// Colors to be used
enum color {
  RED, 
  GREEN, 
  BLUE, 
  YELLOW, 
  BROWN, 
  WHITE1, 
  BLACK1,
  COLOR_COUNT // to keep track of the number of colors
};

color selectedColor = RED;
const char* colorNames[] = {"RED", "GREEN", "BLUE", "YELLOW", "BROWN", "WHITE", "BLACK"};

// WiFi and MQTT credentials
String ssid;
String password;
String mqttServer;
String mqttUser;
String mqttPassword;
String mqttTopic;
int mqttPort;

WiFiClient espClient;
PubSubClient client(espClient);

void updateDisplay(); // Forward declaration of updateDisplay

// Function to load credentials from JSON file
void loadCredentials() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }

  File file = SPIFFS.open("/credentials.json", "r");
  if (!file) {
    Serial.println("Failed to open credentials file");
    return;
  }

  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  file.close();

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.println("Failed to parse credentials file");
    return;
  }

  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();
  mqttServer = doc["mqttServer"].as<String>();
  mqttUser = doc["mqttUser"].as<String>();
  mqttPassword = doc["mqttPassword"].as<String>();
  mqttTopic = doc["mqttTopic"].as<String>();
  mqttPort = doc["mqttPort"];
}

// Function to connect to WiFi
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Connecting to WiFi...");
  display.display();

  WiFi.begin(ssid.c_str(), password.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("WiFi connected");
  display.display();
  delay(1000);
}

// Function to connect to MQTT server
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Connecting to MQTT...");
    display.display();

    if (client.connect("ESP32Client", mqttUser.c_str(), mqttPassword.c_str())) {
      Serial.println("connected");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("MQTT connected");
      display.display();
      delay(1000);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Function to calculate the average of an array
int calculateAverage(int arr[]) {
  int sum = 0;
  int size = 10;
  for (int i = 0; i < size; i++) {
    //Serial.print(arr[i]);
    sum += arr[i];
  }
  return sum / size;
}

// Function to calculate the mode of an array
int calculateMode(int arr[], int size) {
  int maxValue = 0, maxCount = 0;

  for (int i = 0; i < size; ++i) {
    int count = 0;
    
    for (int j = 0; j < size; ++j) {
      if (arr[j] == arr[i])
        ++count;
    }

    if (count > maxCount) {
      maxCount = count;
      maxValue = arr[i];
    }
  }

  return maxValue;
}

// Function to normalize LDR readings
float normalizeLDR(int rawValue, int minValue, int maxValue) {
  return (float)(rawValue - minValue) / (maxValue - minValue);
}

// Function to send data to MQTT
void sendToMQTT() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  StaticJsonDocument<200> doc;
  doc["color"] = colorNames[selectedColor];
  doc["red"] = avg_Red;
  doc["green"] = avg_Green;
  doc["blue"] = avg_Blue;
  doc["white"] = avg_White;
  doc["bypass"] = avg_Bypass;

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(mqttTopic.c_str(), jsonBuffer);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Dados enviados");
  display.setCursor(0, 10);
  display.print("Obrigado!");
  display.display();
  delay(2000);
  updateDisplay();
}

// Function to read the LDR values and calculate the mode
void getColor(){
  if (millis() - time1 > 1000){
    int values_Red[10], values_Green[10], values_Blue[10], values_White[10], values_Bypass[10];

    // read channel R
    led.setPixelColor(0, led.Color(255, 0, 0));
    led.show();
    for (int i = 0; i < 10; i++){
      values_Red[i] = analogRead(PINLDR);
      delay(10);
    }
    avg_Red = calculateMode(values_Red, 10);
    avg_Red = normalizeLDR(avg_Red, 0, 4095); // Normalizing the value
    delay(10);

    // read channel G
    led.setPixelColor(0, led.Color(0, 255, 0));
    led.show();
    for (int i = 0; i < 10; i++){
      values_Green[i] = analogRead(PINLDR);
      delay(10);
    }
    avg_Green = calculateMode(values_Green, 10);
    avg_Green = normalizeLDR(avg_Green, 0, 4095); // Normalizing the value
    delay(10);

    // read channel B
    led.setPixelColor(0, led.Color(0, 0, 255));
    led.show();
    for (int i = 0; i < 10; i++){
      values_Blue[i] = analogRead(PINLDR);
      delay(10);
    }
    avg_Blue = calculateMode(values_Blue, 10);
    avg_Blue = normalizeLDR(avg_Blue, 0, 4095); // Normalizing the value
    delay(10);

    // read channel W
    led.setPixelColor(0, led.Color(255, 255, 255));
    led.show();
    for (int i = 0; i < 10; i++){
      values_White[i] = analogRead(PINLDR);
      delay(10);
    }
    avg_White = calculateMode(values_White, 10);
    avg_White = normalizeLDR(avg_White, 0, 4095); // Normalizing the value
    delay(10);

    // read channel Bypass
    led.setPixelColor(0, led.Color(0, 0, 0));
    led.show();
    for (int i = 0; i < 10; i++){
      values_Bypass[i] = analogRead(PINLDR);
      delay(10);
    }
    avg_Bypass = calculateMode(values_Bypass, 10);
    avg_Bypass = normalizeLDR(avg_Bypass, 0, 4095); // Normalizing the value
  }
}

// Function to print the values of the array
void printValues(){
  Serial.print("Red: ");
  Serial.print(avg_Red);
  Serial.println(" ");

  Serial.print("Green: ");
  Serial.print(avg_Green);
  Serial.println(" ");

  Serial.print("Blue: ");
  Serial.print(avg_Blue);
  Serial.println(" ");

  Serial.print("White: ");
  Serial.print(avg_White);
  Serial.println(" ");

  Serial.print("Bypass: ");
  Serial.print(avg_Bypass);
  Serial.println(" ");
}

void setup_encoder() {
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);
  lastStateCLK = digitalRead(CLK);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Selecione a cor:");
  display.setCursor(0, 10);
  display.print(colorNames[selectedColor]);
  display.display();
}

void countdownDisplay(int seconds) {
  for (int i = seconds; i > 0; i--) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Lendo em:");
    display.setCursor(0, 20);
    display.print(i);
    display.display();
    delay(1000);
  }
  display.clearDisplay();
  display.display();
}

void handleEncoder() {
  currentStateCLK = digitalRead(CLK);
  if (currentStateCLK != lastStateCLK && currentStateCLK == LOW) {
    if (digitalRead(DT) != currentStateCLK) {
      selectedColor = (color)((selectedColor + 1) % COLOR_COUNT);
    } else {
      selectedColor = (color)((selectedColor - 1 + COLOR_COUNT) % COLOR_COUNT);
    }
    updateDisplay();
  }
  lastStateCLK = currentStateCLK;

  if (digitalRead(SW) == LOW) {
    if (millis() - lastButtonPress > 50) {
      countdownDisplay(3); // Adiciona contagem regressiva de 3 segundos
      getColor();
      sendToMQTT();
      lastButtonPress = millis();
    }
  }
}

void setup(){
  Serial.begin(115200);
  led.begin();
  led.show();
  pinMode(PINLDR, INPUT);

  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  } else {
    Serial.println(F("SSD1306 allocation success"));
  }
  display.display();
  delay(2000);
  display.clearDisplay();

  loadCredentials(); // Load credentials from JSON file

  setup_wifi();
  client.setServer(mqttServer.c_str(), mqttPort);
  reconnect();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Ready");
  display.display();
  delay(1000);
  updateDisplay();

  // Initialize encoder
  setup_encoder();
}

void loop(){
  handleEncoder();
  delay(100);
}