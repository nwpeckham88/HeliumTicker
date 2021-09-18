
#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <WebServer.h>
#define WEB_SERVER WebServer
#define ESP_RESET ESP.restart()
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#define WEB_SERVER ESP8266WebServer
#define ESP_RESET ESP.reset()
#endif


#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#ifndef PSTR
 #define PSTR // Make Arduino Due happy
#endif

#define PIN 5

#define WIFI_SSID "CenturyLink5739"
#define WIFI_PASSWORD "jjugy4z5jdnw76"

#define DEFAULT_COLOR 0xFF5900
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 1000
#define DEFAULT_MODE FX_MODE_STATIC

extern const char index_html[];
extern const char main_js[];

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define bool_to_str(a) ((a)?("true"):("false"))

#define LED_PIN 2                       // 0 = GPIO0, 2=GPIO2

#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define HTTP_PORT 80

// MATRIX DECLARATION:
// Parameter 1 = width of NeoPixel matrix
// Parameter 2 = height of matrix
// Parameter 3 = pin number (most are valid)
// Parameter 4 = matrix layout flags, add together as needed:
//   NEO_MATRIX_TOP, NEO_MATRIX_BOTTOM, NEO_MATRIX_LEFT, NEO_MATRIX_RIGHT:
//     Position of the FIRST LED in the matrix; pick two, e.g.
//     NEO_MATRIX_TOP + NEO_MATRIX_LEFT for the top-left corner.
//   NEO_MATRIX_ROWS, NEO_MATRIX_COLUMNS: LEDs are arranged in horizontal
//     rows or in vertical columns, respectively; pick one or the other.
//   NEO_MATRIX_PROGRESSIVE, NEO_MATRIX_ZIGZAG: all rows/columns proceed
//     in the same order, or alternate lines reverse direction; pick one.
//   See example below for these values in action.
// Parameter 5 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_GRBW    Pixels are wired for GRBW bitstream (RGB+W NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)


// Example for NeoPixel Shield.  In this application we'd like to use it
// as a 5x8 tall matrix, with the USB port positioned at the top of the
// Arduino.  When held that way, the first pixel is at the top right, and
// lines are arranged in columns, progressive order.  The shield uses
// 800 KHz (v2) pixels that expect GRB color data.
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32, 8, PIN,
  NEO_MATRIX_BOTTOM    + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
  NEO_GRB            + NEO_KHZ800);
//WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WEB_SERVER server(HTTP_PORT);

unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;

bool display_daily_average = true;
bool display_wallet_value = true;
bool display_daily_total = true;
bool display_thirty_day_total = true;
bool display_witnesses = true;

float daily_average = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nStarting...");

  Serial.println("WS2812FX setup");
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(40);
  matrix.setTextColor(matrix.Color(255, 0, 0)); // default color is red

  Serial.println("Wifi setup");
  wifi_setup();

  Serial.println("HTTP server setup");
  server.on("/", srv_handle_index_html);
  server.on("/set", srv_handle_set);
  server.on("/getStat", srv_handle_get_stat);
  server.onNotFound(srv_handle_not_found);
  server.begin();
  Serial.println("HTTP server started.");

  Serial.println("ready!");
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long now = millis();

  server.handleClient();
  
  matrix.fillScreen(0);
  matrix.setCursor(0, 0);
  matrix.print(F("Howdy"));
  matrix.show();
  delay(100);

  if (now - last_wifi_check_time > WIFI_TIMEOUT) {
    Serial.print("Checking WiFi... ");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
      wifi_setup();
    } else {
      Serial.println("OK");
    }
    last_wifi_check_time = now;
  }
}

/*
   Connect to WiFi. If no connection is made within WIFI_TIMEOUT, ESP gets reset.
*/
void wifi_setup() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);
#ifdef STATIC_IP
  WiFi.config(ip, gateway, subnet);
#endif

  unsigned long connect_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (millis() - connect_start > WIFI_TIMEOUT) {
      Serial.println();
      Serial.print("Tried ");
      Serial.print(WIFI_TIMEOUT);
      Serial.print("ms. Resetting ESP now.");
      ESP_RESET;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

void srv_handle_not_found() {
  server.send(404, "text/plain", "File Not Found");
}

void srv_handle_index_html() {
  server.send_P(200, "text/html", index_html);
}

void srv_handle_set_data() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "daily-average") {
      if (server.arg(i) == "true") {
        display_daily_average = true;
      } else {
        display_daily_average = false;
      }
      server.send_P(200, "text/html", "OK");
      return;
    }
  }
}

void srv_handle_get_stat() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "daily-average") {
      server.send_P(200, "text/html", bool_to_str(display_daily_average));
      return;
    } else {
      server.send_P(200, "text/html", "false");
    }
  }
}


void srv_handle_set() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "c") {
      uint32_t tmp = (uint32_t) strtol(server.arg(i).c_str(), NULL, 10);
      if (tmp >= 0x000000 && tmp <= 0xFFFFFF) {
        //matrix.setColor(tmp);
      }
    }

    if (server.argName(i) == "b") {
      if (server.arg(i)[0] == '-') {
        matrix.setBrightness(matrix.getBrightness() * 0.8);
      } else if (server.arg(i)[0] == ' ') {
        matrix.setBrightness(min(max(matrix.getBrightness(), 5) * 1.2, 255));
      } else { // set brightness directly
        uint8_t tmp = (uint8_t) strtol(server.arg(i).c_str(), NULL, 10);
        matrix.setBrightness(tmp);
      }
      Serial.print("brightness is "); Serial.println(matrix.getBrightness());
    }

    if (server.argName(i) == "da") {
      if (server.arg(i)[0] == 'true') {
        display_daily_average = true;
      } else {
        display_daily_average = false;
      }
    }

    if (server.argName(i) == "wv") {
      if (server.arg(i)[0] == 'true') {
        display_wallet_value = true;
      } else {
        display_wallet_value = false;
      }
    }

    if (server.argName(i) == "dt") {
      if (server.arg(i)[0] == 'true') {
        display_daily_total = true;
      } else {
        display_daily_total = false;
      }
    }

    if (server.argName(i) == "witnesses") {
      if (server.arg(i)[0] == 'true') {
        display_witnesses = true;
      } else {
        display_witnesses = false;
      }
    }
    if (server.argName(i) == "thirty") {
      if (server.arg(i)[0] == 'true') {
        display_thirty_day_total = true;
      } else {
        display_thirty_day_total = false;
      }
    }
  }
  server.send(200, "text/plain", "OK");
}
