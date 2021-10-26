
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

#ifndef PSTR
  #define PSTR // Make Arduino Due happy
#endif

// GFX Related libraries
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include "helium_animation.h"

// WiFi and Net libraries
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "WiFiSetup.h"
#include <ArduinoJson.h>
#include "ota.h"

#include "sensitive.h"

// Time, event scheduling, and NTP library
#include <ezTime.h>

#define PIN D5

#define DEFAULT_COLOR 0xAA59AA
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 1000
#define DEFAULT_MODE FX_MODE_STATIC
#define STEPS_PER_DISPLAY_UPDATE 1000

extern const char index_html[];
extern const char main_js[];

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define bool_to_str(a) ((a)?("true"):("false"))

#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8

// MATRIX DECLARATION:
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(MATRIX_WIDTH, MATRIX_HEIGHT, PIN,
                            NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
                            NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
                            NEO_GRB            + NEO_KHZ800);


#define HTTP_PORT 80
WEB_SERVER server(HTTP_PORT);

#define WIFI_TIMEOUT 300             // checks WiFi every ...s. Reset after this time, if WiFi cannot reconnect.

#define DISPLAY_UPDATE_INTERVAL 100

#define LED_PIN 2                       // 0 = GPIO0, 2=GPIO2

HTTPClient http;

int scroll_speed = 500;
int scroll_pos = 0;


// Millisecond resolution events
unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;
unsigned long last_display_update_time = 0;
unsigned long last_pos_update_time = 0;

// Stat display bools
bool display_daily_average = true;
bool display_wallet_value = true;
bool display_daily_total = true;
bool display_thirty_day_total = true;
bool display_witnesses = true;
bool display_oracle_price = true;


// Helium stats
float daily_average = 0;
float daily_total = 0;
float wallet_value = 0;
float previous_wallet_value = 0;
float thirty_day_total = 0;
float witnesses = 0;
float oracle_price = 0;

// Counters and status variables
int animationCounter = 0;
boolean happyDanceAnimation = false;
boolean initializedWalletValue = false;

String display_string; // String to hold the display info

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
  matrix.setTextColor(matrix.Color(200, 200, 255));

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
  setInterval(60 * 60);
  //setDebug(INFO);

  waitForSync();

  Serial.println("UTC: " + UTC.dateTime());

  Omaha.setLocation("America/Chicago");
  Omaha.setDefault();
  Serial.println("Omaha time: " + Omaha.dateTime());

  //timeClient.begin();

  Serial.println("getting data");

  check_wifi();
  get_daily_total(); // This will ensure we have some data, and then set up the event to continuously update
  get_wallet_value();
  get_thirty_day_total();
  get_oracle_price();
  scroll_text();
  update_display();
  //setEvent( get_daily_total,updateInterval() );

  Serial.println("ready!");
}

time_t updateInterval() {

  time_t event_time = Omaha.now() + (10 * (60)); // x*(60) = x minutes between updates
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}

time_t retryUpdateInterval() {

  time_t event_time = Omaha.now() + 20; // Retry in 20 seconds
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}

void loop() {
  //timeClient.update();
  //Serial.println(Omaha.dateTime(ISO8601));
  events();
  ArduinoOTA.handle();
  // put your main code here, to run repeatedly:
  server.handleClient();
  yield();

  unsigned long now_ms = millis();
  if (happyDanceAnimation) {
    //TODO: Fancy animation because we made money
    deposit_animation();
    animationCounter++;
    yield();
  } else {
    if (previous_wallet_value < wallet_value) {
      if (initializedWalletValue) {
        Serial.println("Starting Happy Dance animation");
        Serial.println(previous_wallet_value);
        Serial.println(wallet_value);
        previous_wallet_value = wallet_value;
        happyDanceAnimation = true;
      }
    } else {
      if (now_ms - last_display_update_time > DISPLAY_UPDATE_INTERVAL) {
        update_display();
        last_display_update_time = now_ms;
      }
      if (now_ms - last_pos_update_time > scroll_speed) {
        scroll_text();
        last_pos_update_time = now_ms;
      }
    }
  }
  //  if (display_clock % 600 == 0) {
  //    get_daily_total();
  //  }
  //delay(1000);
  yield();
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


//setEvent(update_display,Omaha.now() + DISPLAY_UPDATE_INTERVAL);


void check_wifi() {
  Serial.print("Checking WiFi... ");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    wifi_setup();
  } else {
    Serial.println("OK");
  }
  setEvent(check_wifi, Omaha.now() + WIFI_TIMEOUT);
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
  String temp_display_string = "";
  if (display_daily_total) {
    temp_display_string = temp_display_string + " 24Hrs:" + daily_total;
  }
  if (display_wallet_value) {
    temp_display_string = temp_display_string + " Wallet:" + wallet_value;
  }
  if (display_thirty_day_total) {
    temp_display_string = temp_display_string + " 30 Days:" + thirty_day_total;
  }
  if (display_oracle_price) {
    temp_display_string = temp_display_string + " HNT Value:$" + oracle_price;
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
      setEvent( get_daily_total, updateInterval() );

    } else {
      Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_daily_total, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
  yield();
}

void get_wallet_value() {
  Serial.println("Fetching new wallet value");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/accounts/" + ACCOUNT_ADDRESS;
    Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request

    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"]["balance"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      if (initializedWalletValue){
        previous_wallet_value = wallet_value;
      }
      wallet_value = doc["data"]["balance"];
      wallet_value /= 100000000;
      if (!initializedWalletValue){
        previous_wallet_value = wallet_value;
      }

      initializedWalletValue = true;

      Serial.print("Wallet Value: ");
      Serial.println(wallet_value);             //Print the response payload
      setEvent( get_wallet_value, updateInterval() );

    } else {
      Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_wallet_value, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
  yield();
}

void get_thirty_day_total() {
  Serial.println("Fetching new thiry day total");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    time_t month_ago = Omaha.now() - 86400 * 30;
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/rewards/sum?max_time=" + Omaha.dateTime(ISO8601) + "&min_time=" + Omaha.dateTime(month_ago, ISO8601);
    Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request

    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"]["total"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      thirty_day_total = doc["data"]["total"];
      Serial.print("30 Day Total: ");
      Serial.println(thirty_day_total);             //Print the response payload
      setEvent( get_thirty_day_total, updateInterval() );

    } else {
      Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_thirty_day_total, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
  yield();
}

void get_oracle_price() {
  Serial.println("Fetching oracle price");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/oracle/prices/current";
    Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request

    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"]["price"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      oracle_price = doc["data"]["price"];
      oracle_price /= 100000000;
      Serial.print("Oracle Price: ");
      Serial.println(oracle_price);             //Print the response payload
      setEvent( get_oracle_price, updateInterval() );

    } else {
      Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_oracle_price, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
  yield();
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
        matrix.setBrightness(max(matrix.getBrightness() - 1, 0));
      } else {
        matrix.setBrightness(min(matrix.getBrightness() + 1, 60));
      }
      Serial.print("brightness is "); Serial.println(matrix.getBrightness());
    }
  }
  server.send(200, "text/plain", "OK");
}
