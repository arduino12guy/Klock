/*
 * ██╗  ██╗██╗      ██████╗  ██████╗██╗  ██╗
 * ██║ ██╔╝██║     ██╔═══██╗██╔════╝██║ ██╔╝
 * █████╔╝ ██║     ██║   ██║██║     █████╔╝
 * ██╔═██╗ ██║     ██║   ██║██║     ██╔═██╗
 * ██║  ██╗███████╗╚██████╔╝╚██████╗██║  ██╗
 * ╚═╝  ╚═╝╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝
 *
 *  A minimal desk clock. Three modes. One button.
 *  ESP32 + 12-LED WS2812B ring + passive buzzer.
 */

// ─────────────────────────────────────────────
//  INCLUDES
// ─────────────────────────────────────────────
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <OneButton.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>

// ─────────────────────────────────────────────
//  PINS
// ─────────────────────────────────────────────
#define PIN_NEOPIXEL  5
#define PIN_BUTTON    15
#define PIN_BUZZER    18

// ─────────────────────────────────────────────
//  HARDWARE
// ─────────────────────────────────────────────
#define NUM_LEDS        12
#define LED_BRIGHTNESS  85

// ─────────────────────────────────────────────
//  NETWORK & TIME
// ─────────────────────────────────────────────
const char*  WIFI_SSID       = "YOUR_SSID";
const char*  WIFI_PASSWORD   = "YOUR_PASSWORD";
const char*  NTP_SERVER      = "pool.ntp.org";
const long   GMT_OFFSET_SEC  = 19800;
const int    DST_OFFSET_SEC  = 0;
const float  LOC_LATITUDE    =  22.3072f;
const float  LOC_LONGITUDE   =  73.1812f;

// ─────────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────────
#define CLOCK_REFRESH_MS    120UL
#define SOLAR_REFRESH_MS  20000UL
#define THEME_CHECK_MS    60000UL
#define GLOBAL_FADE_MS        8UL
#define GLOBAL_FADE_STEPS    28
#define WIPE_STEP_MS         36UL
#define PULSE_PERIOD_MS    2600UL
#define PULSE_REFRESH_MS     16UL
#define TIMER_FADE_MS         9UL
#define TIMER_FADE_STEPS     24
#define ALARM_FLASH_MS      160UL
#define ALARM_FLASH_COUNT     6

// ─────────────────────────────────────────────
//  THEMES
// ─────────────────────────────────────────────
struct Theme {
  uint32_t handHour;
  uint32_t handMinute;
  uint32_t handSecond;
  uint32_t field;
  uint32_t fieldAccent;
  uint32_t sun;
  uint32_t moon;
  uint32_t horizon;
  uint32_t timerIdle;
  uint32_t timerRun;
  uint32_t timerLow;
  uint32_t wipe;
  uint32_t alarm;
};

const Theme THEME_DAY = {
  0xF5C842,
  0xF08020,
  0xFFF5E0,
  0x000000,
  0x000000,
  0xFFD060,
  0xC0CCEC,
  0x5A3A06,
  0xF0A028,
  0xE07018,
  0xCC2800,
  0xC85A00,
  0xFF4010,
};

const Theme THEME_NIGHT = {
  0x4080FF,
  0x1E40D0,
  0xB0CCFF,
  0x000000,
  0x000000,
  0xFFD060,
  0x8AAAD8,
  0x050A1A,
  0x1A40C0,
  0x0C28C8,
  0x6000CC,
  0x180090,
  0x8000FF,
};

const Theme* theme     = &THEME_NIGHT;
bool         activeIsDay = false;

// ─────────────────────────────────────────────
//  MODES
// ─────────────────────────────────────────────
enum AppMode : uint8_t { MODE_CLOCK=0, MODE_SOLAR=1, MODE_TIMER=2, MODE_COUNT=3 };
AppMode currentMode = MODE_CLOCK;

// ─────────────────────────────────────────────
//  BUZZER
// ─────────────────────────────────────────────
struct Note { uint16_t freq; uint16_t dur; };

const Note MEL_MODE[]  = {{587,65},{740,65},{880,65},{1175,110},{0,25}};
const Note MEL_TICK[]  = {{1500,14},{0,8}};
const Note MEL_ALARM[] = {{987,160},{0,80},{987,160},{0,80},{1318,380},{0,40}};

const uint8_t LMEL_MODE=5, LMEL_TICK=2, LMEL_ALARM=6;

const Note*   buzMel = nullptr;
uint8_t       buzLen=0, buzIdx=0;
unsigned long buzMs=0;
bool          buzOn=false;

// ─────────────────────────────────────────────
//  SOLAR LAYOUT
// ─────────────────────────────────────────────
const uint8_t HRZ[6] = {3,4,5,6,7,8};
const uint8_t ARC[6] = {2,1,0,11,10,9};

// ─────────────────────────────────────────────
//  PIXEL ENGINE
// ─────────────────────────────────────────────
uint32_t      tgt[NUM_LEDS]      = {};
uint32_t      fromBuf[NUM_LEDS]  = {};
uint32_t      live[NUM_LEDS]     = {};
bool          gFadeActive        = false;
uint8_t       gFadeStep          = 0;
unsigned long gFadeMs            = 0;

uint32_t      tfTarget[NUM_LEDS] = {};
uint32_t      tfFrom[NUM_LEDS]   = {};
bool          tfActive           = false;
uint8_t       tfStep             = 0;
unsigned long tfMs               = 0;

// ─────────────────────────────────────────────
//  WIPE
// ─────────────────────────────────────────────
bool          wipeActive = false;
uint8_t       wipeStep   = 0;
unsigned long wipeMs     = 0;

// ─────────────────────────────────────────────
//  ALARM
// ─────────────────────────────────────────────
bool          alarmActive = false;
uint8_t       alarmFlash  = 0;
unsigned long alarmMs     = 0;
bool          alarmLit    = false;

// ─────────────────────────────────────────────
//  CLOCK STATE
// ─────────────────────────────────────────────
unsigned long lastClockMs         = 0;
uint32_t      prevClock[NUM_LEDS] = {};

// ─────────────────────────────────────────────
//  SOLAR STATE
// ─────────────────────────────────────────────
unsigned long lastSolarMs = 0;
bool          solarDirty  = true;

// ─────────────────────────────────────────────
//  TIMER STATE
// ─────────────────────────────────────────────
uint8_t       timerSetMins = 0;
bool          timerRunning = false;
unsigned long timerStartMs = 0;
uint32_t      timerTotalMs = 0;
uint8_t       timerLast    = 255;
unsigned long pulseMs      = 0;
unsigned long lastPulseMs  = 0;

// ─────────────────────────────────────────────
//  THEME STATE
// ─────────────────────────────────────────────
unsigned long lastThemeMs = 0;

// ─────────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────────
Adafruit_NeoPixel ring(NUM_LEDS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
OneButton         btn(PIN_BUTTON, true, true);

// ─────────────────────────────────────────────
//  PROTOTYPES
// ─────────────────────────────────────────────
void     cbClick();
void     cbDouble();
void     cbLong();
void     playMel(const Note* m, uint8_t len);
void     tickBuzzer();
uint32_t lerpC(uint32_t a, uint32_t b, uint8_t t);
uint32_t scaleC(uint32_t c, uint8_t s);
uint32_t addC(uint32_t a, uint32_t b);
void     pushTarget();
void     tickFade();
void     startTF(uint8_t newCount, bool low);
void     tickTF();
void     startWipe();
void     tickWipe();
void     tickAlarm();
void     renderClock();
void     renderSolar();
void     renderTimerIdle();
void     tickTimerPulse();
void     tickTimerCountdown();
bool     calcIsDay();
void     applyTheme();
int      dayOfYear(int y, int mo, int d);
float    sunEvent(int doy, bool rise);

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  ring.begin();
  ring.setBrightness(LED_BRIGHTNESS);
  ring.clear();
  ring.show();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  btn.attachClick(cbClick);
  btn.attachDoubleClick(cbDouble);
  btn.attachLongPressStart(cbLong);

  Serial.print("WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long ws = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-ws < 14000) {
    ring.fill(0x001400); ring.show(); delay(220);
    ring.clear();        ring.show(); delay(220);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK");
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    struct tm ti;
    unsigned long ns = millis();
    while (!getLocalTime(&ti) && millis()-ns < 9000) { delay(400); Serial.print('~'); }
    if (getLocalTime(&ti)) {
      char b[24];
      strftime(b, sizeof(b), "%H:%M:%S", &ti);
      Serial.println(b);
    }
  }

  applyTheme();

  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.ColorHSV((uint16_t)(i * (65536 / NUM_LEDS)), 160, 180));
    ring.show();
    delay(38);
  }
  for (int fade = 180; fade >= 0; fade -= 12) {
    ring.setBrightness(fade < 12 ? 0 : fade);
    ring.show();
    delay(28);
  }
  ring.setBrightness(LED_BRIGHTNESS);
  ring.clear();
  ring.show();
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  btn.tick();
  tickBuzzer();
  tickAlarm();

  unsigned long now = millis();

  if (now - lastThemeMs > THEME_CHECK_MS) {
    lastThemeMs = now;
    applyTheme();
  }

  if (wipeActive) { tickWipe(); return; }
  if (gFadeActive) tickFade();
  if (tfActive)    tickTF();

  switch (currentMode) {

    case MODE_CLOCK:
      if (now - lastClockMs >= CLOCK_REFRESH_MS) {
        lastClockMs = now;
        renderClock();
      }
      break;

    case MODE_SOLAR:
      if (solarDirty || now - lastSolarMs >= SOLAR_REFRESH_MS) {
        lastSolarMs = now;
        solarDirty  = false;
        renderSolar();
      }
      break;

    case MODE_TIMER:
      if (timerRunning) {
        tickTimerCountdown();
        if (tfActive) tickTF();
      } else {
        if (now - lastPulseMs >= PULSE_REFRESH_MS) {
          lastPulseMs = now;
          tickTimerPulse();
        }
      }
      break;

    default: break;
  }
}

// ─────────────────────────────────────────────
//  BUTTON CALLBACKS
// ─────────────────────────────────────────────
void cbClick() {
  playMel(MEL_TICK, LMEL_TICK);
  if (currentMode == MODE_TIMER && !timerRunning && timerSetMins < NUM_LEDS) {
    timerSetMins++;
    pulseMs = millis();
    renderTimerIdle();
  }
}

void cbDouble() {
  currentMode  = (AppMode)((currentMode + 1) % MODE_COUNT);
  timerSetMins = 0;
  timerRunning = false;
  alarmActive  = false;
  solarDirty   = true;
  playMel(MEL_MODE, LMEL_MODE);
  startWipe();
}

void cbLong() {
  if (currentMode == MODE_TIMER && !timerRunning && timerSetMins > 0) {
    timerRunning = true;
    timerTotalMs = (uint32_t)timerSetMins * 60000UL;
    timerStartMs = millis();
    timerLast    = timerSetMins;
    playMel(MEL_TICK, LMEL_TICK);
    startTF(timerSetMins, false);
  }
}

// ─────────────────────────────────────────────
//  BUZZER ENGINE
// ─────────────────────────────────────────────
void playMel(const Note* m, uint8_t len) {
  noTone(PIN_BUZZER);
  buzMel = m; buzLen = len; buzIdx = 0; buzMs = millis(); buzOn = true;
  if (m[0].freq) tone(PIN_BUZZER, m[0].freq);
}

void tickBuzzer() {
  if (!buzOn) return;
  if (millis() - buzMs < buzMel[buzIdx].dur) return;
  noTone(PIN_BUZZER);
  if (++buzIdx >= buzLen) { buzOn = false; return; }
  buzMs = millis();
  if (buzMel[buzIdx].freq) tone(PIN_BUZZER, buzMel[buzIdx].freq);
}

// ─────────────────────────────────────────────
//  COLOR HELPERS
// ─────────────────────────────────────────────
uint32_t lerpC(uint32_t a, uint32_t b, uint8_t t) {
  uint8_t ar=(a>>16)&0xFF, ag=(a>>8)&0xFF, ab=a&0xFF;
  uint8_t br=(b>>16)&0xFF, bg=(b>>8)&0xFF, bb=b&0xFF;
  return ((uint32_t)(ar + (((int)br-ar)*t>>8)) << 16)
       | ((uint32_t)(ag + (((int)bg-ag)*t>>8)) <<  8)
       |  (uint32_t)(ab + (((int)bb-ab)*t>>8));
}

uint32_t scaleC(uint32_t c, uint8_t s) {
  return (((uint32_t)(((c>>16)&0xFF)*s/255))<<16)
       | (((uint32_t)(((c>> 8)&0xFF)*s/255))<< 8)
       |  ((uint32_t)(( c     &0xFF)*s/255));
}

uint32_t addC(uint32_t a, uint32_t b) {
  uint16_t r=((a>>16)&0xFF)+((b>>16)&0xFF);
  uint16_t g=((a>> 8)&0xFF)+((b>> 8)&0xFF);
  uint16_t v=( a     &0xFF)+( b     &0xFF);
  return ((uint32_t)(r>255?255:r)<<16)|((uint32_t)(g>255?255:g)<<8)|(v>255?255:v);
}

// ─────────────────────────────────────────────
//  GLOBAL FADE ENGINE
// ─────────────────────────────────────────────
void pushTarget() {
  for (uint8_t i=0;i<NUM_LEDS;i++) fromBuf[i] = live[i];
  gFadeStep   = 0;
  gFadeActive = true;
  gFadeMs     = millis();
}

void tickFade() {
  if (!gFadeActive) return;
  if (millis() - gFadeMs < GLOBAL_FADE_MS) return;
  gFadeMs = millis();
  gFadeStep++;
  uint8_t t = (uint8_t)((gFadeStep * 255) / GLOBAL_FADE_STEPS);
  for (uint8_t i=0;i<NUM_LEDS;i++) {
    uint32_t c = lerpC(fromBuf[i], tgt[i], t);
    live[i] = c;
    ring.setPixelColor(i, c);
  }
  ring.show();
  if (gFadeStep >= GLOBAL_FADE_STEPS) {
    gFadeActive = false;
    for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = tgt[i];
  }
}

// ─────────────────────────────────────────────
//  TIMER FADE ENGINE
// ─────────────────────────────────────────────
void startTF(uint8_t newCount, bool low) {
  uint32_t col = low ? theme->timerLow : theme->timerRun;
  for (uint8_t i=0;i<NUM_LEDS;i++) {
    tfFrom[i]   = live[i];
    tfTarget[i] = i < newCount ? col : 0;
  }
  tfStep = 0; tfActive = true; tfMs = millis();
}

void tickTF() {
  if (!tfActive) return;
  if (millis() - tfMs < TIMER_FADE_MS) return;
  tfMs = millis();
  tfStep++;
  uint8_t t = (uint8_t)((tfStep * 255) / TIMER_FADE_STEPS);
  for (uint8_t i=0;i<NUM_LEDS;i++) {
    uint32_t c = lerpC(tfFrom[i], tfTarget[i], t);
    live[i] = c;
    ring.setPixelColor(i, c);
  }
  ring.show();
  if (tfStep >= TIMER_FADE_STEPS) {
    tfActive = false;
    for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = tfTarget[i];
  }
}

// ─────────────────────────────────────────────
//  MODE CHANGE WIPE
// ─────────────────────────────────────────────
void startWipe() {
  for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = 0;
  ring.clear(); ring.show();
  wipeStep = 0; wipeActive = true; wipeMs = millis();
}

void tickWipe() {
  if (millis() - wipeMs < WIPE_STEP_MS) return;
  wipeMs = millis();

  ring.clear();

  const uint8_t tail[4] = {240, 140, 60, 18};

  if (wipeStep < NUM_LEDS) {
    for (uint8_t t=0; t<4; t++) {
      int idx = (int)wipeStep - t;
      if (idx >= 0) ring.setPixelColor((uint8_t)idx, scaleC(theme->wipe, tail[t]));
    }
  } else {
    uint8_t eraseHead = wipeStep - NUM_LEDS;
    for (uint8_t t=0; t<4; t++) {
      int idx = (int)eraseHead - t;
      if (idx >= 0 && idx < NUM_LEDS)
        ring.setPixelColor((uint8_t)idx, scaleC(theme->wipe, tail[t]));
    }
  }

  ring.show();
  wipeStep++;

  if (wipeStep >= NUM_LEDS * 2 + 4) {
    wipeActive = false;
    ring.clear(); ring.show();
    for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = 0;

    lastClockMs = 0;
    solarDirty  = true;
    timerLast   = 255;
    pulseMs     = millis();

    if      (currentMode == MODE_TIMER) renderTimerIdle();
    else if (currentMode == MODE_CLOCK) renderClock();
    else if (currentMode == MODE_SOLAR) renderSolar();
  }
}

// ─────────────────────────────────────────────
//  ALARM FLASH
// ─────────────────────────────────────────────
void tickAlarm() {
  if (!alarmActive) return;
  if (millis() - alarmMs < ALARM_FLASH_MS) return;
  alarmMs  = millis();
  alarmLit = !alarmLit;
  alarmFlash++;

  if (alarmFlash > ALARM_FLASH_COUNT * 2) {
    alarmActive = false;
    ring.clear(); ring.show();
    for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = 0;
    return;
  }

  uint8_t  sc = alarmLit ? (uint8_t)(240 - alarmFlash * 16) : 0;
  uint32_t c  = scaleC(theme->alarm, sc);
  ring.fill(c);
  ring.show();
  for (uint8_t i=0;i<NUM_LEDS;i++) live[i] = c;
}

// ─────────────────────────────────────────────
//  MODE 1 — CLOCK
// ─────────────────────────────────────────────
void renderClock() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  int h  = ti.tm_hour % 12;
  int m  = ti.tm_min;
  int s  = ti.tm_sec;
  int ms = (int)(millis() % 1000);

  float hPos = fmod((float)h + (float)m / 60.0f,           12.0f);
  float mPos = fmod((float)m / 5.0f + (float)s / 300.0f,   12.0f);
  float sPos = fmod((float)s / 5.0f + (float)ms / 5000.0f, 12.0f);

  uint32_t acc[NUM_LEDS] = {};

  auto hand = [&](float pos, uint32_t col, uint8_t tailAmt) {
    uint8_t main = (uint8_t)pos % NUM_LEDS;
    uint8_t next = (main + 1) % NUM_LEDS;
    float   frac = pos - floorf(pos);
    uint8_t mSc  = (uint8_t)(255 - (uint8_t)(frac * 148));
    uint8_t nSc  = (uint8_t)(frac * tailAmt);
    acc[main] = addC(acc[main], scaleC(col, mSc));
    acc[next] = addC(acc[next], scaleC(col, nSc));
  };

  hand(hPos, theme->handHour,   128);
  hand(mPos, theme->handMinute, 108);
  hand(sPos, theme->handSecond,  88);

  bool changed = false;
  for (uint8_t i=0;i<NUM_LEDS;i++) {
    tgt[i] = acc[i] ? acc[i] : 0;
    if (tgt[i] != prevClock[i]) changed = true;
  }
  if (!changed) return;
  for (uint8_t i=0;i<NUM_LEDS;i++) prevClock[i] = tgt[i];

  pushTarget();
}

// ─────────────────────────────────────────────
//  MODE 2 — SOLAR / LUNAR
// ─────────────────────────────────────────────
void renderSolar() {
  struct tm ti;
  if (!getLocalTime(&ti)) return;

  int   doy  = dayOfYear(ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday);
  float rise = sunEvent(doy, true);
  float set  = sunEvent(doy, false);
  float now  = ti.tm_hour + ti.tm_min/60.0f + ti.tm_sec/3600.0f;
  bool  day  = (now >= rise && now <= set);

  for (uint8_t i=0;i<NUM_LEDS;i++) tgt[i] = 0;
  for (uint8_t i=0;i<6;i++)        tgt[HRZ[i]] = theme->horizon;

  float prog;
  if (day) {
    prog = (now - rise) / (set - rise);
  } else {
    float nl = 24.0f - (set - rise);
    prog = now > set ? (now - set) / nl : (24.0f - set + now) / nl;
  }
  prog = prog < 0 ? 0 : (prog > 1 ? 1 : prog);

  uint8_t  arc  = (uint8_t)roundf(prog * 5.0f);
  uint32_t body = day ? theme->sun : theme->moon;

  tgt[ARC[arc]] = body;
  if (arc > 0) tgt[ARC[arc-1]] = addC(tgt[ARC[arc-1]], scaleC(body, 48));
  if (arc < 5) tgt[ARC[arc+1]] = addC(tgt[ARC[arc+1]], scaleC(body, 48));

  pushTarget();
}

// ─────────────────────────────────────────────
//  MODE 3 — TIMER
// ─────────────────────────────────────────────
void renderTimerIdle() {
  for (uint8_t i=0;i<NUM_LEDS;i++) tgt[i] = i < timerSetMins ? theme->timerIdle : 0;
  pushTarget();
}

void tickTimerPulse() {
  if (timerSetMins == 0) return;
  float   phase  = (float)((millis() - pulseMs) % PULSE_PERIOD_MS) / (float)PULSE_PERIOD_MS;
  float   bright = 0.38f + 0.62f * (0.5f + 0.5f * sinf(phase * TWO_PI));
  uint8_t sc     = (uint8_t)(bright * 255.0f);
  for (uint8_t i=0;i<NUM_LEDS;i++) {
    uint32_t c = i < timerSetMins ? scaleC(theme->timerIdle, sc) : 0;
    live[i] = c;
    ring.setPixelColor(i, c);
  }
  ring.show();
}

void tickTimerCountdown() {
  if (tfActive) return;

  unsigned long elapsed = millis() - timerStartMs;

  if (elapsed >= timerTotalMs) {
    timerRunning = false;
    timerSetMins = 0;
    alarmActive  = true;
    alarmFlash   = 0;
    alarmLit     = false;
    alarmMs      = millis();
    playMel(MEL_ALARM, LMEL_ALARM);
    for (uint8_t i=0;i<NUM_LEDS;i++) { tgt[i]=0; live[i]=0; }
    ring.clear(); ring.show();
    return;
  }

  uint32_t rem      = timerTotalMs - elapsed;
  uint8_t  ledsNeed = (uint8_t)((rem + 59999UL) / 60000UL);
  if (ledsNeed > timerSetMins) ledsNeed = timerSetMins;
  if (ledsNeed == timerLast) return;
  timerLast = ledsNeed;

  startTF(ledsNeed, ledsNeed <= 3);
}

// ─────────────────────────────────────────────
//  DAY / NIGHT THEME
// ─────────────────────────────────────────────
bool calcIsDay() {
  struct tm ti;
  if (!getLocalTime(&ti)) return true;
  int   doy = dayOfYear(ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday);
  float r   = sunEvent(doy, true);
  float s   = sunEvent(doy, false);
  float now = ti.tm_hour + ti.tm_min/60.0f;
  return (now >= r && now <= s);
}

void applyTheme() {
  bool day = calcIsDay();
  if (day == activeIsDay) return;
  activeIsDay = day;
  theme       = day ? &THEME_DAY : &THEME_NIGHT;
  solarDirty  = true;
  Serial.println(day ? "Theme: DAY" : "Theme: NIGHT");
}

// ─────────────────────────────────────────────
//  SOLAR MATH
// ─────────────────────────────────────────────
int dayOfYear(int year, int month, int day) {
  const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (year%4==0) && ((year%100!=0)||(year%400==0));
  int doy = 0;
  for (int mo=0; mo<month-1; mo++) { doy += dim[mo]; if (mo==1 && leap) doy++; }
  return doy + day;
}

float sunEvent(int doy, bool rise) {
  float decl  = -23.45f * cosf(TWO_PI*(doy+10)/365.0f) * DEG_TO_RAD;
  float lat   = LOC_LATITUDE * DEG_TO_RAD;
  float cosH0 = -tanf(lat) * tanf(decl);
  cosH0 = cosH0 < -1 ? -1 : (cosH0 > 1 ? 1 : cosH0);
  float H0    = acosf(cosH0) * RAD_TO_DEG;
  float B     = TWO_PI * (doy-81) / 364.0f;
  float EoT   = 9.87f*sinf(2*B) - 7.53f*cosf(B) - 1.5f*sinf(B);
  float tzM   = roundf((float)GMT_OFFSET_SEC / 3600.0f) * 15.0f;
  float lon   = 4.0f * (LOC_LONGITUDE - tzM);
  float noon  = 12.0f - (EoT + lon) / 60.0f;
  float off   = H0 / 15.0f;
  return rise ? noon - off : noon + off;
}
