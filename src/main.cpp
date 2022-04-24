/*
 FermWatch - a colourful display for displaying BrewPiLess and Brewfather fermentation data or "No phone no worries!"
 Version 
 1.0.3 try to fix change to set false F and P on reset try R Bacon timing in loop works
 1.0.4 still F & P on reset now has force config every time! and now using LittleFS
 1.0.5 optional temperature correction for iSpindel Temperature gravity and thus calulated ABV
 1.0.6 apparent attenuation calc and AEST offset 36000 secs general graphics tidy up app att can use temp corrected present gravity
 1.0.7 moved OG and EstFG as fixed data and swopped with App Att  Added warning for deserialization errors brew and batch - Auth entry errors. Perhaps add warning re time to manual spund - problem how to acknowledge perhaps touch in future? 
 1.0.8 Interpolation for experimental temperature correction implemented. Note online formulae do not seem to calculate  to match Wheeler's table or scientific calcs.
 1.0.9 Try different font for  startup  AA font trialled for Main screen and brew details looks good but causes instability in the 8266. Occasionally double restarts
 1.1 First Version  -  sanitised and removed AA fonts

FermWatch uses data pushed from BPL every 75 - 115 seconds and data pulled from the Brewfather API every 180 seconds to rotate displays of batch fementation status.
No need for phones browsers etc an always on indication of how the brew is fermenting.

 Using a D1 Mini with 2.8" TFT ILI9341 320 * 240 (in landscape mode), with touchscreen, and the Bodmer TFT_eSPI library. Plus the uMQTTBroker for Arduino (C++-style) on an ESP8266 (D1Mini clone)
 The aim is to use the MQTT facility of BrewPiLess (BPL), sending a JSON message,  to display the status information locally to the unit, instead of using the small SSD1306 OLED display.
 This approach should enable this program to be independent of and compatible with future updates of BPL.
 Note BPL set to push an update every 75 -115 seconds
 
 Also to report iSpindel gravity and temperature, via the Brewfather API 
 The aim here is to display the in progress fermentation details from Brewfather. This should get the batch name, Est FG and OG and thus permit a calculation of ABV. 
 BF updates the information from the iSpindel at 15 minute intervals so can set the scan delay to say 10 minutes (3 for testing).
  
 Future improvements could be to use the touch screen of the ILI9341 
 * to select screen display Summary, BPL, iSpindel
 * change settings in lieu of the BPL rotary switch?
 * be able to change FermWatch config settings (F/C, SG/P, Temp Correction) rather than restarting 
 * use the on board ILI9341 SD card facility to hold fonts etc
 * Migrate to ESP32 to get more memory for screen processing and use  AA fonts for cleaner screen presentation
 * report iSpindel gravity and temperature, possibly via iSpindHub
 
 * The uMQTTBroker program WORKS with 8266 ONLY!  and defines a custom broker class with callbacks, starts it, subscribes locally to anything, and publishs a topic every second.
 * Try to connect from a remote client and publish something - the console will show this as well.
 
 
Hardware connections

TFT 9431 pins    | D1 Mini NodeMCU pins  |   wire colour  |
LCD VCC            Vin / 3V3             |   Red
LCD GND            GND                   |   Orange
LCD CS             D8  .                 |   Yellow
LCD RESET          D4  .                 |   Green
LCD DC             D3  .                 |   Blue
LCD SDI/MOSI       D7  .                 |   Purple
LCD SCK            D5  .                 |   Grey
LCD LED            3V3                   |   White   Jumper
LCD SDO/MISO       D6                    |   Black   (or leave disconnected if not reading TFT)
LCD T_CLK                                   Jumper to D5

Future
T_CS               D2                    |           Chip select pin (T_CS) of touch screen
T_DIN                                    |           Jumper to D7
T_DO                                     |           Jumper to D6
T_IRQ                                    |  no connection?  */

// for wm
// removed for now #define FORMAT_SPIFFS_IF_FAILED false //try this seemed to get rid of the load error   - we cannot mount the SPIFFS disk, shall we format one? Not sure why I put this in

#define ESP_DRD_USE_LittleFS true // this line needs to go before drd.h      Need to be defined before library import
#define FORMAT_LittleFS_IF_FAILED false

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> // for BF API
#include "uMQTTBroker.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "NotoSansBold15.h"
#include "NotoSansBold36.h"
#include "NotoSansMonoSCB20.h"

//Anti Aliassed fonts not used
// Do not include "" around the array name!
// #define AA_FONT_SMALL NotoSansBold15
// #define AA_FONT_LARGE NotoSansBold36
// #define AA_FONT_MONO  NotoSansMonoSCB20 // NotoSansMono-SemiCondensedBold 20pt

#include <TFT_eSPI.h>
#include <SPI.h>
#include "font.h" // Orb 18
#include "MultiMap.h"
// note free fonts need to be after TFT_eSPI.h
#include "Orbitron_Medium_20.h"
#include "NotoSans_Medium20pt7b.h"
#include "Open_Sans_Bold_18.h" // http://oleddisplay.squix.ch/#/home
#include "Open_Sans_Bold_16.h"

#include <FS.h>
#include <LITTLEFS.h> // added

// 
// only for ESP32
// #include <SPIFFS.h> 
// #include <WiFi.h>
// ---------------------------------------------------------------------------------------------------------
// debugger https://github.com/RalphBacon/224-Superior-Serial.print-statements/tree/main/Simple_Example
#define DEBUG 1

#if DEBUG == 1
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define debugf(x) Serial.printf(x) // throws errors not used
#else
#define debug(x)
#define debugln(x)
#define debugf(x)
#endif

// #define LED_BUILTIN 2 use with 8266

// ----------------------------WiFiManager and DRD stuff
#include <WiFiManager.h>
#include <ESP_DoubleResetDetector.h> // Can be installed from the library manager (Search for "ESP_DoubleResetDetector") //https://github.com/khoih-prog/ESP_DoubleResetDetector
// DRD double reset detect https://github.com/khoih-prog/ESP_DoubleResetDetector/blob/master/examples/minimal/minimal.ino
// Note DRD not used - just single press reset

#include <ArduinoJson.h>
// Number of seconds after reset during which a subseqent reset will be considered a double reset. Was 10 changed to 5 needs to be a deliberate gap between presses
#define DRD_TIMEOUT 5

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

// #define LED_OFF     HIGH  // was for ESP32 const int PIN_LED = 2; // blue light on if connected
// pinMode(LED_BUILTIN, OUTPUT); // use with Led builtin 8266

// JSON configuration file
#define JSON_CONFIG_FILE "/sample_config.json"


DoubleResetDetector *drd;

//flag for saving data
bool shouldSaveConfig = false;

// -----------------------Default configuration values   this section is the WiFi Manager Parameter stuff------
// WiFi Manager Parameter - Global variables
char Auth_B[126] = ("Insert your Base64 encoded - Authorisation Basic - 125 char"); // length should be max size + 1 =126

int UTC_offset = 36000; // AEDT need to adjust for daylight saving time 39600 for AEST = 36000
int BF_updateInt = 180000; // milli seconds set for 3 minutes  150 requests/h = 2.5 per minute set minimum time 30 seconds ie 30000
bool Plato = false; // false to leave checkbox unchecked
bool Fahr = false;  // and leave the defaults as C and SG
bool Temp_Corr = false; // default no correction for temperature

// Define WiFiManager Object // from robot
WiFiManager wm;

// bool writeConfigFile()
void saveConfigFile()
{
  debugln(F("Saving config to json"));
  StaticJsonDocument<640> json; // made 640
  json["Auth_B"] = Auth_B;
  json["UTC_offset"] = UTC_offset;
  json["BF_updateInt"] = BF_updateInt;
  json["Plato"] = Plato;//  
  json["Fahr"] = Fahr; // 
  json["Temp_Corr"] = Temp_Corr; 
  
  // Open config file
    File configFile = LittleFS.open(JSON_CONFIG_FILE, "w");
    if (!configFile)
    {
      debugln("failed to open config file for writing");
    }

    serializeJsonPretty(json, Serial);
    if (serializeJson(json, configFile) == 0)
    {
      debugln(F("Failed to write to file"));
    }
  configFile.close();
  }

  bool loadConfigFile()
  // Load existing configuration file
  {
    //clean FS, for testing 
    // LittleFS.format();

    //read configuration from FS json
    debugln("mounting File System ...");

    
  LittleFS.begin(); // for 8266
  // SPIFFS.begin(); 
   // 
    Serial.println("Mounted file system");
    if (LittleFS.exists(JSON_CONFIG_FILE))
    {
      //file exists, reading and loading
      Serial.println("Reading config file");
      File configFile = LittleFS.open(JSON_CONFIG_FILE, "r");
      if (configFile)
      {
        Serial.println("Opened config file");
        StaticJsonDocument<640> json;
        DeserializationError error = deserializeJson(json, configFile);
        serializeJsonPretty(json, Serial);
        if (!error)
        {
          Serial.println("\nparsed json");

          strcpy(Auth_B, json["Auth_B"]);
              UTC_offset =json["UTC_offset"].as<int>();
              BF_updateInt = json["BF_updateInt"].as<int>();
              Plato = json["Plato"].as<bool>();
              Fahr = json["Fahr"].as<bool>();
              Temp_Corr = json["Temp_Corr"].as<bool>();
            Serial.println("Deserialized true false of Fahr "); Serial.println(Fahr);
          return true;
        }
        else
        {
          Serial.println("failed to load json config");
        }
      }
    }
  // }
  // }
  else
  {
    // Error mounting file system
    Serial.println("Failed to mount FS");
  }
  //end read
  return false;
}
//callback notifying us of the need to save config in the right place?
void saveConfigCallback()
// Callback notifying us of the need to save configuration
{
  debugln("Yes, save config");
  shouldSaveConfig = true;
}

// This gets called when the config mode is launched, might be useful to update a display with this info.
void configModeCallback(WiFiManager *myWiFiManager)
{
  debugln("Entered Conf Mode");

  debug("Config SSID: ");
  debugln(myWiFiManager->getConfigPortalSSID());

  debug("Config IP Address: ");
  debugln(WiFi.softAPIP());
}


// ------------------------------------------------------------------------------------------------------------
/* Timing for LOOP functions NTP and accessing BF and screen rotation  BPL set for 75 secs
  "independant" timed events  https://www.programmingelectronics.com/arduino-millis-multiple-things/   */
const long eventTime_1_NTP = 1000; //in ms 1 sec
const long eventTime_2_BF = 180000; //in ms  1 min 1000 * 60  = 60000 180 = 3 mins note max 150 requests per hour changed to 40000 for testing
const long eventTime_3_Screen = 5000; // 5 secs future?
/* When did they start the race? */
unsigned long previousTime_1 = 0;
unsigned long previousTime_2 = 0;
unsigned long previousTime_3 = 0; // spare

void updateNTP();

// ref sprintf https://www.programmingelectronics.com/sprintf-arduino/ although Espressif has this builtin puts in flash not RAM

// -----------------------------------------------------------------------------------------------------------------------
// BF HTTPS requests First set up authorisation
// login details to BF API

WiFiClientSecure client; // secure


// ---------------------------------------------------------------------------------------------------------------------------------
// Global Variables to save batch id and OG
String version ="1.1";
// const char* batch_id;  // go back to const char
char batch_id[30];// this works 29 char +1 [30] batch id now 30 22 Mar so set for 31
const char* brew; // removed [29] try 30 Lush is 29 charseems to work 27 char +1 works with 26 char and 28 char bombs out font 1 25 char max to fit screen
float SGogy; // try float measured OG from BF
float FGEst; // estimated final gravity from BF
float abv;// before adjustment is Init_abv local
int ABV_adjG; // gravity adjustment for ABV calc
float adjABV;  // for printing
float SG_adjT; // gravity adjustment for temperature correction

// iSpindel GLOBAL Variables ex Brewfather to save  angle, id, manual/BPL entry pressure, temp, sg
// float iSp_angle; // current angle 
char* iSp_id; //iSpindel name
float pressure; // manual from BF at the moment
// float iSp_temp;  // iSpindel 'aux' temperature
float iSp_sg; // present SG
float iSp_sga; // temperature 20C adjusted present SG
float Itemp; // unadjusted iSpindel temperature
// float last_angle; // store the last angle to be able to compare in future?
float App_Att;
int batch_No;
int page_counter = 1;

// screen stuff ------------------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
// Callback function to provide the pixel color at x,y //AA
// uint16_t pixelColor(uint16_t x, uint16_t y) { return tft.readPixel(x, y); } // AA fonts

// http://www.barth-dev.de/online/rgb565-color-picker/
#define TFT_GREY 0x5AEB
#define lightblue 0x647B
#define pinkish 0xEBCA
#define darkred 0xA041
#define skyblue 0x373E
#define blue 0x5D9B
#define bronze  0xE5EC
#define yeast 0xD676


int16_t Backgnd = TFT_GOLD;
int16_t InnerBac = TFT_BLACK;
int16_t WaitHeat = pinkish;
int16_t Heating = TFT_RED;
int16_t WaitCool = skyblue;
int16_t Cooling = TFT_BLUE;
int16_t WaitPeak = TFT_BROWN;
int32_t Idle = TFT_GREY;

int lastState; // global variable

String BPLIP;

// https://github.com/martin-ger/uMQTTBroker/blob/master/examples/uMQTTBrokerSampleOOFull/uMQTTBrokerSampleOOFull.ino 
//

// Get NTP Time
const long utcOffsetInSeconds = UTC_offset; // AEDT need to reenter offset for daylight saving time DST
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// MQTT Broker
//  Custom broker class with overwritten callback functions - whatever that means
// 
class myMQTTBroker: public uMQTTBroker
{
public:
    virtual bool onConnect(IPAddress addr, uint16_t client_count) {
      Serial.println(addr.toString()+" BPL connected");
      BPLIP=(addr.toString());
      return true;
    }
    
    virtual bool onAuth(String username, String password) {
      Serial.println("BPL  Username/Password: "+ username + "/" + password);
      return true;
    }
    
    virtual void onData(String topic, const char *data, uint32_t length) 
  {
      char data_str[length+1];
      os_memcpy(data_str, data, length);
      data_str[length] = '\0';

      // uncomment to Print BPL for debugging  
      // debugln((String)data_str);
      // debugln("received topic '"+topic+"' with data '"+(String)data_str+"'");
      debugln("");
      debugln("This is FermWatch VERSION " + version);
      debugln("");

       //if topic BPL
       //  https://arduinojson.org/v6/assistant/

        StaticJsonDocument<256> bpl;// was 256
        DeserializationError error = deserializeJson(bpl, (String)data_str);
          if (error) {
          debug(F("BPL deserializeJson() failed: "));
          debugln(error.f_str());
          return;
          }
        
        // error trap and message if BF batch_id not decoded
        String batch_ln = String(batch_id); 
        if (batch_ln.length() == 0){
        debug("No Batch ID received -  ");
        debugln("Brewfather not connected");
        tft.fillRect(4,155,310,62,TFT_BLACK);
        tft.setCursor(60,180);
        tft.setTextFont(1);
        tft.setTextColor(TFT_RED,TFT_BLACK);
        tft.print("BF NOT CONNECTED");
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        // return;
        }
      
        
        // PAGES
        //Counter to change positions of pages https://www.instructables.com/Arduino-LCD-16x2-Turn-Single-Screen-Into-Multiple-/
        //------- Switch function between pages---// replace with touch?
        switch (page_counter) {
        case 1:{     //BPL Page 1
       
        // BPL STATUS     ideally this would show on the Summary and iSpindel screens as well
        
        int state = bpl["state"]; // 4
        debug("Status : ");
        // Status indicated by the colour of the border
        // Avoid the whole screen refreshing unnecessarily when status has not changed  - not a problem with the alternating screens still startup issue with yellow border!
        // debug
        
        Serial.printf("Last State %i  Current State %i \n", lastState, state);
        if (state == 0); // try this think it is working OK perhaps not!
        tft.fillRoundRect(4,4,312, 232, 2, InnerBac);// clear startup/last screen need to do only once on startup as it invalidates the selections below
        // if (lastState != state && state==0);
        // if new state not equal to old state (lastState) and State =  change the screen or if last state the same - maintain colour
        
        if ((lastState != state && state == 0) || lastState == 0){
          {debugln("Idling  "); tft.fillRoundRect(0,0,320,240,2, Idle);tft.fillRoundRect(4,4,312,232,2, InnerBac); }}
        // if (state == 0 ) {debugln("Idling"); tft.fillScreen(Idle);tft.fillRoundRect(4,4,312,232,2, InnerBac); }
        
        if ((lastState != state && state == 1) || lastState == 1 ){
          { debugln("OFF");tft.fillScreen(TFT_SILVER);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        
        if ((lastState != state && state == 2) || lastState == 2){
          { debugln("Door Open");tft.fillScreen(Idle);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        
        if ((lastState != state && state == 3) || lastState == 3){
          { debugln("Heating  ");tft.fillScreen(Heating);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        // if (state == 4)  { debugln("Cooling");tft.fillScreen(Cooling);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}
        if ((lastState != state && state == 4) || lastState == 4){
          { debugln("Cooling  ");tft.fillRoundRect(0,0,320,240,2, Cooling);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        
        if ((lastState != state && state == 5) || lastState == 5){
          { debugln("Waiting to Cool  ");tft.fillScreen(WaitCool);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        
        if ((lastState != state && state == 6) || lastState == 6){
          { debugln("Waiting to Heat  ");tft.fillScreen(WaitHeat);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
        
        if ((lastState != state && state == 7) || lastState == 7){
           { debugln("Waiting for peak  ");tft.fillScreen(WaitPeak);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}

        if ((lastState != state && state == 8) || lastState == 8){ 
           { debugln("Cooling Time");tft.fillScreen(Idle);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}

        if ((lastState != state && state == 9) || lastState == 9){   
           { debugln("Heating Time");tft.fillScreen(Idle);tft.fillRoundRect(4,4,312, 232, 2, InnerBac);}}
         lastState = state; 
        // }
                
        // Fixed Text
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        tft.setCursor(40,40);
        tft.println("Room");
        tft.setCursor(170,40);
        tft.println("Beer");
        
        tft.setFreeFont(&Orbitron_Medium_18);
        tft.fillRect(10,55,245,38,TFT_BLACK); // yes  needs to be behind custom text moved here to get rid of flicker
        
        // Conversions F to C
        // float TempC;
        //  // TempF = 0;
        // float TempF = (TempC*1.8)+32.0;
        // Serial.print(TempF);
        // Serial.println("°C");

        // Room Temp
        float roomTemp = bpl["roomTemp"]; // 35 C
        if (Fahr == true){
        float FroomTemp = ((roomTemp*1.8)+32.0);
        Serial.printf("Room Temp  =  %.1f F\n", FroomTemp);  // minimum number of digits to write 100.0
        tft.setTextColor(TFT_WHITE,TFT_BLACK);
          tft.setCursor(10, 90);
          if(FroomTemp <10){
            // change cursor posn +32px if less than 10 to align the display position for non skinny digits
            tft.setCursor(42,90);
          }tft.println(FroomTemp,1); // get the room temp ,1 one decimal place
         
        }
        else if (Fahr == false)
        {
          Serial.printf("Room Temp  =  %.1f C\n", roomTemp);  // https://alvinalexander.com/programming/printf-format-cheat-sheet/  \n for newline == println
          // debugln(roomTemp,1);
          tft.setTextColor(TFT_WHITE,TFT_BLACK);
          tft.setCursor(10, 90);
          if(roomTemp <10){
            // change cursor posn +32px if less than 10 to align the display position for non skinny digits
            tft.setCursor(42,90);
          }tft.println(roomTemp,1); // get the room temp ,1 one decimal place
        }  
        // Beer temp
        float beerTemp = bpl["beerTemp"]; // 10.074
        if (Fahr == true){
        float FbeerTemp = ((beerTemp*1.8)+32.0);
        Serial.printf("Beer Temp  =  %.1f F\n", FbeerTemp);

        tft.setTextColor(TFT_GOLD,TFT_BLACK); 
        tft.setCursor(145,90);
        
        if(beerTemp <10.0){ // try using 10.0 rather than 10
        //   // change cursor posn +32px if less than 10 to align the display position
        tft.setCursor(177,90);
        }
        tft.println(FbeerTemp,1); // get the beer temp
        }
        else if (Fahr == false)
        {
        Serial.printf("Beer Temp =  %.1f C\n", beerTemp);
        
        tft.setTextColor(TFT_GOLD,TFT_BLACK); 
        tft.setCursor(145,90);
        if(beerTemp <10){
        //   // change cursor posn +32px if less than 10 to align the display position
        tft.setCursor(177,90);
        }
        tft.println(beerTemp,1); // get the beer temp
        }           
        // Fixed text
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        tft.setCursor(280,115);//was 280,130
        if (Fahr == true){
        tft.println("F"); debugln("Fahrenheit set");
        }
        else if (Fahr == false){
        tft.println("C"); debugln("Celsius set");
        }
        tft.setTextFont(1);
        tft.setCursor(265,85);// tft.drawCircle(89, 56, 2); // was 265,100
        tft.println("o");

        tft.setTextColor(TFT_SILVER,TFT_BLACK); 
        tft.setTextFont(1);
        // Fixed Text
        tft.setCursor(30,100);
        tft.println("Fridge");
        tft.setCursor(155,100);
        tft.println("Target");

        tft.setFreeFont(&Orbitron_Medium_18);
        tft.fillRect(10,115,245,35,TFT_BLACK);// background for custom font
        
        // Fridge Temp
        float fridgeTemp = bpl["fridgeTemp"]; // 14.893
        if (Fahr == true){
        float FfridgeTemp = ((fridgeTemp*1.8)+32.0);
        Serial.printf("F Fridge Temp  =  %.1f F\n", FfridgeTemp);

        tft.setTextColor(TFT_WHITE,TFT_BLACK); 
        tft.setCursor(10,150);
        if(FfridgeTemp <10){
          // change cursor posn +32px if less than 10 to align the display position
          tft.setCursor(42,150);
        }
        tft.println(FfridgeTemp,1); // get the fridge temp
        }
        else if (Fahr == false)
        {
        Serial.printf("Fridge Temp =  %.1f C\n", fridgeTemp);
        
        tft.setTextColor(TFT_WHITE,TFT_BLACK);
        tft.setCursor(10, 150);
        if(fridgeTemp <10){
          // change cursor posn +32px if less than 10 to align the display position
          tft.setCursor(42,150);
        }
        tft.println(fridgeTemp,1); // get the fridge temp ,1 one decimal place
        }
        // Beer Set ie Target Temperature
        float beerSet = bpl["beerSet"]; // 10
        if (Fahr == true){
        float FbeerSet = ((beerSet*1.8)+32.0);
        Serial.printf("F Target Temp  =  %.1f F\n", FbeerSet);
        tft.setTextColor(TFT_MAGENTA,TFT_BLACK); 
        tft.setCursor(145,150);
        if(FbeerSet <10){
          // change cursor posn +32px if less than 10 to align the display position
          tft.setCursor(177,150);
        }
        tft.println(FbeerSet,1); // get the set temp
        } 
        else if (Fahr == false)
        {
        Serial.printf("Target Temp  =  %.1f C\n", beerSet);
        
        tft.setTextColor(TFT_MAGENTA,TFT_BLACK);
        tft.setCursor(145, 150);
        if(beerSet <10){
          // change cursor posn +32px if less than 10 to align the display position
          tft.setCursor(177,150);
        }
        tft.println(beerSet,1); // get the set temp ,1 one decimal place
        
        
        }
        // _---------------------------------------------------------------------
        // for use when information comes via BPL
        // float fridgeSet = doc["fridgeSet"]; // 13.752
        // debug("Fridge Set :  ");
        // debugln(fridgeSet);

        // // iSpindel
        // float plato = doc["plato"]; // 13.752
        // debug("Plato :  ");
        // debugln(plato);

        // float auxTemp = doc["auxTemp"]; // 13.752
        // debug("iSpindel Temp :  ");
        // debugln(auxTemp,1);

        // float voltage = doc["voltage"]; // 13.752
        // debug("iSpindel V :  ");
        // debugln(voltage);

        // float tilt = doc["tilt"]; // 13.752
        // debug("iSpindel Angle :  ");
        // debugln(tilt);
        float Bpressure = bpl["pressure"]; // leave for diag if pressure direct from BPL rather than via BF
        Serial.printf("BPL Pressure  =  %.1f PSI\n", Bpressure);

        // Mostly BPL MODE Screen
        // tft.fillRoundRect(0,0,320,240,2, Idle);tft.fillRoundRect(4,4,312,232,2, InnerBac);// clear the screen
        tft.setTextFont(1);
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        tft.setCursor(12,12);
        int mode = bpl["mode"]; // 3
        debug("BPL MODE :  ");
        if (mode == 0) 
        { debugln("OFF ");tft.setTextColor(TFT_RED,TFT_BLACK); tft.println("OFF");tft.setTextColor(TFT_SILVER,TFT_BLACK);
        }
        if (mode == 1) 
        { debugln("FRIDGE CONSTANT "); tft.fillRoundRect(0,0,320,240,2, Idle);tft.fillRoundRect(4,4,312,232,2, InnerBac); // clear the screen
        tft.setTextColor(TFT_SILVER,TFT_BLACK);tft.println("Fridge Constant");
        
        // if Fridge Constant then Target beerSet should change to fridgeSet value   the rest of the screen has no values displayed 
        float fridgeSet = bpl["fridgeSet"]; // 13.752
        debug("Fridge Set :  ");
        debugln(fridgeSet);
        tft.setFreeFont(&Orbitron_Medium_18);
        tft.fillRect(10,115,245,35,TFT_BLACK);// background for custom font
        tft.setTextColor(TFT_MAGENTA,TFT_BLACK);
        tft.setCursor(145, 150);
          if(fridgeSet < 10){
            // change cursor posn +32px if less than 10 to align the display position
            tft.setCursor(177,150);
          }
        tft.println(fridgeSet,1); // get the fridge temp ,1 one decimal place
        }
        tft.setTextFont(1);
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        tft.setCursor(12,12);
        if (mode == 2) { debugln("BEER CONSTANT"); tft.setTextColor(TFT_SILVER,TFT_BLACK);tft.println("Beer Constant");}
        if (mode == 3) { debugln("BEER PROFILE"); tft.setTextColor(TFT_SILVER,TFT_BLACK);tft.println("Beer Profile");}
        
        // // Fixed Text
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setTextFont(1);
          tft.setCursor(20,160);
          //tft.println("Orig. Grav.");
          tft.println("App.Att. %");
          tft.setCursor(210,160);
          tft.println("ABV % ");
          
          tft.setFreeFont(&Orbitron_Medium_18);
          tft.fillRect(10,175,245,35,TFT_BLACK);// background for custom font
        // ---------------------------------------------------------------------------------------------------------------------------------          
        // Apparent Attenuation
        tft.setTextColor(TFT_GREEN,TFT_BLACK);
        tft.setCursor(20, 210);
         if (iSp_sg == 0)
         { // if iSp_sg not received yet print blank  don't forget the ==
         tft.println("");
         }
         else if (iSp_sg > 0)
         {
          App_Att = (((SGogy-1) - (iSp_sg-1))/(SGogy-1))*100; // 40 - say 20  =20/40 = 50% * by 100
          bool neg_Val = App_Att < 0; // trap negative numbers and force to zero
          if (neg_Val) App_Att = 0;
          tft.println(App_Att,1);
          Serial.printf("Current Apparent Attenuation = %.2f%s\n" , App_Att, "%");
          debugln("");    
         }
        
        // App Att corrected for iSp temperature
        tft.setTextColor(TFT_GREEN,TFT_BLACK);
        tft.setCursor(20, 210);
        if (iSp_sg == 0)
         { // if iSp_sg not received yet print blank  don't forget the ==
         tft.println("");
         }
        else if (Temp_Corr == true)  
        { (iSp_sg = iSp_sga); // use iSp_sga
           float App_Att_Corr = (((SGogy-1) - (iSp_sga-1))/(SGogy-1))*100; // using temp adjusted PG
           tft.println(App_Att_Corr,1);
          tft.fillCircle(130,185,6,TFT_ORANGE); // indicator that temp correction is active
          // testing omly
          Serial.printf("Current Temp Corr. Apparent Attenuation = %.2f%s\n" , App_Att, "%");
          debugln(""); 
        }
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        
        // ABV   from iSpindel calc in SG display in Plato option
          tft.setFreeFont(&Orbitron_Medium_18);
          
          if (iSp_sg <= 0){  // if iSpindel gravity not received yet removed the - 0 or negative don't calculate abv
          abv = 0.0;
          Serial.printf("ABV   =  %.2f%s\n", abv,"%");
          tft.setTextColor(TFT_LIGHTGREY,TFT_BLACK);  tft.setCursor(195,210);
          tft.println("--.--");
          }
          else if (iSp_sg > 0){ // greater than zero was not equal to zero
          float Init_abv; // first calc ABV then work out the fudge factor https://www.gov.uk/government/publications/excise-notice-226-beer-duty/excise-notice-226-beer-duty--2#calculation-strength
          Init_abv = ((SGogy - iSp_sg)*131.25); Serial.printf("Initial ABV = %.3f\n", Init_abv); // nominal 131.25 bit high as 6% beers! seems to be what BF uses
          if (Init_abv >= 0.0  && Init_abv <= 3.3 ) ABV_adjG = 128; 
          if (Init_abv >= 3.3  && Init_abv <= 4.6 ) ABV_adjG = 129;
          if (Init_abv >= 4.6  && Init_abv <= 6.0 ) ABV_adjG = 130;
          if (Init_abv >= 6.0  && Init_abv <= 7.5 ) ABV_adjG = 131;
          if (Init_abv >= 7.5  && Init_abv <= 9.0 ) ABV_adjG = 132;
          if (Init_abv >= 9.0  && Init_abv <= 10.5 ) ABV_adjG = 133;
          if (Init_abv >= 10.5  && Init_abv <= 12.0 )ABV_adjG = 134;
          
          debug("Fudge : "); debugln(ABV_adjG);

          // recalculate the ABV using the fudge factor and print the results
          
          adjABV = ((SGogy - iSp_sg) * ABV_adjG);
          Serial.printf("Adjusted ABV   =  %.2f%s\n", adjABV,"%");
          tft.setTextColor(bronze,TFT_BLACK);  tft.setCursor(195,210);
          tft.println(adjABV,2);// 2 dp as an indicator of change not for accuracy
          // tft.setTextColor(TFT_ORANGE,TFT_BLACK); // tft.setCursor(180,190); // if temp adjusted SG
          if (Temp_Corr == true) {
          tft.fillCircle(306,185,6,TFT_ORANGE); // indicator that temp correction is active
          }
          } // end abv adjustment
          
          // Don't forget the ==
            // x == y (x is equal to y)
            // x != y (x is not equal to y)
            // x <  y (x is less than y)  
            // x >  y (x is greater than y) 
            // x <= y (x is less than or equal to y) 
            // x >= y (x is greater than or equal to y)       

        tft.setTextFont(1);
        tft.setTextColor(TFT_SILVER,TFT_BLACK);
        
        }
        break;
    

    }//switch end


        // iSpindel data ref only
        // decode name
        //if topic ispindel separate not a JSON? these messages received after setting iSp to 192.168.0.27 and port 1883
        // ispindel/Devices' name/temperature   'ispindel/iSpindelBLU[SG]/temperature' with data '25.5'
        // ispindel/Devices' name/temp_units    'ispindel/iSpindelBLU[SG]/temp_units' with data 'C'
        // ispindel/Devices' name/tilt          'ispindel/iSpindelBLU[SG]/tilt' with data '89.41939'
        // ispindel/Devices' name/battery       'ispindel/iSpindelBLU[SG]/battery' with data '3.847758'
        // ispindel/Devices' name/interval      'ispindel/iSpindelBLU[SG]/interval' with data '900'
        //                                      'ispindel/iSpindelBLU[SG]/interval' with data '1'  ???? random message
        // ispindel/Devices' name/RSSI          'ispindel/iSpindelBLU[SG]/RSSI' with data '-92'
        // ispindel/Devices' name/gravity       'ispindel/iSpindelBLU[SG]/gravity' with data '1.111492'   sg
    
    
    //  // error trap else
  }
};
        // -----------------------------------------------------------------------------------------------
        //  BPL JSON ref only
        // String payload=""; //whole json 
        //  int status; //stateState in Integer. 0:IDLE, 1:STATE_OFF,2: DOOR_OPEN, 3:HEATING, 4: COOLING, 5: WAITING_TO_COOL, 6:WAITING_TO_HEAT, 7:WAITING_FOR_PEAK_DETECT, 8:COOLING_MIN_TIME, 9:HEATING_MIN_TIME
        //  int beerTemp; //beer temp
        //  int beerSet; // beer set
        //  int fridgeTemp;//fridge temp
        //  int fridgeSet; // fridge set
        //  int roomTemp; // room temperature
        //  int mode; //mode
        //  int pressure;  //pressure
        // {"state":0,"beerTemp":9.744,"beerSet":9.5,"fridgeTemp":9.75,"fridgeSet":9.238,"roomTemp":23.813,"mode":3,"pressure":0} example message

myMQTTBroker myBroker;

void setup()
{ //WiFi.disconnect();
  // TFT Splash Screen
  tft.init(); //  tft.begin(); difference?
  // for AA fonts
  // tft.setCallback(pixelColor);  // Switch on color callback for anti-aliased fonts
  //tft.setCallback(nullptr);   // Switch off callback (off by default)
  
  tft.setRotation(1); // landscape
  // tft.loadFont(AA_FONT_LARGE); // AA 
  // delay(2000); 
  // tft.setTextSize(1);
  tft.fillScreen(Backgnd);
  tft.fillRoundRect(4,4,312, 232, 2, InnerBac);
  tft.setTextColor(bronze,TFT_BLACK);  
  tft.setTextSize(3);
    
  tft.setCursor(90,105);// was 90,105
  tft.println("FermWatch");
  
  // tft.unloadFont(); // Remove the font to recover memory used AA
  // tft.loadFont(AA_FONT_MONO); // AA
  tft.setTextSize(2);
  // tft.setFreeFont(&NotoSans_Regular20pt7b);
  tft.setCursor(90,145);
  tft.println("Version " + version);

// Yeast budding  A grey ellipse origin at (70, 140) with horizontal radius of 35, and vertical radius of 30
  tft.fillEllipse(45, 80, 35, 30, yeast);  // mother
  delay(2500);
  tft.fillEllipse(100,70, 20, 15, yeast); //  daughter
  
  tft.setCursor(20,190);
  tft.println("Look for FermWatch_AP");
  // tft.unloadFont(); // Remove the font to recover memory used AA
  
  tft.setTextSize(2);
  delay(4000);
  
    pinMode(LED_BUILTIN, OUTPUT); // onboard led light 8266 
  
  // eg from https://github.com/witnessmenow/ESP32-WiFi-Manager-Examples/blob/main/UseCase1_Simple/UseCase1_WithWM/UseCase1_WithWM.ino
  
  // Change to true when testing to force configuration every time we run
  // Made this TRUE so that a single reset causes a cycle through the config cycle to get around 
  // the problem of a single reset in some way altering the bool Fahr and Plato to true
  bool forceConfig = true; 
  
  
  Serial.begin(115200); 
  delay(10);
  
  // interpolation for temperature correction
  // void test_interpolation();


  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (drd->detectDoubleReset())
  {
    Serial.println(F("Forcing config mode as there was a Double reset detected"));
    forceConfig = true;
  }

  bool LittleFSSetup = loadConfigFile();
  if (!LittleFSSetup)
  {
    Serial.println(F("Forcing config mode as there is no saved config"));
    forceConfig = true;
  }
   
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    
  WiFiManager wm;
  // Remove any previous network settings  for testing 
  wm.resetSettings();// wipe settings every time

  //set config save notify callback
  // wm.setSaveConfigCallback(saveConfigCallback); // save the params when using the /param page took this out no effect
  // wm.setDebugOutput(false); // disable  wm serial print debug messages
  
  // wm.setConfigPortalTimeout(60); // timeout if blocked too long and then autoconnect
  
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);

// wm.setPreSaveConfigCallback(saveConfigCallback); // see if this keeps the settings not in dronerobot version

//--- additional Configs params ----------------------------------------------------------------------------------------------------------------------------------------

  // Text box (String) BF Base64 encoded Authorisation Basic
  WiFiManagerParameter custom_text_box("BFAuth_text", "Enter YOUR Brewfather Authorisation Basic - 125 characters Base64 encoded", Auth_B, 126); // 125 == max length 125+1
  
  // Text box (Number) for NTP Client UTC offset in seconds
  char convertedValue[5];
  sprintf(convertedValue, "%d", UTC_offset); // Need to convert to string to display a default value.

  WiFiManagerParameter custom_text_box_num("UTC_num", "UTC Offset time (seconds)", convertedValue, 5); // 5 == max length

  // Text box (Number)
  char convertedValue2[6];
  sprintf(convertedValue2, "%d", BF_updateInt); // Need to convert to string to display a default value.

  WiFiManagerParameter custom_text_box_num2("BF_num", "Brewfather Update time (milliseconds)", convertedValue2, 6); // 6 == max length

  //Check Box NEED default set for unchecked http://pdacontrolen.com/introduction-library-wifimanager/
  // tried two different ways same result achieved  // the earlier version of WM doesn't have , WFM_LABEL_AFTER
    char *customHtml;
    if (Plato)   {    customHtml = "type=\"checkbox\" checked";     }
    else         {    customHtml = "type=\"checkbox\"";     }
    // char customhtml[24] = "type=\"checkbox\"";
    // const char _customHtml_checkbox[] = "type=\"checkbox\"";
    WiFiManagerParameter Plato_chk("Plato_bool", "Check for Gravity in Plato. Default is SG", "T", 2, customHtml, WFM_LABEL_AFTER);
         
    char customhtml2[24] = "type=\"checkbox\"";
    if (Fahr) {    strcat(customhtml2, " checked");     }
    WiFiManagerParameter Fahr_chk("Fahr_bool", "Check for Temperature in Fahr. Default is Celsius", "T", 2, customhtml2, WFM_LABEL_AFTER);

    char *customHtml3;
    if (Temp_Corr)  {      customHtml3 = "type=\"checkbox\""; }
    else            {      customHtml3 = "type=\"checkbox\"checked";    }
    WiFiManagerParameter Temp_chk("Temp_Corr_bool", "Check for iSpindel Gravity Temperature Adjustment.     Default is none.", "T", 2, customHtml3, WFM_LABEL_AFTER); // label after checkbox

    // WiFiManagerParameter custom_html("<br>"); // line break separator between the check boxes
    
    //add all your parameters here
    wm.addParameter(&custom_text_box);
    wm.addParameter(&custom_text_box_num);
    wm.addParameter(&custom_text_box_num2);
    wm.addParameter(&Plato_chk);
    wm.addParameter(&Fahr_chk);
    wm.addParameter(&Temp_chk);
  
  // just the wifi not in  this version wm
    std::vector<const char *> menu = {"wifi"}; // only show WiFi on the menu
    wm.setMenu(menu); // custom menu, pass vector  
  
  // set dark theme
    wm.setClass("invert");

  //set static ip  dns  IPAddress(192,168,0,1  if you have more that one FermWatch make sure you use different IP addresses!
  wm.setSTAStaticIPConfig(IPAddress(192,168,0,42), IPAddress(192,168,0,1), IPAddress(255,255,255,0), IPAddress(192,168,0,1)); // set static ip,gw,sn,dns
  wm.setShowStaticFields(true); // force show static ip fields
  wm.setShowDnsFields(true);    // force show dns field always
  
  if (forceConfig)
  // Run if we need a configuration
  { 
    if (!wm.startConfigPortal("FermWatch_AP", "fermwatch")) //on demand
    {
     tft.setTextColor(TFT_RED,TFT_BLACK);
     tft.setCursor(20,190);
     tft.println("Failed to Connect - Try Again");
     
     debugln("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
    }
   shouldSaveConfig = true; // not sure which one will work try this one  https://forum.arduino.cc/t/wifi-manager-custom-parameters-are-not-saved-in-memory-via-ondemand-portal/921063/4
  }
  else
  {
    if (!wm.autoConnect("FermWatch_AP", "fermwatch"))
    {
      debugln("Auto Connect failed to connect and hit timeout");
      delay(3000);
      // if we still have not connected restart and try all over again
      ESP.restart();
      delay(5000);
    }
  
  }
  
// If we get here, we are connected to the WiFi blue light on

  digitalWrite(LED_BUILTIN, OUTPUT);
  debugln("");
  debug ("WiFi connected  ");
  debug("IP address: ");
  debugln(WiFi.localIP());

  // Lets deal with the user config values
  // Copy the string value
  strncpy(Auth_B, custom_text_box.getValue(), sizeof(Auth_B));
  debug("BF Authorisation Basic: ");   debugln(Auth_B);

  //Convert the number values
  UTC_offset = atoi(custom_text_box_num.getValue());
  debug("UTC Offset: ");   debugln(UTC_offset);

  BF_updateInt = atoi(custom_text_box_num2.getValue());
  debug("BF update Interval: ");   debugln(BF_updateInt);

  //Handle the Plato bool value
  Plato = (strncmp(Plato_chk.getValue(), "T", 1) == 0); // was  == 0 test if 
  debug("Plato: ");
  if (Plato)  {   debugln("true");  }
  else        {   debugln("false");  }
  
  //Handle the Fahr bool value
  Fahr = (strncmp(Fahr_chk.getValue(), "T", 1) == 0);// was ==  0  
  
  debug("Fahr: ");
  if (Fahr) {  debugln("true");   }
  else      {  debugln("false");   }
  
  //Handle the Temp_Corr bool value
  Temp_Corr = (strncmp(Temp_chk.getValue(), "T", 1) == 0);// was ==  0  
  
  debug("Temp Corr: ");
  if (Temp_Corr) {  debugln("true  ");   }
  else           {  debugln("false  ");   }

  //save the custom parameters to FS
  if (shouldSaveConfig) // saveConfigFile();
  {
  saveConfigFile(); 
  }

timeClient.begin(); // NTP

//-----------------------TFT config messages
  
  tft.fillScreen(Backgnd);
  tft.fillRoundRect(4,4,312, 232, 2, InnerBac);
  tft.setCursor(20,40);
  tft.println("Reconfigure Settings? ");
  tft.setCursor(20,80);
  tft.println("Press reset.");// Single press reset seems to work.
  tft.setCursor(20,120);
  tft.println("Select FermWatch_AP");
  tft.setCursor(20,160);
  tft.println("Password fermwatch ");
  // tft.setCursor(40,200);
  // tft.println("Change to suit");
    
  // x = 320, y = 240;  //screen size x horiz y vert swapped for landscape!!
  // draw inner backgnd
  
  delay(5000); // view screen
  
    
  tft.fillScreen(Backgnd);
  tft.fillRoundRect(4,4,312, 232, 2, InnerBac);
  tft.setTextColor(bronze,TFT_BLACK);
  tft.setCursor(20,180);
  tft.println("WiFi Connected");
    tft.setCursor(12,210);
  tft.println("FermWatch IP " + WiFi.localIP().toString());
   debugln("");
   debugln("FermWatch IP" + WiFi.localIP().toString());

  // Start the broker
  debugln("Starting MQTT broker");
  tft.setCursor(20,30);
  tft.println("Starting MQTT broker...");
  tft.setCursor(20,60);
  tft.println("If no BPL data shown");
  tft.setCursor(20,90);
  tft.println("Check that the BPL MQTT");
  tft.setCursor(20,120);
  tft.println("settings are correct"); 
  tft.setCursor(20,150);
  tft.println("or try resetting BPL");
  debugln("If no BPL data shown check BPL MQTT Settings correct or try resetting BPL");
  
  tft.setTextColor(TFT_SILVER,TFT_BLACK); 
  delay(5000); // to allow the screen to be viewed

  myBroker.init();
  // uncomment the next line to check blanking of custom fonts
 // tft.fillScreen(TFT_BLUE);
  /*
 * Subscribe to anything
 */
  myBroker.subscribe("#");  // might need to restrict to BPL in future?

  // Get data from Brewfather API   mostly code from 
  //
  // https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFiClientSecure/examples/WiFiClientInsecure/WiFiClientInsecure.ino
  // and for the JSON
  // https://github.com/witnessmenow/arduino-sample-api-request/blob/master/ESP8266/HTTP_GET_JSON/HTTP_GET_JSON.ino
  // and  should not be in setup so that the batch ID can be available elsewhere
    
  debugln("\nStarting connection to server...");
  client.setInsecure(); //the magic line, use with caution??? no certificate
  if(!client.connect("api.brewfather.app", 443)){
  debugln("BF Connection Failed!");
  }
  else 
  {
    debugln("Connected to BF API !");  
    
    // Send HTTP request using the html from Postman converted using  https://davidjwatts.com/youtube/esp8266/esp-convertHTM.html     
    // base 64 encoded ASCII to Linux URLsafe encoding   https://www.base64encode.org/ suggest paste result in the line below
    // 
    
    // Make a HTTPS request:
    client.print("GET https://api.brewfather.app/v1/batches/?include=measuredOg,recipe.fgEstimated&status=Fermenting HTTP/1.0\r\n"); 
      
    client.println(String("Host: ") + "api.brewfather.app");
    String A1 = "Authorization: Basic ";
    String auth = A1 + Auth_B;
    client.println(auth);
    
    client.println("Connection: close\r\n\r\n");
    client.println();
    
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        // debugln("Authorised. Batch headers received");
        break;
      }
    }
        // // Debug To check if there are incoming bytes available
        // // from the server, read them and print them: this lot under needs commenting out so the Json will work and vice versa
        //   while (client.available()) {
        //     char c = client.read();
        //     Serial.write(c); // to print
        //     // save possibly String buffer =Serial.readString();
        //   }
        //   //client.stop(); moved down
        //   // }
        
      //Use the ArduinoJson Assistant to calculate this:  https://arduinojson.org/v6/assistant/
      // used stream input code
      // size_t inputLength; 
    
    StaticJsonDocument<384> doc; // 512 works 448 works 384 works 

DeserializationError error = deserializeJson(doc, client);

if (error) {
  Serial.print(F("deserializeJson() Brew failed: "));
  Serial.println(error.f_str());
  tft.fillScreen(TFT_PURPLE);
        tft.fillRoundRect(4,4,312, 232, 2, InnerBac);// clear startup/last screen
        tft.setTextColor(TFT_RED,TFT_BLACK);
        tft.setTextFont(2);
        tft.setCursor(12,40);
        tft.println("Failed to Connect to BF");
        tft.setCursor(12,70);
        tft.println("       - Try Again");
        tft.setCursor(12,150);
        tft.println("    Check Auth Key");
        tft.setCursor(12,180);
        tft.println("    entered properly");
        tft.setTextFont(1);
        delay(20000);
  return;
}

JsonObject root_0 = doc[0];
const char* root_0_id = root_0["_id"]; // "LXXXXXXXXXXXXXXXXXXXXXXXXXXXXXLUL"
// const char* root_0_name = root_0["name"]; // "Batch"
int root_0_batchNo = root_0["batchNo"]; // 375
// const char* root_0_status = root_0["status"]; // "Fermenting"
// const char* root_0_brewer = root_0["brewer"]; // "Peter "
// long long root_0_brewDate = root_0["brewDate"]; // 1646398800000

    const char* root_0_recipe_name = root_0["recipe"]["name"]; // "2022 Skinners Lushingtons 2.1"
    float root_0_recipe_fgEstimated = root_0["recipe"]["fgEstimated"]; // 1.009
    float root_0_measuredOg = root_0["measuredOg"]; // 1.039  

    strcpy(batch_id, root_0_id);//use copy string to get the batch ID into the global variable batch_id
    // batch_id = root_0_id;    
    debugln(batch_id); // Serial.printf(root_0_id);
    batch_No = root_0_batchNo; // int
    Serial.print("Batch Number : "); Serial.println(batch_No);
    brew = root_0_recipe_name;
    Serial.println(brew);
    SGogy = root_0_measuredOg;
    FGEst = root_0_recipe_fgEstimated;
      
    // // debug
    client.stop();
  }
      // // Extract values for debug
      //   // debugln(F("From Brewfather API:"));
      //   Serial.printf("Batch ID  = %s\n", batch_id);
      //   // debug("Status:   ");
      //   // debugln(root_0_status);
      //   // debug("Batch No:  ");
      //   // debugln(batch_No);
      //   // Serial.printf("Brew  = %s\n", brew);

          if (Plato == true)
          {
          float OGPlato = ((259-(259/SGogy)));
          Serial.printf("Brewfather OG Plato  =  %.1f deg P\n", OGPlato);
          tft.setCursor(20,200);
          tft.print("Plato Set"); // tft.println(OGPlato,1);
          }
          else if (Plato == false)
          {
          Serial.printf("Brewfather OG SG  =  %.4f deg\n", SGogy);
          Serial.print("\n");
          }

          
   // initialise border 
   tft.fillScreen(TFT_GREY);
   tft.fillRoundRect(4,4,312, 232, 2, InnerBac);// clear startup/last screen
   tft.setTextColor(bronze,TFT_BLACK);
   tft.drawCentreString("Connected to ...   ",160,40,2);
   tft.drawCentreString("Brewfather API",160,70,2);
   tft.setTextFont(1);
   tft.drawCentreString("Waiting for batch  ",160,100,2);
   tft.drawCentreString("to update ...",160,130,2);
  
   tft.setTextColor(TFT_SILVER,TFT_BLACK); 
   
  tft.setTextFont(1);// try font 2
  
  
  // tft.loadFont(AA_FONT_SMALL);
  tft.setCursor(12,180);
  tft.print("Batch : "); tft.println(batch_No);
  tft.setCursor(12,200);
  tft.println(brew);
  delay(3000);
    
  
  // tft.unloadFont(); // Remove the AA font to recover memory used
  // delay(3000); // delay the screen till font unloaded
  tft.setTextFont(1);

} // end of setup

void loop()
{
   drd->loop(); // drd
  
   unsigned long currentTime = millis(); // original needed for BF loop timing 

  // https://github.com/RalphBacon/BB5-Moving-to-a-State-Machine/blob/main/Sketches/3-NonBlocking.ino
  // Refresh NTP Time every 1 sec
  void updateNTP();{
    static unsigned long NTPMillis = millis();
    if (millis() - NTPMillis >= 1000) // 1 sec
    {
      timeClient.update();
      tft.setCursor(214,12);
      tft.println((timeClient.getFormattedTime())); // 
		  NTPMillis = millis();// We must reset the local millis variable
    }
  }

  // Refresh BF Fermentations & iSpindel details every 3 minutes
  // using the batch ID 
  
    /* This is my event_2 */
    if ( currentTime - previousTime_2 >= eventTime_2_BF) 
  {
    
    debugln("\nStarting connection to server...");
    client.setInsecure(); //the magic line, use with caution??? no certificate
  
    if(!client.connect("api.brewfather.app", 443))
    debugln("Connection to BF Batch Failed!"); // this would likely be due to an internet failure
    else {
      debugln("Connecting to BF Batch "); // 
          
      // Make a HTTPS request:
      // debug only
      debug("Batch ID   "); debugln(batch_id); // does this global variable get here?
            String S1 = "GET https://api.brewfather.app/v1/batches/";
            String S2 = "/readings/last HTTP/1.1"; // 
            String last = S1 + batch_id + S2; // this now works!
            client.println(String(last));// the last ie the latest readings
            client.println(String("Host: ") + "api.brewfather.app");
            String A1 = "Authorization: Basic ";
            String auth = A1 + Auth_B;
            client.println(auth);
            
                        
            client.println("Connection: close");
            client.println();

            while (client.connected()) {
            String line = client.readStringUntil('\n');
             if (line == "\r") {
            // debugln("Batch latest readings received");
            break;
              }
            }
                   // for debugging
                // // if there are incoming bytes available from the server, read them and print them:
                // //  this lot under needs commenting out so the Json will work
                //   while (client.available()) {
                //     char c = client.read();
                //     Serial.write(c); // to print
                //     // save possibly String buffer =Serial.readString(); //?
                //   }
                // Serial.print(batch_id);//debug

        StaticJsonDocument<320> batch; // changed from the calculated 192 to 256 to get it to work try 320
        DeserializationError error = deserializeJson(batch, client);
      
      // added error messages likely due to not entering the proper Authorisation string
      if (error) 
        {
        debug(F("deserializeJson() Batch failed: "));
        debugln(error.f_str());
        client.stop();
        tft.fillScreen(TFT_PURPLE);
        tft.fillRoundRect(4,4,312, 232, 2, InnerBac);// clear startup/last screen
        tft.setTextColor(TFT_RED,TFT_BLACK);
        tft.setTextFont(2);
        tft.setCursor(12,40);
        tft.println("Failed to Connect to BF");
        tft.setCursor(12,70);
        tft.println("       - Try Again");
        tft.setCursor(12,150);
        tft.println("    Check Auth Key");
        tft.setCursor(12,180);
        tft.println("    entered properly");
        tft.setTextFont(1);
        delay(20000);
        return;
        }
        // NB any float int etc used here makes it local not GLOBAL
        // const char* comment = batch["comment"]; // nullptr
        pressure = batch["pressure"]; // 4
        // long long time = batch["time"]; // 1644817884597
        int rssi = batch["rssi"]; // -69
        String id = batch["id"]; // "ISPINDELPCBSG"
        float angle = batch["angle"]; // 29.20479
        // const char* type = batch["type"]; // "iSpindel"
        float battery = batch["battery"]; // 3.889468
        iSp_sg = batch["sg"]; // 1.0088  removed the float from the front
        Itemp = batch["temp"]; // 4.1

        // calculate % complete

        float GPtot =((SGogy-1) - (FGEst-1))*1000;  // expected gravity points to ferment in SG eg 40-10 = 30
        float PG_now = ((iSp_sg-1)*1000-(FGEst-1)*1000); // say 39 - 10 = 29
        float Prog = (((1-(PG_now) / GPtot))*100);   // 29 / 30 = %
        // debugln(iSp_sg); debugln(FGEst); debugln(PG_now); debugln(GPtot ); debugln(Att);
        // 
        //  Apparent Attenuation progressive 
        // App_Att = (((SGogy-1) - (iSp_sg-1))/(SGogy-1))*100; // 40 - say 20  =20/40 = 50% * by 100

        // Extract values for diaganostics
          debugln();
          debug(F("Batch:   ")); debugln(batch_id);
          debug("iSpindel Name:   "); debugln(id);
          Serial.printf("Pressure =  %.1f PSI\n", pressure);
          Serial.printf("SG       =  %.5f deg.\n", iSp_sg);// .4 decimal places
          Serial.printf("Battery  =  %.2f V\n", battery);
          Serial.printf("Angle    =  %.2f deg.\n", angle);
          Serial.printf("ITemp    =  %.1f C\n", Itemp);
        // Progress %
          Serial.printf("Present Progress = %.2f%s\n", Prog," %");  // %.2f%s\n", adjABV,"%"

        // tft display mostly iSpindel and derived functions-----------------------------------------------------------------------------------------------------
          // wipe the screen innerbac and update
          // tft.fillScreen(TFT_VIOLET); see if heating cooling etc is maintained
          tft.fillRoundRect(4,4,312, 232, 2, InnerBac);// clear startup/last screen
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setTextFont(1);
          tft.setCursor(12,12);
          // tft.print("iSpindel");
        // compare iSp connection status https://docs.arduino.cc/built-in-examples/strings/StringComparisonOperators  
          if (id == "manual") {
          debugln("iSpindel not attached or manual reading(s) entered!");
          tft.setCursor(12,12); // was 130,12
          tft.setTextColor(TFT_RED,TFT_BLACK);
          tft.print("Manual      "); // indicates that a manual reading has been input to BF e.g. pressure rest of fields,  last values received 900s BF cycle
          // or the fermenting bach has been kegged, BF status changed to conditioning and the values are no longer valid
          } 
          tft.setTextColor(TFT_SILVER,TFT_BLACK); // make sure time is not red
          tft.setCursor(214,12);// stays the same
          tft.println((timeClient.getFormattedTime()));
          
          
          // mostly fixed text
          tft.setTextColor(bronze,TFT_BLACK);
          tft.setCursor(12,12); // was 12,40
          tft.println(id); // iSpindel name
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setCursor(12,40); // was 12,70
          tft.println("RSSI"); 
          tft.setCursor(80,40); // was 80,70
          tft.setTextColor(TFT_GREEN,TFT_BLACK);
          tft.println(rssi);
          tft.setCursor(12,100); // same
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.println("iSp Temp.");
          tft.setCursor(135,40); // was 135,70
          tft.println("Battery");
          tft.setCursor(240,40); // was 240,70
          tft.setTextColor(TFT_GREEN,TFT_BLACK);
          tft.print(battery, 2); tft.println(" V");
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setCursor(185,100); // same
          tft.println("Angle");
          tft.setCursor(50,160); // same
          tft.println("PSI");
          tft.setCursor(140,160); // same
          tft.println("Present Grav.");
          
          tft.setFreeFont(&Orbitron_Medium_18);
          tft.fillRect(10,175,245,35,TFT_BLACK);// background for custom font
          
          // iSpindel Temperature
          tft.setTextColor(TFT_GOLD,TFT_BLACK);
          if (Fahr == true)
          {
          float iSpTemp = ((Itemp*1.8)+32.0);
          Serial.printf("iSpindel Temp  =  %.1f F\n", iSpTemp);
          tft.setCursor(10, 150);  // 
          tft.println(iSpTemp,1);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setCursor(130, 150); //was 125,150
          tft.print("F");
          tft.setTextFont(1);
          tft.setCursor(117,120);
          tft.print("o");
          }
          else if (Fahr == false)
          {
          Serial.printf("iSpindel Temp  =  %.1f C\n", Itemp); 
          tft.setFreeFont(&Orbitron_Medium_18);
          tft.fillRect(10,175,245,35,TFT_BLACK);
          tft.setCursor(10, 150);
          tft.println(Itemp,1); // get iSp temp ,1 decimal place
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setCursor(130, 150); //was 125,150
          tft.print("C");
          tft.setTextFont(1);
          tft.setCursor(117,120);
          tft.print("o");
          }
          tft.setFreeFont(&Orbitron_Medium_18);
          tft.fillRect(10,175,245,35,TFT_BLACK);
          // iSpindel Angle
          tft.setTextColor(TFT_RED,TFT_BLACK);
          tft.setCursor(175, 150); //was 165,150
          // if(Itemp <10){
          // // change cursor posn +32px if less than 10 to align the display position  LOOKS like superfluous
          // tft.setCursor(177,150);
          // }
          tft.println(angle ,2); // get iSp angle ,2 decimal place
          
          // Pressure
          tft.setTextColor(TFT_VIOLET,TFT_BLACK);
          tft.setCursor(20, 210);
          // if(pressure <10){
          // // change cursor posn +32px if less than 10 to align the display position
          // tft.setCursor(42,210);
          // }
          tft.println(pressure,1); // get Pressure ,1 decimal place

          // Present Gravity   from iSpindel SG unadjusted by iSp temperature
          tft.setTextColor(TFT_ORANGE,TFT_BLACK);
          if (Plato == true && Temp_Corr == false) 
          {
          Serial.print("Plato true "); Serial.println(Plato); Serial.print("Temp_Corr false  "); Serial.println(Temp_Corr);
          float iSpPlato = ((259-(259/iSp_sg)));
          Serial.printf("iSpindel Plato  =  %.1f deg P\n", iSpPlato);
          tft.fillRect(190,200,95,35,TFT_BLACK);
          tft.setCursor(145,210);
          tft.println(iSpPlato, 1);
          tft.setCursor(260,210);// was 240,210
          tft.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
          tft.println("P");
          tft.setTextFont(1);
          tft.setCursor(245,180); //was 225,180
          tft.println("o");  
          }
          else if (Plato == false && Temp_Corr == false) 
          {
          Serial.print("Plato false "); Serial.println(Plato); Serial.print("Temp_Corr false  "); Serial.println(Temp_Corr);
          Serial.printf("iSpindel SG  =  %.5f deg\n", iSp_sg); 
          tft.setFreeFont(&Orbitron_Medium_18);
          // tft.fillRect(10,175,245,35,TFT_BLACK);// background for custom font whole screen strip         
          tft.setCursor(145,210);
          tft.println(iSp_sg, 4);
          }

        // Interpolation 
          // https://github.com/RobTillaart/MultiMap
          if (Temp_Corr == true)
          {
            float input[] = {  0.0, 1.0,  2.0,  3.0,  4.0,  5.0,  6.0,  7.0,  8.0,  9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0, 36.0, 37.0, 38.0, 39.0, 40.0 };
            float adj_Val[] = { -0.0016, -0.0017, -0.0017, -0.0018,-0.0018,-0.0018,-0.0017,-0.0017,-0.0016,-0.0016, -0.0015,-0.0014, -0.0013, -0.0012, -0.001, -0.0009,-0.0007,-0.0006, -0.0004, -0.0002, 0.0,
            0.0002, 0.0004,0.0007,0.0009,0.0012,0.0014,0.0017,0.002, 0.0023,0.0026,0.0029,0.0032,0.0035, 0.0039,0.0042,0.0045,0.0049,0.0053,0.0056,0.006};
            {
            SG_adjT = multiMap<float>(Itemp, input, adj_Val, 41);
            
            Serial.print("SG_adjT = "); Serial.println(SG_adjT,5);
            delay(10);
            
            iSp_sga = iSp_sg + SG_adjT;
            
            debug("Adjusted Gravity = "); Serial.println(iSp_sga,5);
            debug("iSp SG Temp correction "); Serial.println(Temp_Corr,5);
            debug("Correction : "); Serial.println(SG_adjT,5);
                      
            }
          }  

          
          if (Temp_Corr == true && Plato == false) 
          {
          tft.fillCircle(306,185,6,TFT_ORANGE);
                    
          // print adjusted SG
          tft.setFreeFont(&Orbitron_Medium_18);
          tft.setCursor(145,210);
          tft.fillRect(190,200,95,35,TFT_BLACK);  //add blanking box
          tft.println(iSp_sga, 4);
          //}
          }

          // for temperature adjusted Plato
          if (Temp_Corr == true && Plato == true) 
          {
          Serial.print("Plato true "); Serial.println(Plato); Serial.print("Temp_Corr true  "); Serial.println(Temp_Corr);
          tft.fillRect(190,200,95,35,TFT_BLACK);
          float iSpPlato = ((259-(259/iSp_sga)));// adjusted version
          tft.setTextColor(TFT_ORANGE,TFT_BLACK);
          tft.setCursor(145,210);
          tft.println(iSpPlato, 1);
          tft.setCursor(260,210);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.println("P");
          tft.setTextFont(1);
          tft.setCursor(245,180);
          tft.println("o"); 
          tft.fillCircle(306,185,6,TFT_ORANGE);
          }
          // Message Line
          // tft.setTextColor(TFT_WHITE,TFT_BLACK);
          //  swop adjusted SG value for unadjusted value
          if (Temp_Corr == true)  
          { (iSp_sg = iSp_sga); // use iSp_sga
          debugln("SG adjusted for iSpindel temperature");
          }
          else if (Temp_Corr == false) {
            debugln("No SG temp adjustment");
          }
          // OG
          tft.setTextFont(1);
          tft.setTextColor(TFT_GREEN,TFT_BLACK);
          tft.setCursor(12,70); // 
          if (Plato == true)
          {
          float OGPlato = ((259-(259/SGogy)));
          Serial.printf("Brewfather OG Plato  =  %.1f deg P\n", OGPlato);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.print("OG "); 
          tft.setTextColor(TFT_GREEN,TFT_BLACK);
          tft.print(OGPlato,1);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.println(" P");
          float FGPlato = ((259-(259/FGEst)));
          tft.setCursor(145,70);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.print("Est. FG "); 
          tft.setTextColor(0xF502,TFT_BLACK);
          tft.print(FGPlato,1); 
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.println(" P");
          }
          else if (Plato == false)
          {
          Serial.printf("Brewfather OG SG  =  %.5f deg\n", SGogy); 
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.print("OG "); 
          tft.setTextColor(TFT_GREEN,TFT_BLACK);
          tft.println(SGogy,4); // getOG ,4 decimal places
          tft.setCursor(145,70);
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.print("Est.FG "); 
          tft.setTextColor(0xF502,TFT_BLACK);
          tft.println(FGEst,4);  
          }
              
                   
          tft.setTextColor(TFT_SILVER,TFT_BLACK);
          tft.setTextFont(1);

          delay (15000);// 15 sec delay just in case the screen swop timing is a bit close
          
          client.stop(); // moved down
          }
        
        
        /* Update the timing for the next event*/
        previousTime_2 = currentTime;
  }
      
        
}