
#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <WebServer.h>
#define WEB_SERVER WebServer
#define ESP_RESET ESP.restart()
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#define WEB_SERVER ESP8266WebServer
#define ESP_RESET ESP.reset()
#endif

HTTPClient http;

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#ifndef PSTR
#define PSTR // Make Arduino Due happy
#endif

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "WiFiSetup.h"

#include <ezTime.h>

#include <ArduinoJson.h>

#define PIN D5

#define WIFI_SSID "CenturyLink5739"
#define WIFI_PASSWORD "jjugy4z5jdnw76"

#define DEFAULT_COLOR 0xFF5900
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 1000
#define DEFAULT_MODE FX_MODE_STATIC
#define STEPS_PER_DISPLAY_UPDATE 1000


extern const char index_html[];
extern const char main_js[];

const String HOTSPOT_ADDRESS = "112bdGoWiDD9FTfTMHt2xgbAY2mn6dEULjGxwpZfJfhQRS9TYRGx";

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define bool_to_str(a) ((a)?("true"):("false"))

#define LED_PIN 2                       // 0 = GPIO0, 2=GPIO2


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
                            NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
                            NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
                            NEO_GRB            + NEO_KHZ800);
//WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WEB_SERVER server(HTTP_PORT);

#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define DISPLAY_UPDATE_INTERVAL 100
int scroll_speed = 700;
int scroll_pos = 0;

unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;
unsigned long last_display_update_time = 0;
unsigned long last_pos_update_time = 0;


bool display_daily_average = true;
bool display_wallet_value = true;
bool display_daily_total = true;
bool display_thirty_day_total = true;
bool display_witnesses = true;

float daily_average = 0;
float daily_total = 0;

Timezone Omaha;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nStarting...");

  Serial.println("WS2812FX setup");
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setBrightness(10);
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

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  OTA_Setup();

  // Set NTP polling frequency (seconds)
  setInterval(60*60);
  //setDebug(INFO);

  waitForSync();

  Serial.println("UTC: " + UTC.dateTime());

  Omaha.setLocation("America/Chicago");
  Omaha.setDefault();
  Serial.println("Omaha time: " + Omaha.dateTime());

  //timeClient.begin();

  //Serial.println("getting data");

  check_wifi();
  get_daily_total(); // This will ensure we have some data, and then set up the event to continuously update
  scroll_text();
  update_display();
  //setEvent( get_daily_total,updateInterval() );

  Serial.println("ready!");
}

time_t updateInterval() {

  time_t event_time = Omaha.now() + (2 * (60)); // x*(60) = x minutes between updates
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}



/*
   Connect to WiFi. If no connection is made within WIFI_TIMEOUT, ESP gets reset.
*/
void wifi_setup() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

String display_string;

void loop() {
  //timeClient.update();
  //Serial.println(Omaha.dateTime(ISO8601));
  events();
  ArduinoOTA.handle();
  // put your main code here, to run repeatedly:
  server.handleClient();

  unsigned long now_ms = millis();
  if (now_ms - last_display_update_time > DISPLAY_UPDATE_INTERVAL) {
    update_display();
    last_display_update_time = now_ms;
  }

  if (now_ms - last_pos_update_time > scroll_speed) {
    scroll_text();
    last_pos_update_time = now_ms;
  }
  //  if (display_clock % 600 == 0) {
  //    get_daily_total();
  //  }
  //delay(1000);
}

void check_wifi() {
  Serial.print("Checking WiFi... ");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    wifi_setup();
  } else {
    Serial.println("OK");
  }
  setEvent(check_wifi,Omaha.now() + WIFI_TIMEOUT);
}

void update_display() {
  //Serial.println("Updating display");
  matrix.fillScreen(0);
  matrix.setCursor(0, 0);
  matrix.print(display_string);
  matrix.show();
  //setEvent(update_display,Omaha.now() + DISPLAY_UPDATE_INTERVAL);
}

void scroll_text() {
  scroll_pos++;
  display_string = build_display_string(scroll_pos);
  //setEvent(scroll_text,Omaha.now() + scroll_speed);
}

String build_display_string(int disp_clock) {
  String temp_display_string = "  ";
  if (display_daily_total) {
    temp_display_string = temp_display_string + "24Hr Total: " + daily_total;
  }

  int pos = disp_clock % temp_display_string.length();

  if (pos > 0) {
    temp_display_string = temp_display_string.substring(pos) + temp_display_string.substring(0, pos - 1);
  }

  //display_string += "  ";

  //Serial.println(display_string);
  return temp_display_string;

}

void get_daily_total() {
  Serial.println("Fetching new daily total");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    time_t day_ago = Omaha.now() - 86400;
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/rewards/sum?max_time=" + Omaha.dateTime(ISO8601) + "&min_time=" + Omaha.dateTime(day_ago, ISO8601);
    Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request

    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> da_filter;
      da_filter["data"]["total"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(da_filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      daily_total = doc["data"]["total"];
      Serial.print("Daily total updated: ");
      Serial.println(daily_total);             //Print the response payload

    }

    http.end();   //Close connection
  }
  // Set up event for next time
  setEvent( get_daily_total, updateInterval() );
}

void srv_handle_not_found() {
  server.send(404, "text/plain", "File Not Found");
}

void srv_handle_index_html() {
  server.send_P(200, "text/html", index_html);
}

void srv_handle_get_stat() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "daily-average") {
      server.send_P(200, "text/html", bool_to_str(display_daily_average));
    } else if (server.argName(i) == "total") {
      server.send_P(200, "text/html", bool_to_str(display_wallet_value));
    } else if (server.argName(i) == "daily-total") {
      server.send_P(200, "text/html", bool_to_str(display_daily_total));
    } else if (server.argName(i) == "thirty-day-total") {
      server.send_P(200, "text/html", bool_to_str(display_thirty_day_total));
    } else if (server.argName(i) == "witnesses") {
      server.send_P(200, "text/html", bool_to_str(display_witnesses));
    } else {
      server.send_P(200, "text/html", "false");
    }
  }
}

void srv_handle_set() {
  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "daily-average") {
      if (server.arg(i) == "true") {
        display_daily_average = true;
      } else {
        display_daily_average = false;
      }
      server.send_P(200, "text/html", "OK");
    }

    if (server.argName(i) == "daily-total") {
      if (server.arg(i) == "true") {
        display_daily_total = true;
      } else {
        display_daily_total = false;
      }
      server.send_P(200, "text/html", "OK");
    }

    if (server.argName(i) == "total") {
      if (server.arg(i) == "true") {
        display_wallet_value = true;
      } else {
        display_wallet_value = false;
      }
      server.send_P(200, "text/html", "OK");
    }

    if (server.argName(i) == "witnesses") {
      if (server.arg(i) == "true") {
        display_witnesses = true;
      } else {
        display_witnesses = false;
      }
      server.send_P(200, "text/html", "OK");
    }

    if (server.argName(i) == "thirty-day-total") {
      if (server.arg(i) == "true") {
        display_thirty_day_total = true;
      } else {
        display_thirty_day_total = false;
      }
      server.send_P(200, "text/html", "OK");
    }

    if (server.argName(i) == "c") {
      uint32_t tmp = (uint32_t) strtol(server.arg(i).c_str(), NULL, 10);
      if (tmp >= 0x000000 && tmp <= 0xFFFFFF) {
        //matrix.setColor(tmp);
        uint32_t rgb_color = matrix.ColorHSV(tmp);
        matrix.setTextColor(rgb_color);
      }
    }

    if (server.argName(i) == "b") {
      if (server.arg(i)[0] == '-') {
        matrix.setBrightness(max(matrix.getBrightness()-1,0));
      } else {
        matrix.setBrightness(min(matrix.getBrightness()+1,60));
      }
      Serial.print("brightness is "); Serial.println(matrix.getBrightness());
    }
  }
  server.send(200, "text/plain", "OK");
}

void OTA_Setup() {

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("heliumticker");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");


  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}
