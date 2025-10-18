#pragma once
#include <Arduino.h>
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USB.h"

// Declare shared structs/enums (must match main sketch)
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

// These are provided by main.cpp / .ino
extern QueueHandle_t hidQueue;
extern USBHIDKeyboard Keyboard;
extern USBHIDMouse Mouse;

// Task entry point
void HIDTask(void* pv);

// Helper functions (optional if you want to call them elsewhere)
void moveMouse(int dx, int dy);
void pressKey(String key, bool shift, bool ctrl, bool alt);
