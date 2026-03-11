#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "secrets.h"

// The robot companion computer is resolved by mDNS hostname (e.g. "robot" -> "robot.local")
// This way the IP can differ per network without any code changes.
const int SERVER_PORT = 8000;
const unsigned long STATUS_INTERVAL_MS = 3000; // Poll every 3 seconds

// --- API Endpoints (rebuilt after mDNS resolution) ---
String SERVER_BASE;
String URL_START;
String URL_SAVE;
String URL_DISCARD;
String URL_STATUS;

// --- UI Layout ---
// The Tab5 has a massive 7-inch (1024x600) or similar high-res display, but we'll use auto coordinates
// We'll define simple touch zones based on display width and height
int screenW, screenH;

// --- App State ---
bool isRecording = false;
unsigned long lastStatusCheck = 0;

// Button structure for simple touch mapping
struct TouchButton {
  int x, y, w, h;
  uint16_t color;
  String label;
  
  bool contains(int tx, int ty) {
    return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
  }
};

TouchButton btnStart, btnSave, btnDiscard, btnNetwork;

// --- Function Declarations ---
bool sendPostRequest(const String& url);
void fetchRosStatus();
void drawUI();
void drawStatus(const String& msg, uint16_t color);
void connectToWiFi(const char* ssid, const char* password);
void resolveServerIP();

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // Initialize screen
  screenW = M5.Display.width();
  screenH = M5.Display.height();
  
  int btnWidth = screenW - 40;
  int startX = 20;
  int startY = 80;

  int networkBtnHeight = 100;
  btnNetwork = {startX, startY, btnWidth, networkBtnHeight, TFT_DARKGREEN, "SWITCH NETWORK"};
  
  // Start button takes up most of the screen
  int startBtnHeight = screenH - 240;
  btnStart = {startX, startY + networkBtnHeight + 20, btnWidth, startBtnHeight, TFT_DARKGREEN, "START RECORDING"};
  
  // When recording, we split the screen: a big save button, and a smaller discard button
  int saveBtnHeight = (int)(startBtnHeight * 0.7);
  int discardBtnHeight = startBtnHeight - saveBtnHeight - 20;
  
  btnSave = {startX, startY + networkBtnHeight + 20, btnWidth, saveBtnHeight, TFT_MAROON, "STOP & SAVE"};
  btnDiscard = {startX, startY + networkBtnHeight + 20 + saveBtnHeight + 20, btnWidth, discardBtnHeight, TFT_DARKGREY, "STOP & DISCARD"};
  
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("ROS Teleop Interface", screenW/2, 10);
  
  // Connect to WiFi
  drawStatus("Connecting to WiFi...", TFT_YELLOW);
  connectToWiFi(WIFI1_SSID, WIFI1_PASSWORD);
  if (WiFi.status() != WL_CONNECTED) {
    drawStatus("Failed to connect to WiFi. Switching to backup...", TFT_RED);
    delay(1000);
    connectToWiFi(WIFI2_SSID, WIFI2_PASSWORD);
  }
  
  drawUI();
}

void loop() {
  M5.update();

  // Periodic status sync
  unsigned long now = millis();
  if (now - lastStatusCheck >= STATUS_INTERVAL_MS) {
    lastStatusCheck = now;
    fetchRosStatus();
  }
  
  // Check for touch events
  // Use isPressed() + static flag instead of wasPressed()/wasReleased():
  // wasPressed/wasReleased are only true for ONE frame, so they get silently
  // dropped if M5.update() is delayed (e.g. during HTTP calls).
  // isPressed() is true for the entire touch duration, so it's never missed.
  static bool touchHandled = false;
  auto t = M5.Touch.getDetail();
  if (t.isPressed()) {
    if (!touchHandled) {
      touchHandled = true;
      int tx = t.x;
      int ty = t.y;

      if (btnNetwork.contains(tx, ty)) {
        if (WiFi.SSID() == WIFI1_SSID) {
          drawStatus("Switching to: " + String(WIFI2_SSID) + "...", TFT_YELLOW);
          connectToWiFi(WIFI2_SSID, WIFI2_PASSWORD);
        } else {
          drawStatus("Switching to: " + String(WIFI1_SSID) + "...", TFT_YELLOW);
          connectToWiFi(WIFI1_SSID, WIFI1_PASSWORD);
        }
      }
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
    }
  } else {
    touchHandled = false;  // finger lifted, allow next press
  }
  
  delay(10);
}

// --- Helper Functions ---

// Returns true if the HTTP request was successful (HTTP 200)
bool sendPostRequest(const String& url) {
  bool success = false;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(3000);  // 3s timeout for command posts
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

void fetchRosStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.setConnectTimeout(500);  // 500ms TCP connect - unreachable hosts block here, not in setTimeout
  http.setTimeout(1000);        // 1s response read timeout
  http.begin(URL_STATUS);
  int code = http.GET();

  if (code == 200) {
    String body = http.getString();
    // Simple JSON parse: look for "true" or "false" in "is_recording"
    bool newState = body.indexOf("\"is_recording\":true") >= 0;
    if (newState != isRecording) {
      isRecording = newState;
      drawUI();  // Re-render buttons to match actual state
    }
  }
  http.end();
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
  drawButton(btnNetwork);
  
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

void connectToWiFi(const char* ssid, const char* password) {
  Serial.println("Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    drawStatus("Connected! Resolving robot...", TFT_CYAN);
    resolveServerIP();
  } else {
    drawStatus("WiFi Failed! Please check credentials.", TFT_RED);
  }
}

void resolveServerIP() {
  // Resolve the robot's mDNS hostname to an IP on the current network.
  // The hostname is defined in secrets.h as ROBOT_HOSTNAME (without .local).
  IPAddress ip = MDNS.queryHost(ROBOT_HOSTNAME, 3000 /* ms timeout */);
  if (ip != INADDR_NONE) {
    SERVER_BASE = String("http://") + ip.toString() + ":" + SERVER_PORT;
    URL_START   = SERVER_BASE + "/logger/start";
    URL_SAVE    = SERVER_BASE + "/logger/stop_and_save";
    URL_DISCARD = SERVER_BASE + "/logger/stop_and_discard";
    URL_STATUS  = SERVER_BASE + "/logger/status";
    drawStatus("Robot @ " + ip.toString(), TFT_GREEN);
    Serial.println("Robot resolved to: " + ip.toString());
  } else {
    SERVER_BASE = "";
    URL_START = URL_SAVE = URL_DISCARD = URL_STATUS = "";
    drawStatus("Robot not found on this network!", TFT_RED);
    Serial.println("mDNS resolution failed for: " + String(ROBOT_HOSTNAME));
  }
}