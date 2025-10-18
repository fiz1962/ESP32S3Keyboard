#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "HIDTask.h"
#include "WebTask.h"

// Global shared objects
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
QueueHandle_t hidQueue;

void setup() {
  Serial.begin(115200);
  hidQueue = xQueueCreate(20, sizeof(HIDEvent));

  // Core 0: HID Task
  xTaskCreatePinnedToCore(HIDTask, "HIDTask", 8192, NULL, 1, NULL, 0);
  // Core 1: Web Server
  xTaskCreatePinnedToCore(core1Task, "WebServer", 8192, NULL, 1, NULL, 1);
}

void loop() {}
