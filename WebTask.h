#pragma once
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "HIDTask.h"  // For HIDEvent and queue


extern AsyncWebServer server;

// Function declarations
void core1Task(void* pv);
String listFiles(const char *dirname = "/");
