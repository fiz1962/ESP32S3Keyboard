#include "WebTask.h"

AsyncWebServer server(80);

void startWiFi();

String listFiles(const char *dirname) {
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

void core1Task(void* pv) {

  startWiFi();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed");
  }

  // Root page - list files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", listFiles("/"));
  });

  // Stream file
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("name")) {
      String name = "/" + request->getParam("name")->value();
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, name, "text/plain");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
      return;
    }
    request->send(200, "text/plain", "File can not be streamed");
  });

  // Serve static files
  server.serveStatic("/", LittleFS, "/");

  // Upload handler
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

  // Delete file
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  // Keyboard events
  server.on("/press", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("key")) {
      HIDEvent ev;
      ev.type = KEY;
      ev.key = r->getParam("key")->value();
      ev.shift = r->hasParam("shift") && r->getParam("shift")->value() == "true";
      ev.ctrl  = r->hasParam("ctrl")  && r->getParam("ctrl")->value() == "true";
      ev.alt   = r->hasParam("alt")   && r->getParam("alt")->value() == "true";

      Serial.printf("Key:%s Shift:%s\n", ev.key.c_str(), r->getParam("shift")->value().c_str());
      xQueueSend(hidQueue, &ev, 0);
    }
    r->send(200, "text/plain", "ok");
  });

  // Mouse move
  server.on("/move", HTTP_GET, [](AsyncWebServerRequest* r) {
    HIDEvent ev;
    ev.type = MOUSE_MOVE;
    ev.dx = r->getParam("dx")->value().toInt();
    ev.dy = r->getParam("dy")->value().toInt();
    xQueueSend(hidQueue, &ev, 0);
    r->send(200, "text/plain", "ok");
  });

  // Mouse click
  server.on("/click", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (r->hasParam("button")) {
      HIDEvent ev;
      ev.type = MOUSE_CLICK;
      ev.key = r->getParam("button")->value();
      xQueueSend(hidQueue, &ev, 0);
      Serial.println(r->getParam("button")->value());
    }
    r->send(200, "text/plain", "ok");
  });

  server.begin();
  for (;;) vTaskDelay(1000);
}
