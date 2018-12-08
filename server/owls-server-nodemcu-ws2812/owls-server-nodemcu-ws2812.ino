// Server for LED strip Scratch extension, to run on NodeMCU.

#include "NeoPixelBus.h"        // Library to control WS2812 led strip
#include "ESP8266WiFi.h"        // WiFi Library
#include "WebSocketsServer.h"   // Web socket server library

#define LED D4                  // Led in NodeMCU (v3).
#define PixelCount 75           // Number of leds on strip
#define PixelPin 2              // NodeMCU pin 2 (RX) connected to Digital In of Pixel Strip
#define WSPort 8001             // Websocket Port
#define GRB                     // Define this if your led strip uses RBG instead of RGB.

//#define DEBUG                   // Define this to switch on debugging


#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.println (x)
#else
 #define DEBUG_PRINT(x)
#endif

const char* WiFi_hostname = "owls";
const char* ssid = "YourSSID";        // Name of WiFi Network
const char* password = "YourSSIDPassword";  // Password of WiFi Network

bool autoShow;
bool nodeLedOn;
int virtualPixels;

// Initialize WS2812 Library
#ifdef GRB
  NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
#else
  NeoPixelBus<NeoRbgFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
#endif

// Initialize WebSocket server
WebSocketsServer webSocket = WebSocketsServer(WSPort);

void setup() {

  Serial.begin(115200);
  delay(10);

  virtualPixels = PixelCount;
  strip.Begin(); // Init of Pixel Strip
  strip.Show(); // Clears any lit up Leds

  pinMode(LED, OUTPUT);    // NodeMCU LED pin as output.
  nodeLedOn = false;

  // Connect to WiFi network
  WiFi.hostname(WiFi_hostname);
  WiFi.mode(WIFI_STA); //Indicate to act as wifi_client only.
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // Wait until connected to WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(125);
    strip.ClearTo(RgbColor(128,0,0));
    strip.Show();
    delay(125);
    strip.ClearTo(RgbColor(0,0,0));
    strip.Show();
    Serial.print(".");
    switchNodeMCULed();
  }

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

  // Initialize websocket
  webSocket.begin(); 
  webSocket.onEvent(webSocketEvent); 
}

void loop() {
  // Handle websocket
  webSocket.loop();
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
      webSocket.broadcastTXT(pixelCountString);
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
          strip.RotateLeft(1);
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
            strip.RotateRight(1);
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
      webSocket.broadcastTXT(pixelCountString);      
    }
    // setVirtualPixels command
    if (params[0] == "setVirtualPixels") {
      virtualPixels = params[1].toInt();
    }
  } 
}
