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

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#ifndef PSTR
#define PSTR // Make Arduino Due happy
#endif

// WiFi Password and Hotspot/Account info (not exactly sensitive, but personal)
#include "sensitive.h"

#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "WiFiSetup.h"
#include "BMP.h"

#include <ezTime.h>

#include <ArduinoJson.h>

#define DEFAULT_COLOR 0xAA59AA
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 1000
#define DEFAULT_MODE FX_MODE_STATIC
#define STEPS_PER_DISPLAY_UPDATE 1000

// This could also be defined as matrix->color(255,0,0) but those defines
// are meant to work for adafruit_gfx backends that are lacking color()
#define LED_BLACK    0

#define LED_RED_VERYLOW   (3 <<  11)
#define LED_RED_LOW     (7 <<  11)
#define LED_RED_MEDIUM    (15 << 11)
#define LED_RED_HIGH    (31 << 11)

#define LED_GREEN_VERYLOW (1 <<  5)
#define LED_GREEN_LOW     (15 << 5)
#define LED_GREEN_MEDIUM  (31 << 5)
#define LED_GREEN_HIGH    (63 << 5)

#define LED_BLUE_VERYLOW  3
#define LED_BLUE_LOW    7
#define LED_BLUE_MEDIUM   15
#define LED_BLUE_HIGH     31

#define LED_ORANGE_VERYLOW  (LED_RED_VERYLOW + LED_GREEN_VERYLOW)
#define LED_ORANGE_LOW    (LED_RED_LOW     + LED_GREEN_LOW)
#define LED_ORANGE_MEDIUM (LED_RED_MEDIUM  + LED_GREEN_MEDIUM)
#define LED_ORANGE_HIGH   (LED_RED_HIGH    + LED_GREEN_HIGH)

#define LED_PURPLE_VERYLOW  (LED_RED_VERYLOW + LED_BLUE_VERYLOW)
#define LED_PURPLE_LOW    (LED_RED_LOW     + LED_BLUE_LOW)
#define LED_PURPLE_MEDIUM (LED_RED_MEDIUM  + LED_BLUE_MEDIUM)
#define LED_PURPLE_HIGH   (LED_RED_HIGH    + LED_BLUE_HIGH)

#define LED_CYAN_VERYLOW  (LED_GREEN_VERYLOW + LED_BLUE_VERYLOW)
#define LED_CYAN_LOW    (LED_GREEN_LOW     + LED_BLUE_LOW)
#define LED_CYAN_MEDIUM   (LED_GREEN_MEDIUM  + LED_BLUE_MEDIUM)
#define LED_CYAN_HIGH   (LED_GREEN_HIGH    + LED_BLUE_HIGH)

#define LED_WHITE_VERYLOW (LED_RED_VERYLOW + LED_GREEN_VERYLOW + LED_BLUE_VERYLOW)
#define LED_WHITE_LOW   (LED_RED_LOW     + LED_GREEN_LOW     + LED_BLUE_LOW)
#define LED_WHITE_MEDIUM  (LED_RED_MEDIUM  + LED_GREEN_MEDIUM  + LED_BLUE_MEDIUM)
#define LED_WHITE_HIGH    (LED_RED_HIGH    + LED_GREEN_HIGH    + LED_BLUE_HIGH)

extern const char index_html[];
extern const char main_js[];

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define bool_to_str(a) ((a)?("true"):("false"))

#define MATRIX_PIN D5
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8


#define HTTP_PORT 80

HTTPClient http;

// MATRIX DECLARATION:
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_PIN ,
                            NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
                            NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG,
                            NEO_GRB            + NEO_KHZ800);
//WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
WEB_SERVER server(HTTP_PORT);

#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define DISPLAY_UPDATE_INTERVAL 100
int scroll_speed = 500;
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
bool display_oracle_price = true;

float daily_average = 0;
float daily_total = 0;
float wallet_value = 0;
float previous_wallet_value = 0;
float thirty_day_total = 0;
float witnesses = 0;
float oracle_price = 0;
float last_wallet_deposit = 0;

String last_activity_hash = "";

int animationCounter = 0;
boolean happyDanceAnimation = false;
boolean initializedWalletValue = false;
String display_string;

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
  server.on("/fakeDeposit", srv_handle_fake_deposit);
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
  //get_last_activity();
  scroll_text();
  update_display();
  //setEvent( get_daily_total,updateInterval() );

  Serial.println("ready!");
}

time_t updateInterval() {

  time_t event_time = Omaha.now() + (20 * (60)); // x*(60) = x minutes between updates
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

void loop() {
  //timeClient.update();
  //Serial.println(Omaha.dateTime(ISO8601));
  events();
  ArduinoOTA.handle();
  // put your main code here, to run repeatedly:
  server.handleClient();

  unsigned long now_ms = millis();
  if (happyDanceAnimation) {
    //TODO: Fancy animation because we made money
    deposit_animation();
    animationCounter++;
  } else {
    if (previous_wallet_value < wallet_value) {
      if (initializedWalletValue) {
        last_wallet_deposit = wallet_value - previous_wallet_value;
        previous_wallet_value = wallet_value;
        Serial.println("Starting Happy Dance animation");
        //Serial.println(previous_wallet_value);
        //Serial.println(wallet_value);
        Serial.print("We made: ");
        Serial.println(last_wallet_deposit);
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
}
int sprite = 0;

void deposit_animation() {
  //Serial.println("Updating display");
  int pos_mod = animationCounter % MATRIX_HEIGHT / 4;
  matrix.clear();
  display_rgbBitmap(sprite%3,0,0);
  matrix.setCursor(9,0);
  String deposit_string = "We made " + String(last_wallet_deposit,6) + " HNT!  ";
  int pos = sprite % deposit_string.length();
  if (pos > 0) {
    deposit_string = deposit_string.substring(pos) + deposit_string.substring(0, pos - 1);
  }
  matrix.print(deposit_string);
  matrix.show();
  if (animationCounter == 30) {
    animationCounter = 0;
    sprite++;
    if (sprite > 3*deposit_string.length() - 2){ // Subtract a couple ticks so it doesn't last quite as long.
      sprite = 0;
      happyDanceAnimation = false;
    }
  }
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
  String temp_display_string = "  ";
  if (display_daily_total) {
    temp_display_string = temp_display_string + "24Hrs:" + daily_total;
  }
  if (display_thirty_day_total) {
    temp_display_string = temp_display_string + " Month:" + thirty_day_total;
  }
  if (display_wallet_value) {
    temp_display_string = temp_display_string + " Wallet:" + String(wallet_value,2);
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
    http.setTimeout(200);
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
}

void get_wallet_value() {
  Serial.println("Fetching new wallet value");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(1000);
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
}

void get_thirty_day_total() {
  Serial.println("Fetching new thiry day total");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
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
}

void get_oracle_price() {
  Serial.println("Fetching oracle price");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
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
}


void get_last_activity() {
  Serial.println("Fetching last activity");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    String temp_activity_hash = "";
    
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/activity?limit=1";
    Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request

    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"][0]["hash"] = true;
      filter["data"][0]["type"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      temp_activity_hash = String(doc["data"][0]["hash"]);
      if (temp_activity_hash != last_activity_hash){
        last_activity_hash = temp_activity_hash;
        Serial.print("Something happened:");
        //Serial.println(last_activity_hash);
        Serial.println(String(doc["data"][0]["type"]));
      }
      setEvent( get_last_activity, updateInterval() );

    } else {
      Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_last_activity, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
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

void srv_handle_fake_deposit() {
    happyDanceAnimation = true;
    last_wallet_deposit = 0.00246;
    server.send(200, "text/plain", "OK");
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

void display_rgbBitmap(uint8_t bmp_num, uint8_t x,uint8_t y) {
  fixdrawRGBBitmap(x, y, RGB_bmp[bmp_num], 8, 8);
  //matrix.show();
}


// Convert a BGR 4/4/4 bitmap to RGB 5/6/5 used by Adafruit_GFX
void fixdrawRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
  uint16_t RGB_bmp_fixed[w * h];
  for (uint16_t pixel = 0; pixel < w * h; pixel++) {
    uint8_t r, g, b;
    uint16_t color = pgm_read_word(bitmap + pixel);

    //Serial.print(color, HEX);
    b = (color & 0xF00) >> 8;
    g = (color & 0x0F0) >> 4;
    r = color & 0x00F;
    //Serial.print(" ");
    //Serial.print(b);
    //Serial.print("/");
    //Serial.print(g);
    //Serial.print("/");
    //Serial.print(r);
    //Serial.print(" -> ");
    // expand from 4/4/4 bits per color to 5/6/5
    b = map(b, 0, 15, 0, 31);
    g = map(g, 0, 15, 0, 63);
    r = map(r, 0, 15, 0, 31);
    //Serial.print(r);
    //Serial.print("/");
    //Serial.print(g);
    //Serial.print("/");
    //Serial.print(b);
    RGB_bmp_fixed[pixel] = (r << 11) + (g << 5) + b;
   // Serial.print(" -> ");
    //Serial.print(pixel);
    //Serial.print(" -> ");
    //Serial.println(RGB_bmp_fixed[pixel], HEX);
  }
  matrix.drawRGBBitmap(x, y, RGB_bmp_fixed, w, h);
}
