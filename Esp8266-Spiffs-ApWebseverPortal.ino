/* 
    modified by fryakatkop nov2015


  FSWebServer - Example WebServer with SPIFFS backend for esp8266
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.
 
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  
  upload the contents of the data folder with MkSPIFFS Tool ("ESP8266 Sketch Data Upload" in Tools menu in Arduino IDE)
  or you can upload the contents of a folder if you CD in that folder and run the following command:
  for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp8266fs.local/edit; done
  
  access the sample web page at http://esp8266fs.local *edit* dns will catch all request and response with 302
  edit the page by going to http://esp8266fs.local/edit
*/
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <FS.h>

#define DBG_OUTPUT_PORT Serial

const char *ssid = "ESP-test";
const char *password = "";

/* hostname for mDNS. Should work at least on windows. Try http://esp8266.local */
const char *myHostname = "esp8266";

const char *metaRefreshStr = "<head><meta http-equiv=\"refresh\" content=\"3; url=http://192.168.4.1/index.html\" /></head><body><p>redirecting...</p></body>";

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

// LED Animations
unsigned long previousMillis = 0;
bool ledAniRider = 0;
bool ledAniFlipper = 0;
bool ledAniDir = 0;
int ledAniPos = 0;
int interval = 100; // ms

/* Soft AP network parameters */
IPAddress apIP(192, 168, 4, 1); // note: update metaRefreshStr string if ip change!
IPAddress netMsk(255, 255, 255, 0);

// Web server
ESP8266WebServer server(80);

// Gpios with LEDs
byte ledIoNames[] = {5, 4, 13, 12, 14, 16 };  // GPIO 5&4 12&13 flipped on my board so dirty patch here 5<->4 12<->13


//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  DBG_OUTPUT_PORT.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}

void handleLeds()
{
	if(server.arg("LED") ) {
		int pin = server.arg("LED").toInt();
		bool onOFF = server.arg("state").toInt();
  	digitalWrite(pin, onOFF);
	}
	String json = "{ \"leds\": [";
	for (int i = 0; i < sizeof(ledIoNames); i++)
  {
		bool state = digitalRead(ledIoNames[i]);
    json += "{ \"" + String(i) + "\": [";
    json += "\""+String(ledIoNames[i])+"\", ";
		json += "\""+String(state)+"\" ] }";
    if(!(i == sizeof(ledIoNames)-1)) 
      json += ",";
    
	}
	json += "] }";
	server.send(200, "text/json", json);
	json = String();
	
}

void clearAll() {
  //stop Animations
  ledAniRider = 0;
  ledAniFlipper = 0;
  ledAniPos = 0;
  ledAniDir = 0;
  // clear all leds
  for (int i = 0; i < sizeof(ledIoNames); i++) {
    digitalWrite(ledIoNames[i], LOW);
  }  
}

void setup(void){
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.setDebugOutput(true);
  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    DBG_OUTPUT_PORT.printf("\n");
  }
  
	// Setup LED pins
	for (int i = 0; i < sizeof(ledIoNames); i++) {
		pinMode(ledIoNames[i],OUTPUT);
	} 

  //WIFI INIT
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);


  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println ( WiFi.softAPIP() );

  /* Setup the DNS server redirecting all the domains to the apIP */  
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Setup MDNS responder
  if (!MDNS.begin(myHostname)) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }
  
  
  //SERVER INIT
  //list directory
  server.on("/listFiles", HTTP_GET, handleFileList);
  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(302, "text/html", metaRefreshStr);
  });
  // handle led requests
  server.on("/leds", HTTP_GET, handleLeds);
  server.on("/clearAll", HTTP_GET, clearAll);
  server.on("/rider", HTTP_GET, [](){
      ledAniPos = 0;
      ledAniDir = 0;
      ledAniFlipper = 0;
      ledAniRider = 1;
  });
  server.on("/flipper", HTTP_GET, [](){
      ledAniPos = 0;
      ledAniDir = 0;
      ledAniRider = 0;
      ledAniFlipper = 1;
  });
  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/all", HTTP_GET, [](){
    String json = "{";
    json += "\"heap\":"+String(ESP.getFreeHeap());
    json += ", \"analog\":"+String(analogRead(A0));
    json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
  });
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

}
 
void loop(void){
  //DNS
  dnsServer.processNextRequest();
  //HTTP
  server.handleClient();

  // LED Animation
  if(ledAniRider || ledAniFlipper)
  {
    unsigned long currentMillis = millis();
  
    if (currentMillis - previousMillis >= interval) {
      // save the last time you blinked the LED
      previousMillis = currentMillis;
  
      if(ledAniRider)
      {
        if(ledAniDir == 0)
        {
          if(ledAniPos < sizeof(ledIoNames))
          {
            digitalWrite(ledIoNames[ledAniPos], HIGH);        
            digitalWrite(ledIoNames[ledAniPos -1], LOW);
            ledAniPos++;
          }
          else
          {
            ledAniDir = 1;
            digitalWrite(ledIoNames[ledAniPos], HIGH);        
            digitalWrite(ledIoNames[ledAniPos +1], LOW);
            ledAniPos--;
          }
        }
        else
        {
          if(ledAniPos >= 0)
          {
            digitalWrite(ledIoNames[ledAniPos], HIGH);        
            digitalWrite(ledIoNames[ledAniPos +1], LOW);
            ledAniPos--;
          }
          else
          {
            ledAniDir = 0;
            digitalWrite(ledIoNames[ledAniPos], HIGH);        
            digitalWrite(ledIoNames[ledAniPos -1], LOW);
            ledAniPos++;
          }
        }
      }
      else if(ledAniFlipper)
      {
        if(ledAniDir == 0)
        {
          if(ledAniPos < sizeof(ledIoNames))
          {
            digitalWrite(ledIoNames[ledAniPos], HIGH);
            ledAniPos++;
          }
          else
          {
            ledAniDir = 1;       
            digitalWrite(ledIoNames[ledAniPos], LOW);
            ledAniPos--;
          }
        }
        else
        {
          if(ledAniPos >= 0)
          {
            digitalWrite(ledIoNames[ledAniPos], LOW);
            ledAniPos--;
          }
          else
          {
            ledAniDir = 0;
            digitalWrite(ledIoNames[ledAniPos], HIGH);   
            ledAniPos++;
          }
        }
      }
    }
  }
}
