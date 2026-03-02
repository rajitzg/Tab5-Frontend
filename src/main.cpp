#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>

// --- Configuration ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// The IP address of the robot companion computer running the FastAPI node
// and the port it is running on (default 8000)
const char* SERVER_IP = "192.168.1.100";  
const int SERVER_PORT = 8000;

// --- API Endpoints ---
const String URL_START = String("http://") + SERVER_IP + ":" + SERVER_PORT + "/logger/start";
const String URL_SAVE = String("http://") + SERVER_IP + ":" + SERVER_PORT + "/logger/stop_and_save";
const String URL_DISCARD = String("http://") + SERVER_IP + ":" + SERVER_PORT + "/logger/stop_and_discard";

// --- UI Layout ---
// The Tab5 has a massive 7-inch (1024x600) or similar high-res display, but we'll use auto coordinates
// We'll define simple touch zones based on display width and height
int screenW, screenH;

// --- App State ---
bool isRecording = false;

// Button structure for simple touch mapping
struct TouchButton {
  int x, y, w, h;
  uint16_t color;
  String label;
  
  bool contains(int tx, int ty) {
    return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
  }
};

TouchButton btnStart, btnSave, btnDiscard;

// --- Function Declarations ---
bool sendPostRequest(const String& url);
void drawUI();
void drawStatus(const String& msg, uint16_t color);

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // Initialize screen
  screenW = M5.Display.width();
  screenH = M5.Display.height();
  
  int btnWidth = screenW - 40;
  int startX = 20;
  int startY = 80;
  
  // Start button takes up most of the screen
  int startBtnHeight = screenH - 120;
  btnStart = {startX, startY, btnWidth, startBtnHeight, TFT_DARKGREEN, "START RECORDING"};
  
  // When recording, we split the screen: a big save button, and a smaller discard button
  int saveBtnHeight = (int)(startBtnHeight * 0.7);
  int discardBtnHeight = startBtnHeight - saveBtnHeight - 20;
  
  btnSave = {startX, startY, btnWidth, saveBtnHeight, TFT_MAROON, "STOP & SAVE"};
  btnDiscard = {startX, startY + saveBtnHeight + 20, btnWidth, discardBtnHeight, TFT_DARKGREY, "STOP & DISCARD"};
  
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("ROS Teleop Interface", screenW/2, 10);
  
  // Connect to WiFi
  drawStatus("Connecting to WiFi...", TFT_YELLOW);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    drawStatus("WiFi Connected! IP: " + WiFi.localIP().toString(), TFT_CYAN);
  } else {
    drawStatus("WiFi Failed! Please check credentials.", TFT_RED);
  }
  
  drawUI();
}

void loop() {
  M5.update();
  
  // Check for touch events
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail();
    
    // Only trigger on touch release to prevent multiple triggers
    if (t.wasReleased()) {
      int tx = t.x;
      int ty = t.y;
      
      if (!isRecording && btnStart.contains(tx, ty)) {
        drawStatus("Sending START command...", TFT_YELLOW);
        if (sendPostRequest(URL_START)) {
          isRecording = true;
          drawUI();
        }
      } 
      else if (isRecording) {
        if (btnSave.contains(tx, ty)) {
          drawStatus("Sending STOP & SAVE command...", TFT_YELLOW);
          if (sendPostRequest(URL_SAVE)) {
            isRecording = false;
            drawUI();
          }
        }
        else if (btnDiscard.contains(tx, ty)) {
          drawStatus("Sending STOP & DISCARD command...", TFT_YELLOW);
          if (sendPostRequest(URL_DISCARD)) {
            isRecording = false;
            drawUI();
          }
        }
      }
      
      // Small debounce delay
      delay(200);
    }
  }
  
  delay(10);
}

// --- Helper Functions ---

// Returns true if the HTTP request was successful (HTTP 200)
bool sendPostRequest(const String& url) {
  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);
    
    // Post empty body
    int httpResponseCode = http.POST("");
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      Serial.println(response);
      
      if (httpResponseCode == 200) {
         drawStatus("Success! Code: " + String(httpResponseCode), TFT_GREEN);
         success = true;
      } else {
         drawStatus("API Error: Code " + String(httpResponseCode), TFT_ORANGE);
      }
      
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      drawStatus("Connection failed to robot.", TFT_RED);
    }
    
    http.end();
  } else {
    drawStatus("WiFi not connected.", TFT_RED);
  }
  return success;
}

void drawButton(TouchButton b) {
  M5.Display.fillRoundRect(b.x, b.y, b.w, b.h, 10, b.color);
  M5.Display.drawRoundRect(b.x, b.y, b.w, b.h, 10, TFT_WHITE);
  
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE);
  // Draw label perfectly centered in the button
  M5.Display.drawString(b.label, b.x + (b.w/2), b.y + (b.h/2));
}

void drawUI() {
  // Clear the button area
  M5.Display.fillRect(0, 80, screenW, screenH - 80, TFT_BLACK);
  
  if (isRecording) {
    drawButton(btnSave);
    drawButton(btnDiscard);
  } else {
    drawButton(btnStart);
  }
}

void drawStatus(const String& msg, uint16_t color) {
  // Clear status area (just below the title)
  M5.Display.fillRect(0, 40, screenW, 30, TFT_BLACK);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(color);
  M5.Display.drawString(msg, screenW/2, 45);
}