#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

const char* ssid = "myssid";
const char* password = "mypasswd";
const char* mDNSName = "espKB";

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

// ======== HTML + JS ========
const String index_html PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>ESP32-S3 HID Keyboard + Mouse</title>
<style>
body {
  background:#111;
  color:#fff;
  margin:0;
  font-family:sans-serif;
  display:flex;
  flex-direction:column;
  align-items:center;
}
h2 {
  margin:10px;
  text-align:center;
  font-size:clamp(1rem,4vw,2rem);
}
#keyboard {
  display:grid;
  gap:0.3em;
  width:98vw;
  max-width:1000px;
  margin-top:10px;
}
.mouse-btn {
  flex: 1;
  background: #333;
  color: #fff;
  text-align: center;
  padding: 0.8em 0;
  border-radius: 0.5em;
  cursor: pointer;
  user-select: none;
  font-size: clamp(0.8rem, 2vw, 1rem);
}
.mouse-btn:active {
  background: #0a0;
}
.row {
  display:grid;
  grid-template-columns: repeat(auto-fit, minmax(2.5em, 1fr));
  gap:0.3em;
}
.key {
  background:#333;
  border-radius:0.6em;
  text-align:center;
  cursor:pointer;
  user-select:none;
  font-size:clamp(0.7rem,1.5vw,1.2rem);
  display:flex;
  justify-content:center;
  align-items:center;
  aspect-ratio: 1.5 / 1;
}
.key.space {
  grid-column: span 5;
  aspect-ratio: 6 / 1;
  padding:0.4em 0;
}
.key.active { background:#0a0; }

#touchpad-container {
  position:relative;
  width:98vw;
  max-width:1000px;
  height:25vh;
  background:#222;
  border:2px solid #555;
  border-radius:10px;
  margin:2vh 0;
  touch-action:none;
  overflow:hidden;
}

#cursor {
  position:absolute;
  width:15px;
  height:15px;
  background:#0f0;
  border-radius:50%;
  pointer-events:none;
  top:50%;
  left:50%;
  transform:translate(-50%,-50%);
}
</style>
</head>
<body>
<h2>ESP32-S3 HID Keyboard + Mouse</h2>

<!-- Keyboard -->
<div id="keyboard">
  <div class="row" id="rowF"></div>
  <div class="row" id="rowNum"></div>
  <div class="row" id="row1"></div>
  <div class="row" id="row2"></div>
  <div class="row" id="row3"></div>
  <div class="row" id="row4"></div>
</div>

<!-- Touchpad -->
<div id="touchpad-container"><div id="cursor"></div></div>

<div id="mouse-buttons" style="display:flex; gap:1em; margin-bottom:2vh;">
  <div class="mouse-btn" data-button="left">Left</div>
  <div class="mouse-btn" data-button="middle">Middle</div>
  <div class="mouse-btn" data-button="right">Right</div>
</div>

<script>
// Keyboard layout using HTML-safe entities
// Use actual arrow/backspace characters in JS (safe in modern browsers)
const rows = {
  rowF:["Esc","F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"],
  rowNum:["`","1","2","3","4","5","6","7","8","9","0","-","=","⌫"], // backspace
  row1:["Tab","Q","W","E","R","T","Y","U","I","O","P","[","]","\\"],
  row2:["Caps","A","S","D","F","G","H","J","K","L",";","'","Enter"],
  row3:["Shift","Z","X","C","V","B","N","M",",",".","/"],
  row4:["Ctrl","Alt","Space","←","↑","↓","→"] // arrows as characters
};

// Shifted symbols
const shiftedSymbols = {
  "`":"~","1":"!","2":"@","3":"#","4":"$","5":"%","6":"^","7":"&","8":"*","9":"(","0":")",
  "-":"_","=":"+","[":"{","]":"}","\\":"|",";":":","'":"\"",
  ",":"<",".":">","/":"?"
};

let caps=false, shift=false, alt=false, ctrl=false;

// Create keyboard
function createKeyboard(){
  for(const row in rows){
    const container=document.getElementById(row);
    rows[row].forEach(k=>{
      const div=document.createElement("div");
      div.classList.add("key");
      if(k==="Space") div.classList.add("space");
      div.textContent = k; // just use textContent

      div.dataset.key = k.replace(/&[a-z#0-9]+;/gi, ""); // store raw key for fetch
      div.onclick = () => handleKeyClick(k);
      container.appendChild(div);
    });
  }
  updateKeyLabels();
}

// Update key labels (shift/caps)
function updateKeyLabels() {
  document.querySelectorAll(".key").forEach(el => {
    const k = el.dataset.key;
    if(k.length===1 && /[a-zA-Z]/.test(k)){
      if((caps && !shift) || (!caps && shift)) el.textContent = k.toUpperCase();
      else el.textContent = k.toLowerCase();
    } else if(shift && shiftedSymbols[k]) el.textContent = shiftedSymbols[k];
    else if(k==="←"||k==="→"||k==="↓"||k==="⌫") el.innerHTML = el.innerHTML; // preserve entities
    else el.textContent = k;
  });
}

// Handle key press
function handleKeyClick(k){
  const keyText = k.replace(/&[a-z#0-9]+;/gi, ""); // strip HTML entities for sending
  const keyDiv = Array.from(document.querySelectorAll(".key"))
                       .find(el => el.dataset.key.toLowerCase() === keyText.toLowerCase());

  // Flash the key
  if(keyDiv){
    keyDiv.classList.add("active");
    setTimeout(() => keyDiv.classList.remove("active"), 150); // flash duration
  }

  if(keyText==="Shift"){shift=!shift;toggleActive("Shift",shift);updateKeyLabels();return;}
  if(keyText==="Caps"){caps=!caps;toggleActive("Caps",caps);updateKeyLabels();return;}
  if(keyText==="Alt"){alt=!alt;toggleActive("Alt",alt);return;}
  if(keyText==="Ctrl"){ctrl=!ctrl;toggleActive("Ctrl",ctrl);return;}

  let outKey=keyText;
  if(keyText.length===1){
    if(shift && shiftedSymbols[keyText]) outKey=shiftedSymbols[keyText];
    else if(/[a-z]/.test(keyText)){
      if((caps&&!shift)||(!caps&&shift)) outKey=keyText.toUpperCase();
      else outKey=keyText.toLowerCase();
    }
  }

  fetch(`/press?key=${encodeURIComponent(outKey)}&shift=${shift}&ctrl=${ctrl}&alt=${alt}`)
    .catch(()=>{});

  if(shift){
    shift=false;
    toggleActive("Shift",false);
    updateKeyLabels();
  }
}


function toggleActive(key,state){
  document.querySelectorAll(".key").forEach(el=>{
    if(el.dataset.key.toLowerCase()===key.toLowerCase())
      el.classList.toggle("active",state);
  });
}

// Touchpad logic
const pad=document.getElementById("touchpad-container");
const cursor=document.getElementById("cursor");
let cursorX=50, cursorY=50, isDown=false, lastX=0, lastY=0;

function updateCursor(){
  cursor.style.left=cursorX+"%";
  cursor.style.top=cursorY+"%";
}

// Handle both touch and mouse
function handleMove(dx,dy){
  fetch(`/move?dx=${dx}&dy=${dy}`).catch(()=>{});
  const rect=pad.getBoundingClientRect();
  cursorX += dx/rect.width*100;
  cursorY += dy/rect.height*100;
  cursorX = Math.max(0,Math.min(100,cursorX));
  cursorY = Math.max(0,Math.min(100,cursorY));
  updateCursor();
}

// Touch events
pad.addEventListener("touchstart",e=>{
  const t=e.touches[0];
  lastX=t.clientX; lastY=t.clientY;
});
pad.addEventListener("touchmove",e=>{
  const t=e.touches[0];
  const dx=t.clientX-lastX;
  const dy=t.clientY-lastY;
  lastX=t.clientX; lastY=t.clientY;
  handleMove(dx,dy);
});

function recenterCursor(){
  cursorX = 50;
  cursorY = 50;
  updateCursor();
}

// Touch release
pad.addEventListener("touchend",()=>{
  fetch("/click").catch(()=>{});
  recenterCursor();
});

// Mouse release
pad.addEventListener("mouseup",()=>{
  isDown=false;
  fetch("/click").catch(()=>{});
  recenterCursor();
});

// Mouse support
pad.addEventListener("mousedown",e=>{isDown=true;lastX=e.clientX;lastY=e.clientY;});
pad.addEventListener("mousemove",e=>{
  if(!isDown)return;
  const dx=e.clientX-lastX;
  const dy=e.clientY-lastY;
  lastX=e.clientX; lastY=e.clientY;
  handleMove(dx,dy);
});
document.querySelectorAll(".mouse-btn").forEach(btn=>{
  btn.addEventListener("click", ()=>{
    const button = btn.dataset.button; // left/middle/right
    fetch(`/click?button=${button}`).catch(()=>{});
  });
});

// Init
createKeyboard();
updateCursor();
</script>
</body>
</html>)rawliteral";

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
  if(ctrl) Keyboard.press(KEY_LEFT_CTRL);
  if(alt) Keyboard.press(KEY_LEFT_ALT);
  if(shift) Keyboard.press(KEY_LEFT_SHIFT);

  if(key=="Space") Keyboard.write(' ');
  else if(key=="Enter") Keyboard.write(KEY_RETURN);
  else if(key=="⌫") Keyboard.write(KEY_BACKSPACE);
  else if(key=="Tab") Keyboard.write(KEY_TAB);
  else if(key=="↑") Keyboard.write(KEY_UP_ARROW);
  else if(key=="↓") Keyboard.write(KEY_DOWN_ARROW);
  else if(key=="←") Keyboard.write(KEY_LEFT_ARROW);
  else if(key=="→") Keyboard.write(KEY_RIGHT_ARROW);
  else if(key=="Esc") Keyboard.write(KEY_ESC);
  else if(key.startsWith("F")){
    int fn=key.substring(1).toInt();
    if(fn>=1 && fn<=12) Keyboard.press(KEY_F1+fn-1);
  }
  else if(key.length()==1) Keyboard.write(key[0]);
  delay(20);
  Keyboard.releaseAll();
}

// ======== Web Server (Core 1) ========
void core1Task(void* pv){
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED) delay(500);
  Serial.println(WiFi.localIP());

  MDNS.begin(mDNSName);

  server.on("/",HTTP_GET,[](AsyncWebServerRequest* r){ r->send_P(200,"text/html",index_html.c_str()); });

  server.on("/press",HTTP_GET,[](AsyncWebServerRequest* r){
    if(r->hasParam("key")){
      HIDEvent ev;
      ev.type=KEY;
      ev.key=r->getParam("key")->value();
      ev.shift=r->hasParam("shift") && r->getParam("shift")->value()=="true";
      ev.ctrl=r->hasParam("ctrl") && r->getParam("ctrl")->value()=="true";
      ev.alt=r->hasParam("alt") && r->getParam("alt")->value()=="true";
      xQueueSend(hidQueue,&ev,0);;
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
