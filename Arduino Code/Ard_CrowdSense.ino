#include <Wire.h>
#include "MAX30105.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_NeoPixel.h>

// Suppress I2C_BUFFER_LENGTH warning
#ifdef I2C_BUFFER_LENGTH
#undef I2C_BUFFER_LENGTH
#endif
#define I2C_BUFFER_LENGTH 32

// ===== RGB LED Ring Setup =====
#define LED_PIN D4          // Connect NI wire to D4 (GPIO2)
#define LED_COUNT 12        // Number of LEDs on your ring (adjust if different)
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===== OLED Display Setup =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== WiFi Configuration =====
const char* ssid = "Xiaomi 12 Pro";
const char* password = "tonyholmes";
const char* serverUrl = "http://10.215.21.17:3000/api/heartrate";

// ===== Sensor Object =====
MAX30105 sensor;

// ===== BPM Variables =====
unsigned long lastBeatTime = 0;
int bpm = 0;
int avgBPM = 0;
int bpmBuffer[15] = {0};
int bpmIndex = 0;

// ===== SpO2 Variables =====
int spo2 = 0;
int avgSpO2 = 0;
int spo2Buffer[15] = {0};
int spo2Index = 0;

// ===== Signal Processing =====
unsigned long lastDebugTime = 0;
unsigned long fingerPresentTime = 0;
bool fingerPresent = false;
unsigned long lastPeakTime = 0;
int validPeakCount = 0;
unsigned long lastTransmitTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastLEDUpdate = 0;

int valueHistory[50] = {0};
int irHistory[50] = {0};
int redHistory[50] = {0};
int historyIndex = 0;
int baselineValue = 0;
int stableReadings = 0;
bool measuring = false;

// Auto-calibration variables
int minIR = 999999;
int maxIR = 0;
int fingerThreshold = 0;

// ===== User Info =====
String userId = "CROWD_USER_001";
String locationId = "ZONE_A";

// ===== Display States =====
enum DisplayState {
  STATE_WELCOME,
  STATE_WIFI_CONNECTING,
  STATE_WIFI_CONNECTED,
  STATE_NO_FINGER,
  STATE_MEASURING,
  STATE_ANOMALY
};

DisplayState currentState = STATE_WELCOME;
unsigned long anomalyStartTime = 0;

// ===== RGB LED Functions =====

// Set all LEDs to a specific color
void setRingColor(uint32_t color) {
  for(int i = 0; i < LED_COUNT; i++) {
    ring.setPixelColor(i, color);
  }
  ring.show();
}

// Get color based on health status
uint32_t getHealthColor(int heartRate, int spo2Value) {
  if(heartRate > 120 || (spo2Value < 85 && spo2Value > 0)) {
    // Critical - Red
    return ring.Color(255, 0, 0);
  }
  else if(heartRate > 100 || (spo2Value < 90 && spo2Value > 0)) {
    // Warning - Orange
    return ring.Color(255, 165, 0);
  }
  else if(heartRate < 50 && heartRate > 0) {
    // Low heart rate - Yellow
    return ring.Color(255, 255, 0);
  }
  else if(heartRate > 0 && spo2Value > 0) {
    // Normal - Green
    return ring.Color(0, 255, 0);
  }
  else {
    // No data / Waiting - Blue
    return ring.Color(0, 0, 255);
  }
}

// Flash LED on heartbeat detection
void heartbeatFlash() {
  setRingColor(ring.Color(255, 255, 255));  // White flash
  delay(30);
  setRingColor(getHealthColor(avgBPM, avgSpO2));
}

// Alert animation (flashing red)
void alertFlash() {
  static bool flashState = false;
  if(millis() - lastLEDUpdate > 300) {
    if(flashState) {
      setRingColor(ring.Color(255, 0, 0));  // Red
    } else {
      setRingColor(ring.Color(0, 0, 0));    // Off
    }
    flashState = !flashState;
    lastLEDUpdate = millis();
  }
}

// Breathing effect for normal operation
void breathingEffect(uint32_t color) {
  static int brightness = 0;
  static int direction = 5;
  
  brightness += direction;
  if(brightness >= 100 || brightness <= 0) {
    direction = -direction;
  }
  
  int r = (color >> 16) & 0xFF;
  int g = (color >> 8) & 0xFF;
  int b = color & 0xFF;
  
  r = r * brightness / 100;
  g = g * brightness / 100;
  b = b * brightness / 100;
  
  for(int i = 0; i < LED_COUNT; i++) {
    ring.setPixelColor(i, ring.Color(r, g, b));
  }
  ring.show();
}

// Progress animation while measuring
void measuringAnimation(int progress) {
  int activeLEDs = map(progress, 0, 100, 0, LED_COUNT);
  
  for(int i = 0; i < LED_COUNT; i++) {
    if(i < activeLEDs) {
      ring.setPixelColor(i, ring.Color(0, 100, 255));  // Blue
    } else {
      ring.setPixelColor(i, ring.Color(0, 0, 0));       // Off
    }
  }
  ring.show();
}

// Startup rainbow animation
void rainbowAnimation() {
  for(int j = 0; j < 256; j++) {
    for(int i = 0; i < LED_COUNT; i++) {
      ring.setPixelColor(i, ring.Color(
        (i * 256 / LED_COUNT + j) & 255,
        (i * 128 / LED_COUNT + j * 2) & 255,
        (i * 64 / LED_COUNT + j * 4) & 255
      ));
    }
    ring.show();
    delay(5);
  }
}

int calculateSpO2(int irValue, int redValue) {
  if(irValue <= 0 || redValue <= 0) return 0;
  
  float ratio = (float)redValue / (float)irValue;
  if(ratio < 0.5) ratio = 0.5;
  if(ratio > 2.0) ratio = 2.0;
  
  int spo2Value = (int)(110 - (25 * ratio));
  if(spo2Value > 100) spo2Value = 100;
  if(spo2Value < 70) spo2Value = 70;
  
  return spo2Value;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=================================");
  Serial.println("CrowdSense HR & SpO2 Monitor");
  Serial.println("=================================\n");
  
  // ===== Initialize RGB LED Ring =====
  ring.begin();
  ring.setBrightness(100);
  ring.show();
  
  // Startup animation
  Serial.println("🎨 RGB LED Ring initializing...");
  rainbowAnimation();
  setRingColor(ring.Color(0, 0, 255));  // Blue
  delay(500);
  
  // ===== Initialize OLED Display =====
  Wire.begin(4, 5);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ OLED not found!");
  } else {
    Serial.println("✅ OLED initialized");
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CrowdSense v3.0");
    display.println("HR + SpO2 + LED");
    display.display();
  }
  
  // ===== Connect to WiFi =====
  currentState = STATE_WIFI_CONNECTING;
  updateDisplay();
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    updateDisplay();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    currentState = STATE_WIFI_CONNECTED;
    
    // Success animation - quick green flash
    setRingColor(ring.Color(0, 255, 0));
    delay(200);
    setRingColor(ring.Color(0, 0, 255));
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
  updateDisplay();
  
  // ===== Initialize MAX30102 Sensor =====
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ MAX30102 not detected!");
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("Sensor Error!");
    display.display();
    while(1);
  }
  
  sensor.setup();
  sensor.setPulseAmplitudeRed(0x2F);
  sensor.setPulseAmplitudeIR(0x2F);
  
  Serial.println("✅ Sensor ready!");
  Serial.println("\n📊 Dashboard: http://10.215.21.17:3000");
  Serial.println("💓 Place your finger on the sensor\n");
  
  currentState = STATE_NO_FINGER;
  updateDisplay();
  
  // Set LED to blue (waiting for finger)
  setRingColor(ring.Color(0, 0, 255));
  delay(2000);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  switch(currentState) {
    case STATE_WELCOME:
      display.setCursor(0, 10);
      display.println("CrowdSense");
      display.println("Initializing...");
      break;
      
    case STATE_WIFI_CONNECTING:
      display.setCursor(0, 10);
      display.println("Connecting to");
      display.println("WiFi...");
      display.setCursor(0, 40);
      for(int i = 0; i < (millis() / 500) % 4; i++) display.print(".");
      break;
      
    case STATE_WIFI_CONNECTED:
      display.setCursor(0, 10);
      display.println("WiFi Connected!");
      display.setCursor(0, 25);
      display.print("IP: ");
      display.println(WiFi.localIP());
      break;
      
    case STATE_NO_FINGER:
      display.setTextSize(2);
      display.setCursor(20, 15);
      display.println("Place");
      display.setCursor(20, 35);
      display.println("Hand");
      display.setTextSize(1);
      display.setCursor(0, 55);
      display.println("Place gently on sensor");
      break;
      
    case STATE_MEASURING:
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("HR & SpO2 Monitor");
      
      display.setTextSize(2);
      display.setCursor(0, 20);
      if(avgBPM > 0) {
        display.print(avgBPM);
        display.setTextSize(1);
        display.print(" BPM");
      } else {
        display.print("--");
        display.setTextSize(1);
        display.print(" BPM");
      }
      
      display.setTextSize(2);
      display.setCursor(70, 20);
      if(avgSpO2 > 0) {
        display.print(avgSpO2);
        display.setTextSize(1);
        display.print("%");
      } else {
        display.print("--");
        display.setTextSize(1);
        display.print("%");
      }
      
      display.setTextSize(1);
      display.setCursor(0, 50);
      if(avgBPM > 0 && avgSpO2 > 0) {
        if(avgBPM > 120 || avgBPM < 50 || avgSpO2 < 90) {
          display.print(" ANOMALY!");
        } else {
          display.print("NORMAL");
        }
      } else {
        display.print("Analyzing...");
      }
      
      display.setCursor(100, 55);
      display.print("#");
      display.print(validPeakCount);
      break;
      
    case STATE_ANOMALY:
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(" ALERT!");
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.print(avgBPM);
      display.setTextSize(1);
      display.print(" BPM");
      display.setTextSize(2);
      display.setCursor(70, 20);
      display.print(avgSpO2);
      display.setTextSize(1);
      display.print("%");
      display.setTextSize(1);
      display.setCursor(0, 50);
      if(avgBPM > 120) display.println("HR Too High!");
      else if(avgBPM < 50 && avgBPM > 0) display.println("HR Too Low!");
      if(avgSpO2 < 90 && avgSpO2 > 0) display.println("Low Oxygen!");
      break;
  }
  
  display.display();
}

void sendToBackend() {
  if (WiFi.status() != WL_CONNECTED || avgBPM == 0) {
    return;
  }
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");
  
  String jsonPayload = "{";
  jsonPayload += "\"user_id\":\"" + userId + "\",";
  jsonPayload += "\"location\":\"" + locationId + "\",";
  jsonPayload += "\"heart_rate\":" + String(avgBPM) + ",";
  jsonPayload += "\"spo2\":" + String(avgSpO2) + ",";
  jsonPayload += "\"signal_quality\":50,";
  jsonPayload += "\"total_beats\":" + String(validPeakCount);
  jsonPayload += "}";
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("✓ Data sent: ");
    Serial.print(avgBPM);
    Serial.print(" BPM, SpO2: ");
    Serial.print(avgSpO2);
    Serial.println("%");
  } else {
    Serial.print("❌ Send failed: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
}

void loop() {
  sensor.check();
  
  int irValue = sensor.getIR();
  int redValue = sensor.getRed();
  
  // Auto-calibration: Track min/max values to determine finger presence
  if(irValue > 0) {
    if(irValue < minIR) minIR = irValue;
    if(irValue > maxIR) maxIR = irValue;
    
    // Update threshold every 100 readings
    static int calibrationCounter = 0;
    if(++calibrationCounter > 100) {
      fingerThreshold = minIR + (maxIR - minIR) * 0.3; // 30% above minimum
      Serial.print("Auto-calibration - Min: ");
      Serial.print(minIR);
      Serial.print(" | Max: ");
      Serial.print(maxIR);
      Serial.print(" | Threshold: ");
      Serial.println(fingerThreshold);
      calibrationCounter = 0;
    }
  }
  
  // DEBUG: Print raw values every 2 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.print("IR: ");
    Serial.print(irValue);
    Serial.print(" | Red: ");
    Serial.print(redValue);
    Serial.print(" | Threshold: ");
    Serial.println(fingerThreshold);
    lastPrint = millis();
  }
  
  // Use dynamic threshold for finger detection
  bool fingerPresentNow = (irValue > fingerThreshold + 5000) && (irValue > 10000);
  
  if(fingerPresentNow) {
    
    // Store history
    irHistory[historyIndex] = irValue;
    redHistory[historyIndex] = redValue;
    valueHistory[historyIndex] = irValue;
    historyIndex = (historyIndex + 1) % 50;
    
    // Calculate baseline
    long sum = 0;
    int count = 0;
    for(int i = 0; i < 50; i++) {
      if(valueHistory[i] > 0) {
        sum += valueHistory[i];
        count++;
      }
    }
    if(count > 0) baselineValue = sum / count;
    
    int deviation = irValue - baselineValue;
    
    // Check stability
    if(abs(deviation) < 2000) {
      stableReadings++;
    } else {
      stableReadings = 0;
    }
    
    // Start measuring after stable readings
    if(stableReadings > 30 && !measuring) {
      measuring = true;
      fingerPresentTime = millis();
      currentState = STATE_MEASURING;
      Serial.println("\n✓ Finger detected! Measuring HR & SpO2...\n");
      lastPeakTime = 0;
      validPeakCount = 0;
      updateDisplay();
      
      // Animate LED for measurement start
      for(int i = 0; i <= 100; i += 20) {
        measuringAnimation(i);
        delay(50);
      }
    }
    
    // Heart rate and SpO2 measurement
    if(measuring && millis() - fingerPresentTime > 2000) {
      static bool inPeak = false;
      
      // Calculate SpO2
      int avgIR = 0, avgRed = 0;
      int validSamples = 0;
      for(int i = 0; i < 5; i++) {
        int idx = (historyIndex - 1 - i + 50) % 50;
        if(irHistory[idx] > 0 && redHistory[idx] > 0) {
          avgIR += irHistory[idx];
          avgRed += redHistory[idx];
          validSamples++;
        }
      }
      if(validSamples > 0) {
        avgIR /= validSamples;
        avgRed /= validSamples;
        spo2 = calculateSpO2(avgIR, avgRed);
      }
      
      // Detect heartbeat
      if(deviation > 25 && !inPeak) {
        inPeak = true;
        unsigned long currentTime = millis();
        unsigned long timeSinceLastPeak = currentTime - lastPeakTime;
        
        if(lastPeakTime > 0 && timeSinceLastPeak > 400 && timeSinceLastPeak < 1500) {
          bpm = 60000 / timeSinceLastPeak;
          
          if(bpm > 45 && bpm < 150) {
            validPeakCount++;
            
            // Heartbeat LED flash
            heartbeatFlash();
            
            // Store BPM
            bpmBuffer[bpmIndex] = bpm;
            bpmIndex = (bpmIndex + 1) % 15;
            
            long sumBPM = 0;
            int validCount = 0;
            for(int i = 0; i < 15; i++) {
              if(bpmBuffer[i] > 45 && bpmBuffer[i] < 150) {
                sumBPM += bpmBuffer[i];
                validCount++;
              }
            }
            if(validCount > 0) avgBPM = sumBPM / validCount;
            
            // Store SpO2
            spo2Buffer[spo2Index] = spo2;
            spo2Index = (spo2Index + 1) % 15;
            
            long sumSpO2 = 0;
            int validSpO2 = 0;
            for(int i = 0; i < 15; i++) {
              if(spo2Buffer[i] >= 70 && spo2Buffer[i] <= 100) {
                sumSpO2 += spo2Buffer[i];
                validSpO2++;
              }
            }
            if(validSpO2 > 0) avgSpO2 = sumSpO2 / validSpO2;
            
            // Check anomalies
            if(avgBPM > 120 || (avgBPM < 50 && avgBPM > 0) || (avgSpO2 < 90 && avgSpO2 > 0)) {
              currentState = STATE_ANOMALY;
              anomalyStartTime = millis();
              updateDisplay();
              Serial.print("🚨 ANOMALY! HR: ");
              Serial.print(avgBPM);
              Serial.print(" BPM, SpO2: ");
              Serial.print(avgSpO2);
              Serial.println("%");
            } else {
              if(currentState == STATE_ANOMALY && millis() - anomalyStartTime > 5000) {
                currentState = STATE_MEASURING;
              }
              updateDisplay();
            }
            
            Serial.print("💓 Beat #");
            Serial.print(validPeakCount);
            Serial.print(": ");
            Serial.print(bpm);
            Serial.print(" BPM | SpO2: ");
            Serial.print(spo2);
            Serial.print("% | Avg: ");
            Serial.print(avgBPM);
            Serial.print(" BPM, ");
            Serial.print(avgSpO2);
            Serial.println("%");
            
            if(millis() - lastTransmitTime > 5000) {
              sendToBackend();
              lastTransmitTime = millis();
            }
          }
        }
        lastPeakTime = currentTime;
      }
      
      if(inPeak && deviation < 10) {
        inPeak = false;
      }
      
      // Update LED color based on health status (breathing effect for normal operation)
      static unsigned long lastColorUpdate = 0;
      if(millis() - lastColorUpdate > 50) {
        if(currentState == STATE_ANOMALY) {
          alertFlash();
        } else if(avgBPM > 0 && avgSpO2 > 0) {
          // Normal breathing effect
          breathingEffect(getHealthColor(avgBPM, avgSpO2));
        } else {
          setRingColor(getHealthColor(avgBPM, avgSpO2));
        }
        lastColorUpdate = millis();
      }
    }
    
  } else {
    // Finger removed
    if(measuring) {
      Serial.println("\n🖐️ Finger removed");
      measuring = false;
      avgBPM = 0;
      avgSpO2 = 0;
      validPeakCount = 0;
      memset(bpmBuffer, 0, sizeof(bpmBuffer));
      memset(spo2Buffer, 0, sizeof(spo2Buffer));
      bpmIndex = 0;
      spo2Index = 0;
      lastPeakTime = 0;
      stableReadings = 0;
      currentState = STATE_NO_FINGER;
      updateDisplay();
      
      // Set LED to blue (waiting for finger)
      setRingColor(ring.Color(0, 0, 255));
    }
  }
  
  if(millis() - lastDisplayUpdate > 500 && measuring) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(15);
}