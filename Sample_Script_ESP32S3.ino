// Midea AC ESP32 Controller - Final Production Version
// Based on RG57A7/BGEF Protocol
// Features: Dynamic State Construction (Prevents Temp Reset)

#include <IRremote.hpp>

#define IR_SEND_PIN 14   

// --- TIMING CONSTANTS ---
const uint16_t MARK_US       = 560;
const uint16_t ONE_SPACE_US  = 1690;
const uint16_t ZERO_SPACE_US = 560;
const uint16_t LEAD_MARK_US  = 4350;
const uint16_t LEAD_SPACE_US = 4400;
const uint16_t FRAME_GAP_US  = 5200;

// --- VIRTUAL REMOTE STATE ---
// We initialize to: 24°C, Cool Mode, Auto Fan
int global_temp = 24; 
int global_mode = 0; // 0=Cool, 1=Auto, 3=Heat
int global_fan  = 0; // 0=Auto, 1=Low, 2=Med, 3=High

// --- LUT MAPPINGS ---
// Temperature bits (17C to 30C)
const uint8_t temp_bits[] = {
  0x00, 0x08, 0x0C, 0x04, 0x06, 0x0E, 0x0A, 0x02, // 17-24
  0x03, 0x0B, 0x09, 0x01, 0x05, 0x0D  // 25-30
};

#define BYTE2_FAN_AUTO 0xFD
#define BYTE2_FAN_LOW  0xF9
#define BYTE2_FAN_MED  0xFA
#define BYTE2_FAN_HIGH 0xFC
#define BYTE2_MODE_AUTO 0xF8 

// --- SPECIAL TEMPLATES ---
// These are toggle commands that don't use the standard state structure
struct Template5 { uint8_t b0,b1,b2,b3,b4; };
Template5 tmpl_led   = {0xAD, 0x52, 0xAF, 0x50, 0xA5}; 
Template5 tmpl_turbo = {0xAD, 0x52, 0xAF, 0x50, 0x45};
Template5 tmpl_swing = {0x4D, 0xB2, 0xD6, 0x29, 0x07}; 

// --- CHECKSUM CALCULATOR ---
uint8_t compute_checksum(const uint8_t bytes5[5]) {
  uint16_t s = 0;
  for (int i = 0; i < 5; ++i) s += bytes5[i];
  return (uint8_t)((0xFD - s) & 0xFF);
}

// --- BUILD RAW IR TIMINGS ---
size_t build_raw_from_6bytes(const uint8_t bytes[6], uint16_t *outBuf, size_t outBufSize) {
  size_t idx = 0;
  auto push = [&](uint16_t v)->bool {
    if (idx >= outBufSize) return false;
    outBuf[idx++] = v;
    return true;
  };

  // Frame 1
  push(LEAD_MARK_US); push(LEAD_SPACE_US);
  for (int b = 0; b < 6; ++b) {
    for (int bit = 0; bit < 8; ++bit) {
      push(MARK_US);
      if (bytes[b] & (1 << bit)) push(ONE_SPACE_US);
      else push(ZERO_SPACE_US);
    }
  }
  push(MARK_US); push(FRAME_GAP_US);

  // Frame 2 (Repeat)
  push(LEAD_MARK_US); push(LEAD_SPACE_US);
  for (int b = 0; b < 6; ++b) {
    for (int bit = 0; bit < 8; ++bit) {
      push(MARK_US);
      if (bytes[b] & (1 << bit)) push(ONE_SPACE_US);
      else push(ZERO_SPACE_US);
    }
  }
  push(MARK_US);
  return idx;
}

// --- MAIN SEND FUNCTION ---
void updateAC() {
  uint8_t bytes[6];
  bytes[0] = 0x4D;
  bytes[1] = 0xB2;

  // 1. Set Byte 2 (Fan/Mode Base)
  if (global_fan == 0) bytes[2] = BYTE2_FAN_AUTO;
  else if (global_fan == 1) bytes[2] = BYTE2_FAN_LOW;
  else if (global_fan == 2) bytes[2] = BYTE2_FAN_MED;
  else if (global_fan == 3) bytes[2] = BYTE2_FAN_HIGH;

  // If Mode is Auto, Byte 2 is forced to Auto Mode signature
  if (global_mode == 1) bytes[2] = BYTE2_MODE_AUTO;

  // 2. Set Byte 3 (Inverse of Byte 2)
  bytes[3] = ~bytes[2]; 

  // 3. Set Byte 4 (Mode Nibble + Temp Nibble)
  uint8_t tempIndex = global_temp - 17; 
  if (tempIndex < 0) tempIndex = 0;
  if (tempIndex > 13) tempIndex = 13;
  
  uint8_t lowerNibble = temp_bits[tempIndex];
  uint8_t upperNibble = 0;
  
  if (global_mode == 0) upperNibble = 0x0;      // Cool
  else if (global_mode == 1) upperNibble = 0x1; // Auto
  else if (global_mode == 3) upperNibble = 0x3; // Heat

  bytes[4] = (upperNibble << 4) | lowerNibble;

  // 4. Checksum
  bytes[5] = compute_checksum(bytes);

  // 5. Send
  static uint16_t rawBuf[300];
  size_t len = build_raw_from_6bytes(bytes, rawBuf, 300);
  IrSender.sendRaw(rawBuf, len, 38);

  // 6. User Feedback
  Serial.print(F("SENT -> Mode:"));
  if(global_mode==0) Serial.print(F("Cool"));
  if(global_mode==1) Serial.print(F("Auto"));
  if(global_mode==3) Serial.print(F("Heat"));
  
  Serial.print(F(" | Temp:")); Serial.print(global_temp);
  
  Serial.print(F(" | Fan:"));
  if(global_mode == 1) Serial.println(F("Auto (Locked)"));
  else {
    if(global_fan==0) Serial.println(F("Auto"));
    if(global_fan==1) Serial.println(F("Low"));
    if(global_fan==2) Serial.println(F("Med"));
    if(global_fan==3) Serial.println(F("High"));
  }
}

// Helper for templates (LED, Turbo, Swing)
void send_special(const Template5 &t5, String name) {
  uint8_t bytes[6] = { t5.b0, t5.b1, t5.b2, t5.b3, t5.b4, 0x00 };
  bytes[5] = compute_checksum(bytes);
  static uint16_t rawBuf[300];
  size_t len = build_raw_from_6bytes(bytes, rawBuf, 300);
  IrSender.sendRaw(rawBuf, len, 38);
  Serial.print(F("SENT SPECIAL -> ")); Serial.println(name);
}

void handleCommand(String s) {
  s.trim(); s.toLowerCase();
  if (s.length() == 0) return;

  // Temp Control
  if (s.startsWith("t") && s.length() > 1) {
    int t = s.substring(1).toInt();
    if (t >= 17 && t <= 30) {
      global_temp = t;
      updateAC();
    } else {
      Serial.println(F("Error: Temp must be 17-30"));
    }
  }
  
  // Modes
  else if (s == "mc") { global_mode = 0; updateAC(); }
  else if (s == "mh") { global_mode = 3; updateAC(); }
  else if (s == "ma") { 
    global_mode = 1; 
    global_fan = 0; // Reset fan to auto for consistency
    updateAC(); 
  }

  // Fans
  else if (s == "fa") { global_fan = 0; updateAC(); }
  else if (s == "fl") { global_fan = 1; updateAC(); }
  else if (s == "fm") { global_fan = 2; updateAC(); }
  else if (s == "fh") { global_fan = 3; updateAC(); }

  // Power (Send current state to turn ON)
  else if (s == "pon") { 
    Serial.println(F("Power ON..."));
    updateAC(); 
  }
  // Power OFF (Hardcoded Template)
  else if (s == "poff") {
    Template5 off = {0x4D, 0xB2, 0xDE, 0x21, 0x07};
    send_special(off, "Power OFF");
  }

  // Extras
  else if (s == "led") { send_special(tmpl_led, "LED Toggle"); }
  else if (s == "boost") { send_special(tmpl_turbo, "Turbo Toggle"); }
  else if (s == "swg") { send_special(tmpl_swing, "Swing Toggle"); }
  
  else {
    Serial.println(F("Unknown Cmd. Try: pon, poff, t24, mc, ma, mh, fl, fa, led"));
  }
}

void setup() {
  Serial.begin(115200);
  IrSender.begin(IR_SEND_PIN);
  Serial.println(F("Midea AC Remote (Final) - Ready"));
  Serial.println(F("---------------------------------"));
}

void loop() {
  if (Serial.available()) {
    handleCommand(Serial.readStringUntil('\n'));
  }
}