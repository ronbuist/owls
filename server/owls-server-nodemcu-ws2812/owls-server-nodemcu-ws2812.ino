// Server for LED strip Scratch extension, to run on NodeMCU.

#include <FS.h>                 // Library for SPIFFS
#include <NeoPixelBus.h>        // Library to control WS2812 led strip
#include <ESP8266WiFi.h>        // WiFi Library
#include <WebSocketsServer.h>   // Web socket server library
#include <DNSServer.h>          // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>   // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson

#define LED D4                  // Led in NodeMCU (v3).
#define PixelPin 2              // NodeMCU pin 2 (RX) connected to Digital In of Pixel Strip
#define MaxPixelCount 300       // Define this to be the maximum number of pixels you support
#define GRB                     // Define this if your led strip uses RBG instead of RGB.


//#define DEBUG                   // Define this to switch on debugging

#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.println (x)
#else
 #define DEBUG_PRINT(x)
#endif

// Initialize WS2812 Library
#ifdef GRB
  NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(MaxPixelCount, PixelPin);
#else
  NeoPixelBus<NeoRbgFeature, Neo800KbpsMethod> strip(MaxPixelCount, PixelPin);
#endif


//define your default values here, if there are different values in config.json, they are overwritten.
char owls_name[40] = "owls";
char owls_port[6] = "8001";
char owls_pixels[3] = "75";
char owls_case_light_on[5] = "true";
char owls_case_left[5] = "true";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

bool autoShow;
bool nodeLedOn;
int virtualPixels;
int PixelCount;
WebSocketsServer* webSocket = NULL;

void setup() {

  Serial.begin(115200);
  delay(10);

  pinMode(LED, OUTPUT);    // NodeMCU LED pin as output.
  nodeLedOn = false;

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    DEBUG_PRINT("mounted file system");

    //clean FS, for testing
    //SPIFFS.format();
    
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DEBUG_PRINT("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINT("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINT("\nparsed json");

          strcpy(owls_name, json["owls_name"]);
          strcpy(owls_port, json["owls_port"]);
          strcpy(owls_pixels, json["owls_pixels"]);
          strcpy(owls_case_light_on, json["owls_case_light_on"]);
          strcpy(owls_case_left, json["owls_case_left"]);

        } else {
          DEBUG_PRINT("failed to load json config");
        }
        configFile.close();
      }
    } else {
      DEBUG_PRINT("\n/config.json not found. Mark for creation.");
      shouldSaveConfig = true;
    }
  } else {
    DEBUG_PRINT("failed to mount FS");
  }


  // Set the extra parameters for WifiManager
  WiFiManagerParameter custom_owls_name("OWLS_name", "OWLS server name", owls_name, 40);
  WiFiManagerParameter custom_owls_port("OWLS_port", "OWLS port", owls_port, 6);
  WiFiManagerParameter custom_owls_pixels("OWLS_pixels", "OWLS Number of pixels", owls_pixels, 3);
  WiFiManagerParameter custom_owls_case_light_on("OWLS_case_light_on", "OWLS case light on (true or false)", owls_case_light_on, 5);
  WiFiManagerParameter custom_owls_case_left("OWLS_case_left", "OWLS case left of ledstrip (true or false)", owls_case_left, 5);

  // Set up WiFi using wifiManager, but first set the hostname we want.
  WiFi.hostname(owls_name);
  WiFiManager wifiManager;

  // Set the callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Add all the parameters
  wifiManager.addParameter(&custom_owls_name);
  wifiManager.addParameter(&custom_owls_port);
  wifiManager.addParameter(&custom_owls_pixels);
  wifiManager.addParameter(&custom_owls_case_light_on);
  wifiManager.addParameter(&custom_owls_case_left);
  
  String ssid = "OWLS-" + String(ESP.getChipId());
  wifiManager.autoConnect(ssid.c_str());

  // Get the parameters that were provided
  strcpy(owls_name, custom_owls_name.getValue());
  strcpy(owls_port, custom_owls_port.getValue());
  strcpy(owls_pixels, custom_owls_pixels.getValue());
  strcpy(owls_case_light_on, custom_owls_case_light_on.getValue());
  strcpy(owls_case_left, custom_owls_case_left.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DEBUG_PRINT("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["owls_name"] = owls_name;
    json["owls_port"] = owls_port;
    json["owls_pixels"] = owls_pixels;
    json["owls_case_light_on"] = owls_case_light_on;
    json["owls_case_left"] = owls_case_left;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUG_PRINT("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  // Initialize WS2812 Library
  PixelCount = atoi(owls_pixels);
  virtualPixels = PixelCount;
  strip.Begin(); // Init of Pixel Strip

  // Confirmation that WiFi is connected
  Serial.println("");
  Serial.print("WiFi connected. Local IP address is ");
  Serial.println(WiFi.localIP());
  nodeMCULedOn(true);
  strip.ClearTo(RgbColor(0,128,0));
  strip.Show();
  delay(1000);
  strip.ClearTo(RgbColor(0,0,0));
  strip.Show();


  // Initialize WebSocket server
  int WSPort = atoi(owls_port);
  DEBUG_PRINT("Websocket port is ");
  DEBUG_PRINT(owls_port);
  DEBUG_PRINT(String(WSPort));
  webSocket = new WebSocketsServer(WSPort);
  webSocket->begin(); 
  webSocket->onEvent(webSocketEvent); 
}

void loop() {
  // Handle websocket
  webSocket->loop();
}

void switchNodeMCULed() {
  if (nodeLedOn) {
    // NodeMCU led is on. Switch it off.
    digitalWrite(LED, HIGH);
  } else {
    // NodeMCU led is off. Switch it on.
    digitalWrite(LED, LOW);    
  }
  nodeLedOn = !nodeLedOn;
}

void nodeMCULedOn(bool ledOn) {
  if (ledOn) {
    // Switch NodeMCU led on.
    digitalWrite(LED, LOW);
  } else {
    // Switch NodeMCU led off.
    digitalWrite(LED, HIGH);    
  }
  nodeLedOn = ledOn;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){

  String cmdLine("");
  String params[5];
  int delimLoc;
  int pos;
  RgbColor currentColor;

  // We are only interested in messages of type TEXT...
  if (type == WStype_TEXT){

    // Get everything that was passed into a single string.
    cmdLine = String((char*) payload);
    DEBUG_PRINT("Command line received: ");
    DEBUG_PRINT(cmdLine);

    // Now get the actual command and the parameters
    delimLoc = cmdLine.indexOf(' ');            // finds location of first space
    if (delimLoc == -1) {
      params[0] = cmdLine;
    } else {
      pos = 0;
      while (delimLoc != -1) {
        params[pos] = cmdLine.substring(0, delimLoc);
        cmdLine = cmdLine.substring(delimLoc+1);    // keep the rest; comtinue with that
        delimLoc = cmdLine.indexOf(' ');
        pos++;
      }
      params[pos] = cmdLine;
    }
    DEBUG_PRINT("Command is ");
    DEBUG_PRINT(params[0]);

#ifdef DEBUG
    // switch the Node MCU led.
    switchNodeMCULed();
#endif

    // Handle the command.
    if (params[0] == "init") {
      // Initialize the led strip.
      autoShow = true;
      virtualPixels = PixelCount;
      String pixelCountString = String(PixelCount);
      webSocket->broadcastTXT(pixelCountString);
      // Now clear the strip.
      params[0] = "clear";
    }
    // clear command
    if (params[0] == "clear") {
      strip.ClearTo(RgbColor(0,0,0));
      if (autoShow) strip.Show();
    }
    // setpixel command
    if (params[0] == "setpixel") {
      if (virtualPixels < PixelCount) {
         pos = params[1].toInt();
         while (pos < PixelCount) {
           strip.SetPixelColor(pos,RgbColor(params[2].toInt(),params[3].toInt(),params[4].toInt()));
           pos = pos + virtualPixels;
         }
      } else {
        strip.SetPixelColor(params[1].toInt(),RgbColor(params[2].toInt(),params[3].toInt(),params[4].toInt()));
      }
      if (autoShow) strip.Show();
    }
    // setpixels command
    if (params[0] == "setpixels") {
      strip.ClearTo(RgbColor(params[1].toInt(),params[2].toInt(),params[3].toInt()));
      if (autoShow) strip.Show();
    }
    // autoshow command
    if (params[0] == "autoshow") {
      if (params[1] == "on") {
        autoShow = true; 
      } else {
        if (params[1] == "off") {
          autoShow = false;
        } else {
          DEBUG_PRINT("ERROR: unknown value for autoshow.");
        }
      }
    }
    // show command
    if (params[0] == "show") strip.Show();
    // shift command
    if (params[0] == "shift") {
      if (params[1] == "left" ) {
        if (virtualPixels < PixelCount) {
          pos = 0;
          while (pos < PixelCount) {
            strip.RotateLeft(1,pos,pos + virtualPixels - 1);
            pos = pos + virtualPixels;
          }
          if ((PixelCount % virtualPixels) > 0) {
            for (pos = 0; pos < (PixelCount % virtualPixels); pos ++) {
              currentColor = strip.GetPixelColor(pos);
              strip.SetPixelColor((PixelCount / virtualPixels) * virtualPixels + pos, currentColor);
            }
          }
        } else {
          strip.RotateLeft(1,0,PixelCount-1);
        }
      } else {
        if (params[1] == "right" ) {
          if (virtualPixels < PixelCount) {
            pos = 0;
            while (pos < PixelCount) {
              strip.RotateRight(1,pos,pos + virtualPixels - 1);
              pos = pos + virtualPixels;
              }
              if ((PixelCount % virtualPixels) > 0) {
                for (pos = 0; pos < (PixelCount % virtualPixels); pos ++) {
                  currentColor = strip.GetPixelColor(pos);
                  strip.SetPixelColor((PixelCount / virtualPixels) * virtualPixels + pos, currentColor);
                }
              }
          } else {
            strip.RotateRight(1,0,PixelCount-1);
          }
        } else {
          DEBUG_PRINT("ERROR: unknown value for shift.");
        }
      }
      if (autoShow) strip.Show();
    }
    // dim command
    if (params[0] == "dim") {
      for (pos = 0; pos < PixelCount; pos++) {
        currentColor = strip.GetPixelColor(pos);
        currentColor.Darken(params[1].toInt());
        strip.SetPixelColor(pos,currentColor);
      }
      if (autoShow) strip.Show();
    }
    // getpixelcount command
    if (params[0] == "getpixelcount") {
      String pixelCountString = String(PixelCount);
      webSocket->broadcastTXT(pixelCountString);      
    }
    // setVirtualPixels command
    if (params[0] == "setVirtualPixels") {
      virtualPixels = params[1].toInt();
    }
  } 
}
