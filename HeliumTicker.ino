#include <arduino.h>
#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <WebServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#define ESP_RESET ESP.restart()
#else
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
//#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#define ESP_RESET ESP.reset()
#endif
#ifndef PSTR
#define PSTR // Make Arduino Due happy
#endif

// EEPROM Read/Write Library
#include <EEPROM.h>
// Web Dashboard. Uses ESP8266AsyncWebServer
#include <ESPDash.h>
// Adafruit GFX for WS2812B LED Matrix
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#include "FS.h"

// WiFi Password and Hotspot/Account info (not exactly sensitive, but personal)
#include "sensitive.h"

// OTA Libraries
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Mario BMP
#include "BMP.h"

// NTP Server time and events library
#include <ezTime.h>

// JSON Serialization/Deserialization
#include <ArduinoJson.h>

#define DEFAULT_COLOR 0xAA59AA
#define DEFAULT_BRIGHTNESS 128
#define DEFAULT_SPEED 1000
#define DEFAULT_MODE FX_MODE_STATIC
#define STEPS_PER_DISPLAY_UPDATE 1000


// EEPROM Locations
#define work_equivalent_EEPROM 1
#define display_wallet_EEPROM 2
#define display_daily_EEPROM 3
#define display_thirty_day_EEPROM 4
#define display_brightness_EEPROM 5
#define display_oracle_price_EEPROM 6
#define clock_mode_EEPROM 7
#define night_dim_EEPROM 8

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

const char* PARAM_MESSAGE = "message";

extern const char index_html[];
extern const char main_js[];

// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define bool_to_str(a) ((a)?("true"):("false"))

// Pin for photocell. Not implemented.
#define PHOTOCELL_PIN A0
// Pin for matrix
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
/* Start Webserver */
AsyncWebServer server(80);
/* Attach ESP-DASH to AsyncWebServer */
ESPDash dashboard(&server);


#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define DISPLAY_UPDATE_INTERVAL 100
int scroll_speed = 500;                 // Steps between scrolls. Lower = Faster
int scroll_pos = 0;                     // Incrementer

int photocellReading  = 0;

unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;
unsigned long last_display_update_time = 0;
unsigned long last_pos_update_time = 0;


// State variables for stats
bool display_work_equivalent = true;
bool display_wallet_value = true;
bool display_daily_total = true;
bool display_thirty_day_total = true;
bool display_oracle_price = true;
bool clockMode = false;
bool nightDim = true;

// Stats
float daily_total = 0;
float wallet_value = 0;
float previous_wallet_value = 0;
float thirty_day_total = 0;
float witnesses = 0;
float oracle_price = 0;
float last_wallet_deposit = 0;

String last_activity_hash = "";

byte display_brightness = 20;
int animationCounter = 0;
boolean happyDanceAnimation = false;
boolean initializedWalletValue = false;
String display_string;

Timezone Omaha;
/*
  Button Card
  Format - (Dashboard Instance, Card Type, Card Name)
*/
Card oracle_price_card(&dashboard, BUTTON_CARD, "Show Oracle Price");
Card work_equivalent_card(&dashboard, BUTTON_CARD, "Show Work Equivalent");
Card daily_total_card(&dashboard, BUTTON_CARD, "Show 24hr Total");
Card thirty_day_total_card(&dashboard, BUTTON_CARD, "Show 30 Day Total");
Card wallet_value_card(&dashboard, BUTTON_CARD, "Show Wallet Value");
Card clock_mode_card(&dashboard, BUTTON_CARD, "Clock Mode");
Card night_dim_card(&dashboard, BUTTON_CARD, "Night Dim");
/*
  Slider Card
  Format - (Dashboard Instance, Card Type, Card Name, Card Symbol(optional), int min, int max)
*/
Card brightness_card(&dashboard, SLIDER_CARD, "Brightness", "", 0, 255);
//File colorpicker = SPIFFS.open("/tinycolorpicker.js", "r");
//File indexhtml = SPIFFS.open("/index.html", "r");

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(2000);
  matrix.begin();
  matrix.show();
  matrix.setTextWrap(false);
  matrix.setBrightness(5); // Low power to ensure the ESP gets enough for its power hungry WiFi connection
  matrix.setTextColor(matrix.Color(200, 200, 255));
  matrix.setCursor(0, 0);
  matrix.clear();
  matrix.print("Starting...");
  matrix.show();

  //Serial.println("WS2812FX setup");

  while (!SPIFFS.begin()) {
    scrollInfoText("SPIFFS Error");
  }

  //Serial.println("Wifi setup");
  wifi_setup();

  matrix.print("WiFi Good");
  //Serial.println("HTTP server setup");
  //server.on("/", srv_handle_index_html);
  //server.on("/", HTTP_GET, srv_handle_index_html);
  //server.on("/set", srv_handle_set);
  //server.on("/getStat", srv_handle_get_stat);
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String message;
    if (request->hasParam(PARAM_MESSAGE)) {
      message = request->getParam(PARAM_MESSAGE)->value();
    } else {
      message = "No message sent";
    }
    request->send(200, "text/plain", "Hello, GET: " + message);
  });

  // Send a POST request to <IP>/post with a form field message set to <message>
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest * request) {
    String message;
    if (request->hasParam(PARAM_MESSAGE, true)) {
      message = request->getParam(PARAM_MESSAGE, true)->value();
    } else {
      message = "No message sent";
    }
    request->send(200, "text/plain", "Hello, POST: " + message);
  });
  server.on("/fakeDeposit", srv_handle_fake_deposit);
  server.onNotFound(notFound);
  server.begin();
  //Serial.println("HTTP server started.");

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  OTA_Setup();

  // Set NTP polling frequency (seconds)
  setInterval(60 * 60);
  //setDebug(INFO);

  // Watis for NTP server to be synced
  waitForSync();

  //Serial.println("UTC: " + UTC.dateTime());

  // Tell the NTP server where we are, and make it the default timezone.
  Omaha.setLocation("America/Chicago");
  Omaha.setDefault();
  //Serial.println("Omaha time: " + Omaha.dateTime());

  //timeClient.begin();
  scrollInfoText("EEPROM");
  EEPROM.begin(10);                     // Initialize EEPROM (or EEPROM emulation on the ESP8266)
  EEPROM_read();                        // Read stored values
  scrollInfoText("GOOD");
  setUpDashboard();                     // Set up dashboard callbacks

  //Serial.println("getting data");
  matrix.setBrightness(display_brightness);
  check_wifi();                         // Ensure WiFi is connected (it should be, we just started it), and set up event to check continuosly
  scrollInfoText("Loading...");
  get_daily_total();                    // This will ensure we have some data, and then set up the event to continuously update
  get_wallet_value();                   // the values from the API
  get_thirty_day_total();
  get_oracle_price();
  //get_last_activity();

  scroll_text();                        // Update the display string and slide it over if its time
  //adjustBrightness();                   // adjust brightness based on light (NOT IMPLEMENTED)
  update_display();                     // Blit the display
  //setEvent( get_daily_total,updateInterval() );
  //setEvent(adjustBrightness, nightAndDayTime());
  //Serial.println("ready!");
}

time_t nightAndDayTime() {
  return Omaha.now() + 10000;
}

void updateSunriseSunset() {

}


// Callbacks for dashboard cards
void setUpDashboard() {
  /* Attach Button Callback */
  oracle_price_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_oracle_price = value;
    EEPROM.put(display_oracle_price_EEPROM, display_oracle_price);
    oracle_price_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  work_equivalent_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_work_equivalent = value;
    EEPROM.write(work_equivalent_EEPROM, display_work_equivalent);
    work_equivalent_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  daily_total_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_daily_total = value;
    EEPROM.write(display_daily_EEPROM, display_daily_total);
    daily_total_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  thirty_day_total_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_thirty_day_total = value;
    EEPROM.write(display_thirty_day_EEPROM, display_thirty_day_total);
    thirty_day_total_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  wallet_value_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_wallet_value = value;
    EEPROM.write(display_wallet_EEPROM, display_wallet_value);
    wallet_value_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });


  brightness_card.attachCallback([&](int value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    display_brightness = value;
    EEPROM.write(display_brightness_EEPROM, display_brightness);
    matrix.setBrightness(display_brightness);
    brightness_card.update(display_brightness);
    dashboard.sendUpdates();
    EEPROM.commit();
  });


  clock_mode_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    clockMode = value;
    EEPROM.write(clock_mode_EEPROM, clockMode);
    clock_mode_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  night_dim_card.attachCallback([&](bool value) {
    /* Print our new button value received from dashboard */
    Serial.println("Button Triggered: " + String((value) ? "true" : "false"));
    /* Make sure we update our button's value and send update to dashboard */
    nightDim = value;
    EEPROM.write(night_dim_EEPROM, nightDim);
    night_dim_card.update(value);
    dashboard.sendUpdates();
    EEPROM.commit();
  });

  // Update the state of the web display with our current state
  work_equivalent_card.update(display_work_equivalent);
  oracle_price_card.update(display_oracle_price);
  wallet_value_card.update(display_wallet_value);
  thirty_day_total_card.update(display_thirty_day_total);
  daily_total_card.update(display_daily_total);
  brightness_card.update(display_brightness);
  clock_mode_card.update(clockMode);
}

// Used to set the interval that data is refreshed
time_t updateInterval() {

  time_t event_time = Omaha.now() + (20 * (60)); // x*(60) = x minutes between updates
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}

// Used to set the interval that failed data retrieval is retried
time_t retryUpdateInterval() {

  time_t event_time = Omaha.now() + 20; // Retry in 20 seconds
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}

// Used to set the interval that the lights are sampled. NOT IMPLEMENTED
time_t lightsReadingInterval() {
  time_t event_time = Omaha.now() + (5 * (60)); // x*(60) = x minutes between updates
  //Serial.println(event_time);
  //Serial.println(Omaha.dateTime(event_time));
  return event_time;
}

// Read in the saved state from EEPROM
void EEPROM_read() {
  EEPROM.get(work_equivalent_EEPROM, display_work_equivalent);
  EEPROM.get(display_oracle_price_EEPROM, display_oracle_price);
  EEPROM.get(display_wallet_EEPROM, display_wallet_value);
  EEPROM.get(display_thirty_day_EEPROM, display_thirty_day_total);
  EEPROM.get(display_daily_EEPROM, display_daily_total);
  EEPROM.get(display_brightness_EEPROM, display_brightness);
  EEPROM.get(clock_mode_EEPROM, clockMode);
}

/*
   Connect to WiFi. If no connection is made within WIFI_TIMEOUT, ESP gets reset.
*/
void wifi_setup() {
  //Serial.println();
  //Serial.print("Connecting to ");
  //Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#ifdef STATIC_IP
  WiFi.config(ip, gateway, subnet);
#endif

  String infoString = "Connecting to ";
  infoString += WIFI_SSID;
  unsigned long connect_start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    //Serial.print(".");
    scrollInfoText(infoString);
    if (millis() - connect_start > WIFI_TIMEOUT) {
      //Serial.println();
      //Serial.print("Tried ");
      //Serial.print(WIFI_TIMEOUT);
      //Serial.print("ms. Resetting ESP now.");
      ESP_RESET;
    }
  }

  //Serial.println("");
  //Serial.println("WiFi connected");
  //Serial.print("IP address: ");
  //Serial.println(WiFi.localIP());
  //Serial.println();
}

// Scrolls text across the matrix. Call continuously and it will manage the speed
void scrollInfoText(String text) {
  static int infoScrollPos = 0;
  matrix.clear();
  matrix.setCursor(infoScrollPos, 0);
  matrix.print(text);
  matrix.show();
  infoScrollPos--;
  int strLen = text.length() * -1 * 6;
  //Serial.print("scrollPos/strLen:");
  //  Serial.println(infoScrollPos);

  //  Serial.println(strLen);

  if (infoScrollPos < strLen) {
    infoScrollPos = 0;
  }

}

uint16_t firstPixelHue = 0;
byte wheel_pos = 0;
int time_stagger = 2;
int time_stagger_counter = 0;

void draw_rainbow_line() {
  //firstPixelHue += 64; // Advance just a little along the color wheel
  wheel_pos++;
  //matrix.show();
  for (int i = 0; i < MATRIX_WIDTH; i++) { // For each pixel in row...
    //int pixelHue = firstPixelHue + (i * 64*8);
    //matrix.drawPixel(i,MATRIX_HEIGHT -1,matrix.gamma32(matrix.ColorHSV(pixelHue,255,255)));
    matrix.drawPixel(i, MATRIX_HEIGHT - 1, wheel(wheel_pos + i));
  }
}

uint32_t wheel(byte wheelPos) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) {
    return matrix.Color(255 - wheelPos * 3, 0, wheelPos * 3);
  }
  if (wheelPos < 170) {
    wheelPos -= 85;
    return matrix.Color(0, wheelPos * 3, 255 - wheelPos * 3);
  }
  wheelPos -= 170;
  return matrix.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
}

void loop() {
  //timeClient.update();
  //Serial.println(Omaha.dateTime(ISO8601));
  events();
  ArduinoOTA.handle();
  yield();
  // put your main code here, to run repeatedly:
  unsigned long now_ms = millis();
  yield();
  if (happyDanceAnimation) {
    //TODO: Fancy animation because we made money
    deposit_animation();
    animationCounter++;
  } else {
    if (previous_wallet_value < wallet_value) { // If we made money, maybe....
      if (initializedWalletValue) { // unless we just initialized our wallet value. If we have already loaded data...
        last_wallet_deposit = wallet_value - previous_wallet_value; // Calculate the amount of the deposit
        previous_wallet_value = wallet_value;                       // Update values
        //Serial.println("Starting Happy Dance animation");
        //Serial.println(previous_wallet_value);
        //Serial.println(wallet_value);
        //Serial.print("We made: ");
        //Serial.println(last_wallet_deposit);
        happyDanceAnimation = true;                                 // Make it dance!
      }
    } else {
      //matrix.clear();
      //draw_rainbow_line();
      if (now_ms - last_display_update_time > DISPLAY_UPDATE_INTERVAL) {
        update_display();
        last_display_update_time = now_ms;
      }
      if (now_ms - last_pos_update_time > scroll_speed) {
        scroll_text();
        last_pos_update_time = now_ms;
      }
      yield();
    }
  }
  //  if (display_clock % 600 == 0) {
  //    get_daily_total();
  //  }
  //delay(1000);
  matrix.show(); // Show our new display
}

int sprite = 0;
void adjustBrightness() {
  //photocellReading = analogRead(PHOTOCELL_PIN);
  //Serial.println("Adjusting brightness");
  photocellReading += 10;
  if (photocellReading > 1023) photocellReading = 0;
  int newBrightness = map((1023 - photocellReading), 0, 1023, 3, 10);
  //Serial.println(newBrightness);
  matrix.setBrightness(newBrightness);
  setEvent(adjustBrightness, lightsReadingInterval());
}

#define danceLoops 5

// animationCounter is an incrementer to keep track of ticks so we can set the time of our animation frames
// Each loop counts to animationCounter and then changes to the next sprite
void deposit_animation() {
  //Serial.println("Updating display");
  matrix.clear();
  display_rgbBitmap(sprite % 3, 0, 0);
  matrix.setCursor(9, 0);
  float hntVal = last_wallet_deposit * oracle_price;
  String deposit_string = "We made " + String(last_wallet_deposit, 6) + " HNT (worth $" + String(hntVal, 2) + ") ";
  int pos = sprite % deposit_string.length(); // Keep incrementing along the length of the string and wrap what is to the right of the pos back around
  if (pos > 0) {
    deposit_string = deposit_string.substring(pos) + deposit_string.substring(0, pos - 1);
  }
  matrix.print(deposit_string); // Display the string
  matrix.show();
  if (animationCounter == 30) { // 30 ticks per frame
    animationCounter = 0; // reset the animation counter
    sprite++; // move to the next sprite
    if (sprite > danceLoops * int(deposit_string.length() / 2)) { // Subtract a couple ticks so it doesn't last quite as long.
      sprite = 0;
      happyDanceAnimation = false;
    }
  }
  yield();
}
//setEvent(update_display,Omaha.now() + DISPLAY_UPDATE_INTERVAL);


void check_wifi() {
  //Serial.print("Checking WiFi... ");
  if (WiFi.status() != WL_CONNECTED) {
    //Serial.println("WiFi connection lost. Reconnecting...");
    wifi_setup();
  } else {
    //Serial.println("OK");
  }
  setEvent(check_wifi, Omaha.now() + WIFI_TIMEOUT);
}

void update_display() {
  //Serial.println("Updating display");
  matrix.setCursor(0, 0);
  matrix.clear();
  draw_rainbow_line();              /// Rainbow line under the display string
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
  String temp_display_string = " ";
  if (display_daily_total) {
    temp_display_string = temp_display_string + "24Hrs:" + daily_total;
  }
  if (display_thirty_day_total) {
    temp_display_string = temp_display_string + " Month:" + thirty_day_total;
  }
  if (display_wallet_value) {
    temp_display_string = temp_display_string + " Wallet:" + String(wallet_value, 2);
  }
  yield();
  if (display_oracle_price) {
    temp_display_string = temp_display_string + " HNT Value:$" + oracle_price;
  }
  if (display_work_equivalent) {
    temp_display_string = temp_display_string + " 40hrs/wk:$" + String(thirty_day_total / 160 * oracle_price, 2) + "/hr";
  }
  if (temp_display_string == " " || clockMode) {
    temp_display_string = Omaha.dateTime("l ~t~h~e jS ~o~f F Y, g:i A ");
  }
  int pos = disp_clock % temp_display_string.length();

  if (pos > 0) {
    temp_display_string = temp_display_string.substring(pos) + temp_display_string.substring(0, pos - 1);
  }

  //display_string += " ";

  //Serial.println(display_string);
  return temp_display_string;

}

void get_daily_total() {
  //Serial.println("Fetching new daily total");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!
    http.setTimeout(200);
    client.setInsecure(); // this is the magical line that makes everything work
    time_t day_ago = Omaha.now() - 86400;
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/rewards/sum?max_time=" + Omaha.dateTime(ISO8601) + "&min_time=" + Omaha.dateTime(day_ago, ISO8601);
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> da_filter;
      da_filter["data"]["total"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(da_filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      daily_total = doc["data"]["total"];
      //Serial.print("Daily total updated: ");
      //Serial.println(daily_total);             //Print the response payload
      setEvent( get_daily_total, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_daily_total, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}

void get_wallet_value() {
  //Serial.println("Fetching new wallet value");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(1000);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/accounts/" + ACCOUNT_ADDRESS;
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"]["balance"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      if (initializedWalletValue) {
        previous_wallet_value = wallet_value;
      }
      wallet_value = doc["data"]["balance"];
      wallet_value /= 100000000;
      if (!initializedWalletValue) {
        previous_wallet_value = wallet_value;
      }

      initializedWalletValue = true;

      //Serial.print("Wallet Value: ");
      //Serial.println(wallet_value);             //Print the response payload
      setEvent( get_wallet_value, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_wallet_value, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}

void get_thirty_day_total() {
  //Serial.println("Fetching new thiry day total");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    time_t month_ago = Omaha.now() - 86400 * 30;
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/rewards/sum?max_time=" + Omaha.dateTime(ISO8601) + "&min_time=" + Omaha.dateTime(month_ago, ISO8601);
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
    if (httpCode > 0) { //Check the returning code

      String payload = http.getString();   //Get the request response payload

      StaticJsonDocument<200> filter;
      filter["data"]["total"] = true;

      StaticJsonDocument<400> doc;
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));

      // Print the result
      //serializeJsonPretty(doc, Serial);
      thirty_day_total = doc["data"]["total"];
      //Serial.print("30 Day Total: ");
      //Serial.println(thirty_day_total);             //Print the response payload
      setEvent( get_thirty_day_total, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_thirty_day_total, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}

void get_oracle_price() {
  //Serial.println("Fetching oracle price");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/oracle/prices/current";
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
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
      //Serial.print("Oracle Price: ");
      //Serial.println(oracle_price);             //Print the response payload
      setEvent( get_oracle_price, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_oracle_price, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}


void get_binance_price() {
  //Serial.println("Fetching oracle price");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "/api/v3/ticker/price?symbol=HNT";
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
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
      //Serial.print("Oracle Price: ");
      //Serial.println(oracle_price);             //Print the response payload
      setEvent( get_oracle_price, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_oracle_price, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}

void get_last_activity() {
  //Serial.println("Fetching last activity");
  if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    String temp_activity_hash = "";

    WiFiClientSecure client;
    HTTPClient http;  //Declare an object of class HTTPClient
    http.setTimeout(200);
    const int httpPort = 443; // 80 is for HTTP / 443 is for HTTPS!

    client.setInsecure(); // this is the magical line that makes everything work
    String query = "https://api.helium.io/v1/hotspots/" + HOTSPOT_ADDRESS + "/activity?limit=1";
    //Serial.println(query);
    http.begin(client, query); //Specify request destination
    int httpCode = http.GET();                                  //Send the request
    yield();
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
      if (temp_activity_hash != last_activity_hash) {
        last_activity_hash = temp_activity_hash;
        //Serial.print("Something happened:");
        ////Serial.println(last_activity_hash);
        //Serial.println(String(doc["data"][0]["type"]));
      }
      setEvent( get_last_activity, updateInterval() );

    } else {
      //Serial.println("Failed. Retrying in 20 seconds");
      setEvent( get_last_activity, retryUpdateInterval() );

    }

    http.end();   //Close connection
  }
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void srv_handle_index_html(AsyncWebServerRequest * request) {
  request->send(200, "text/html", index_html);
}

void srv_handle_fake_deposit(AsyncWebServerRequest * request) {
  happyDanceAnimation = true;
  last_wallet_deposit = 0.00246;
  request->send(200, "text/plain", "OK");
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
    //Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      //Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      //Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      //Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      //Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      //Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void display_rgbBitmap(uint8_t bmp_num, uint8_t x, uint8_t y) {
  fixdrawRGBBitmap(x, y, RGB_bmp[bmp_num], 8, 8);
  //matrix.show();
}

// Convert a BGR 4/4/4 bitmap to RGB 5/6/5 used by Adafruit_GFX
void fixdrawRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
  uint16_t RGB_bmp_fixed[w * h];
  for (uint16_t pixel = 0; pixel < w * h; pixel++) {
    yield();
    uint8_t r, g, b;
    uint16_t color = pgm_read_word(bitmap + pixel);

    ////Serial.print(color, HEX);
    b = (color & 0xF00) >> 8;
    g = (color & 0x0F0) >> 4;
    r = color & 0x00F;
    ////Serial.print(" ");
    ////Serial.print(b);
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
