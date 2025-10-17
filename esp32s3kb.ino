#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "wifiCreds.h"

USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
AsyncWebServer server(80);

// ======= Event types =======
enum EventType { KEY, MOUSE_MOVE, MOUSE_CLICK };

struct HIDEvent {
  EventType type;
  String key;
  bool shift;
  bool ctrl;
  bool alt;
  int dx;
  int dy;
};

// ======= FreeRTOS Queue =======
QueueHandle_t hidQueue;

// ======== Forward declarations ========
void moveMouse(int dx,int dy);
void pressKey(String key,bool shift,bool ctrl,bool alt);

String listFiles(const char *dirname = "/") {
  String output = "<html><body style='font-family:sans-serif;background:#111;color:#fff'>";
  output += "<h2>ESP32 File Browser (LittleFS)</h2><ul>";

  File root = LittleFS.open(dirname);
  if (!root || !root.isDirectory()) {
    return "Failed to open directory";
  }

  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    output += "<li><a href='" + name + "' style='color:#0f0'>" + name + "</a>";
    output += " (" + String(file.size()) + " bytes)";
    output += " <a href='/delete?name=/" + name + "' style='color:#f44'>[delete]</a>";
    output += " <a href='/stream?name=" + name + "' style='color:#f44'>[Open]</a>";
    output += "</li>";
    file = root.openNextFile();
  }
  output += "</ul>";

  // Upload form
  output += R"rawliteral(
  <h3>Upload File</h3>
  <form method='POST' action='/upload' enctype='multipart/form-data'>
    <input type='file' name='upload' accept='*/*'>
    <input type='submit' value='Upload'>
  </form>
  </body></html>
  )rawliteral";

  return output;
}

// ======== HID Task (Core 0) ========
void HIDTask(void* pv){
  USB.begin();
  Keyboard.begin();
  Mouse.begin();

  HIDEvent ev;
  for(;;){
    if(xQueueReceive(hidQueue,&ev, portMAX_DELAY)){
      switch(ev.type){
        case KEY:
          pressKey(ev.key, ev.shift, ev.ctrl, ev.alt);
          break;
        case MOUSE_MOVE:
          moveMouse(ev.dx, ev.dy);
          break;
        case MOUSE_CLICK:
          if( ev.key == "left")
              Mouse.click(MOUSE_LEFT);
          if( ev.key == "middle")
              Mouse.click(MOUSE_MIDDLE);
          if( ev.key == "right")
              Mouse.click(MOUSE_RIGHT);
          break;
      }
    }
  }
}

// ======== HID helpers ========
void moveMouse(int dx,int dy){
  Mouse.move(dx,dy);
}

void pressKey(String key,bool shift,bool ctrl,bool alt){
  // Apply modifiers first
  if (ctrl) Keyboard.press(KEY_LEFT_CTRL);
  if (alt)  Keyboard.press(KEY_LEFT_ALT);
  if (shift) Keyboard.press(KEY_LEFT_SHIFT);

  // Type the key itself
  if (key.length() == 1) {
    Keyboard.press(key[0]);
    delay(10);
    Keyboard.release(key[0]);
  } else if (key == "Enter") {
    Keyboard.press(KEY_RETURN);
    Keyboard.release(KEY_RETURN);
  } else if (key == "Tab") {
    Keyboard.press(KEY_TAB);
    Keyboard.release(KEY_TAB);
  } else if (key == "⌫") {
    Keyboard.press(KEY_BACKSPACE);
    Keyboard.release(KEY_BACKSPACE);
  } else if (key == "Space") {
    Keyboard.press(' ');
    Keyboard.release(' ');
  }

  Keyboard.releaseAll();
}

// ======== Web Server (Core 1) ========
void core1Task(void* pv){
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());

  MDNS.begin(mDNSName);

  // Root page - list files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", listFiles("/"));
  });
  
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("name")) {
      String name = "/"+request->getParam("name")->value();
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, name, "text/plain");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
      return;
    }
    request->send(200, "text/plain", "File can not be streamed");
  });

  if( !LittleFS.begin(true) ) {
    Serial.println("LittleFS failed");
  }

  // Serve files directly
  server.serveStatic("/", LittleFS, "/");

  // Handle file uploads
  server.on(
    "/upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", "<html><body><h3>Upload complete.</h3><a href='/'>Back</a></body></html>");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("UploadStart: %s\n", filename.c_str());
        if (!filename.startsWith("/")) filename = "/" + filename;
        request->_tempFile = LittleFS.open(filename, FILE_WRITE);
      }
      if (request->_tempFile) {
        request->_tempFile.write(data, len);
      }
      if (final) {
        Serial.printf("UploadEnd: %s (%u bytes)\n", filename.c_str(), (unsigned int)index + len);
        if (request->_tempFile) request->_tempFile.close();
      }
    }
  );

  // Delete a file
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("name")) {
      String filename = request->getParam("name")->value();
      if (LittleFS.exists(filename)) {
        LittleFS.remove(filename);
        request->send(200, "text/html", "<html><body><h3>Deleted " + filename + "</h3><a href='/'>Back</a></body></html>");
      } else {
        request->send(404, "text/plain", "File not found");
      }
    } else {
      request->send(400, "text/plain", "Missing name param");
    }
  });

  server.on("/press",HTTP_GET,[](AsyncWebServerRequest* r){
    if(r->hasParam("key")){
      HIDEvent ev;
      ev.type=KEY;
      ev.key=r->getParam("key")->value();

      ev.shift=r->hasParam("shift") && r->getParam("shift")->value()=="true";
      ev.ctrl=r->hasParam("ctrl") && r->getParam("ctrl")->value()=="true";
      ev.alt=r->hasParam("alt") && r->getParam("alt")->value()=="true";

    //press?key=a&lower=a&upper=A&shifted=A
      //if( ev.shift )
      //  ev.key=r->getParam("upper")->value();
      //else
      //  ev.key=r->getParam("lower")->value();
      Serial.print("Key:");
      Serial.print(ev.key);
      
      Serial.print(": Shift:");
      Serial.println(r->getParam("shift")->value());

      xQueueSend(hidQueue,&ev,0);
    }
    r->send(200,"text/plain","ok");
  });

  server.on("/move",HTTP_GET,[](AsyncWebServerRequest* r){
    HIDEvent ev;
    ev.type=MOUSE_MOVE;
    ev.dx=r->getParam("dx")->value().toInt();
    ev.dy=r->getParam("dy")->value().toInt();
    xQueueSend(hidQueue,&ev,0);
    r->send(200,"text/plain","ok");
  });

  server.on("/click",HTTP_GET,[](AsyncWebServerRequest* r){
    if (r->hasParam("button")) {
      HIDEvent ev;
      ev.type=MOUSE_CLICK;
      ev.key = r->getParam("button")->value();
      xQueueSend(hidQueue,&ev,0);
      Serial.println(r->getParam("button")->value());
    }
    r->send(200,"text/plain","ok");
  });

  server.begin();
  for(;;) vTaskDelay(1000);
}

// ======== Setup ========
void setup(){
  Serial.begin(115200);
  hidQueue=xQueueCreate(20,sizeof(HIDEvent));

  // Core 0: HID Task
  xTaskCreatePinnedToCore(HIDTask,"HIDTask",8192,NULL,1,NULL,0);
  // Core 1: Web Server
  xTaskCreatePinnedToCore(core1Task,"WebServer",8192,NULL,1,NULL,1);
}

void loop(){}
