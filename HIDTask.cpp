#include "HIDTask.h"

void sendRawKey(uint8_t modifier, uint8_t keycode);
void sendCharRaw(char c);

void moveMouse(int dx, int dy) {
  Mouse.move(dx, dy);
}

void pressKey(String key, bool shift, bool ctrl, bool alt) {
  if (ctrl) Keyboard.press(KEY_LEFT_CTRL);
  if (alt)  Keyboard.press(KEY_LEFT_ALT);
  if (shift) {Keyboard.press(KEY_LEFT_SHIFT);delay(100);}
//KEY_UP_ARROW 
if (key.length() == 1 && key != "↑" && key != "↓" && key != "←" && key != "→" ) {
    Keyboard.write(key[0]);
    delay(10);
  } else if (key == "Enter") {
    Keyboard.press(KEY_RETURN);
    delay(100);
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
  } else if (key == "↑" ) {
    Keyboard.press(KEY_UP_ARROW);
    Keyboard.release(KEY_UP_ARROW);
    //Serial.print("HID Key:");
    //Serial.println(key);
  } else if (key == "↓" ) {
    Keyboard.press(KEY_DOWN_ARROW);
    Keyboard.release(KEY_DOWN_ARROW);
    //Serial.print("HID Key:");
    Serial.println(key);
  } else if (key == "←" ) {
    Keyboard.press(KEY_LEFT_ARROW);
    Keyboard.release(KEY_LEFT_ARROW);
    //Serial.print("HID Key:");
    //Serial.println(key);
  } else if (key == "→" ) {
    Keyboard.press(KEY_RIGHT_ARROW);
    Keyboard.release(KEY_RIGHT_ARROW);
    //Serial.print("HID Key:");
    //Serial.println(key);
  } else if (key == "ESC" ) {
    Keyboard.press(KEY_ESC);
    Keyboard.release(' ');
  } else if( key.length() > 1 && key[0]=='F' ) {
    Keyboard.press(KEY_F1 + key.substring(1).toInt() );
    Keyboard.release(' ');
  }

   if (shift) {delay(100);Keyboard.release(KEY_LEFT_SHIFT);}
   if (alt) {delay(100);Keyboard.release(KEY_LEFT_ALT);}
   if (ctrl) {delay(100);Keyboard.release(KEY_LEFT_CTRL);}
}


void HIDTask(void* pv) {
  USB.begin();
  Keyboard.begin();
  Mouse.begin();

  HIDEvent ev;
  for (;;) {
    if (xQueueReceive(hidQueue, &ev, portMAX_DELAY)) {
      switch (ev.type) {
        case KEY:
          pressKey(ev.key, ev.shift, ev.ctrl, ev.alt);
          break;
        case MOUSE_MOVE:
          moveMouse(ev.dx, ev.dy);
          break;
        case MOUSE_CLICK:
          if (ev.key == "left")   Mouse.click(MOUSE_LEFT);
          if (ev.key == "middle") Mouse.click(MOUSE_MIDDLE);
          if (ev.key == "right")  Mouse.click(MOUSE_RIGHT);
          break;
      }
    }
  }
}
