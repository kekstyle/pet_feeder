/*
  Distributeur avec double‑clic OK pour "dose immédiate"
  + DEBUG console série

  Matériel:
  - Arduino Uno ou Mega 2560
  - LCD I2C 16x2 (adresse 0x27 par défaut)
  - 4 boutons sur D2..D5 en INPUT_PULLUP (actifs à LOW)
  - RTC (DS3231/DS1307 via RTClib) facultative
  - (Option) DRV8825: STEP=D6, DIR=D7, EN=D8

  Fonctionnalités:
  - Ecran principal: heure courante + heure de distribution
  - Menu: régler horloge, distribution, dose (tours), vitesse (RPM), infos
  - Double‑clic OK (depuis HOME) = distribution immédiate
*/

struct Now { int h, m, s; unsigned long day; };

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <EEPROM.h>  // ====== AJOUT EEPROM ======

// ====== DEBUG (compatible AVR, sans Serial.printf) ======
#define DEBUG 1
#if DEBUG
  #include <stdarg.h>
  #include <stdio.h>

  static void dbgPrintf(const char* fmt, ...) {
    char buf[128];                // augmente si besoin
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);            // sortie série
  }

  #define DBG_BEGIN()    Serial.begin(115200)        // sur Uno, while(!Serial) n'est pas nécessaire
  #define DBG(...)       dbgPrintf(__VA_ARGS__)
  #define DBGLN(x)       Serial.println(x)
#else
  #define DBG_BEGIN()
  #define DBG(...)
  #define DBGLN(x)
#endif


// ====== CONFIG MOTEUR ======
#define USE_DRV8825 1          // 1=activer DRV8825, 0=simulation
const int PIN_STEP = 6;
const int PIN_DIR  = 7;
const int PIN_EN   = 8;
const int STEPS_PER_REV = 200; // adapter si micro‑pas

// ====== Sécurité moteur (option nFAULT) ======
// Décommente / adapte si nFAULT est câblé. Sinon, laisse commenté.
// #define FAULT_PIN 9   // DRV8825 nFAULT (actif LOW)
volatile bool gMotionAborted = false;  // flag: dernier mouvement abandonné

// ====== LCD ======
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// ====== Boutons ======
const uint8_t BTN_OK   = 2;
const uint8_t BTN_MENU = 3;
const uint8_t BTN_UP   = 4;
const uint8_t BTN_DOWN = 5;

// Anti‑rebond
unsigned long lastDebounceMs = 0;
const unsigned long DEBOUNCE_MS = 40;

// Taille du buffer d’états selon la carte
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
  const uint8_t MAX_PINS = 70; // Mega 2560
#else
  const uint8_t MAX_PINS = 14; // Uno
#endif
uint8_t lastBtnLevel[MAX_PINS];

// front descendant + anti‑rebond simple
bool btnPressed(uint8_t pin) {
  if (pin >= MAX_PINS) return false;
  if (millis() - lastDebounceMs < DEBOUNCE_MS) return false;
  uint8_t s = digitalRead(pin);
  bool pressed = (lastBtnLevel[pin] == HIGH && s == LOW);
  if (pressed) {
    lastDebounceMs = millis();
    DBG("[BTN] pin %u pressed at %lu ms\n", pin, lastDebounceMs);
  }
  lastBtnLevel[pin] = s;
  return pressed;
}

// ====== Horloge (RTC facultative) ======
RTC_DS3231 rtc;
bool rtcAvailable = false;

unsigned long softEpochMs = 0; // millis() au dernier set
unsigned long softEpochS  = 0; // secondes "logicielles" (0..86399)
unsigned long softDay     = 0; // compteur de jours

void softClockSet(int h, int m, int s) {
  softEpochS  = (unsigned long)h * 3600UL + (unsigned long)m * 60UL + (unsigned long)s;
  softEpochMs = millis();
  DBG("[CLOCK] Soft clock set to %02d:%02d:%02d at %lu ms\n", h, m, s, softEpochMs);
}

Now getNow() {
  Now n;
  if (rtcAvailable) {
    DateTime dt = rtc.now();
    n.h = dt.hour(); n.m = dt.minute(); n.s = dt.second();
    n.day = (unsigned long)(dt.unixtime() / 86400UL);
  } else {
    unsigned long elapsedS = (millis() - softEpochMs) / 1000UL;
    unsigned long total = (softEpochS + elapsedS);
    unsigned long totDayS = total % 86400UL;
    n.h = (int)(totDayS / 3600UL);
    n.m = (int)((totDayS % 3600UL) / 60UL);
    n.s = (int)(totDayS % 60UL);
    n.day = softDay + total / 86400UL;
  }
  return n;
}

// ====== UI ======
enum UiState { UI_HOME, UI_MENU, UI_SET_CLOCK, UI_SET_FEED, UI_SET_DOSE, UI_SET_SPEED, UI_INFO };
UiState ui = UI_HOME;

// Réglages utilisateur
int   feedH     = 8, feedM = 0;
float doseTurns = 1.0f;
int   maxRPM    = 60;

// ====== PERSISTANCE EEPROM (ajout) ======
struct Settings {
  uint16_t magic;      // 0xA55A
  uint8_t  version;    // 1
  int8_t   feedH;      // 0..23
  int8_t   feedM;      // 0..59
  float    doseTurns;  // ex: 1.0
  int16_t  maxRPM;     // ex: 60
  uint8_t  reserved[7];
  uint8_t  crc;        // CRC8 sur tout sauf 'crc'
};

static Settings G;
static bool gDirty = false;
const uint16_t SETTINGS_MAGIC = 0xA55A;
const uint8_t  SETTINGS_VER   = 1;
const int      SETTINGS_ADDR  = 0;

static uint8_t crc8(const uint8_t* d, size_t n){
  uint8_t c=0;
  for(size_t i=0;i<n;i++){
    c ^= d[i];
    for(uint8_t b=0;b<8;b++)
      c = (c & 0x80) ? (uint8_t)((c<<1) ^ 0x07) : (uint8_t)(c<<1);
  }
  return c;
}
static void settingsDefaults(){
  memset(&G, 0, sizeof(G));
  G.magic = SETTINGS_MAGIC;
  G.version = SETTINGS_VER;
  G.feedH = 8; G.feedM = 0;
  G.doseTurns = 1.0f;
  G.maxRPM = 60;
}
static bool settingsValidInEeprom(){
  Settings tmp;
  for (size_t i=0;i<sizeof(Settings);++i)
    ((uint8_t*)&tmp)[i] = EEPROM.read(SETTINGS_ADDR+i);
  if (tmp.magic != SETTINGS_MAGIC || tmp.version != SETTINGS_VER) return false;
  uint8_t expect = crc8((uint8_t*)&tmp, sizeof(Settings)-1);
  return (expect == tmp.crc);
}
static void eepromWriteBlock(int addr, const uint8_t* p, size_t n){
  for(size_t i=0;i<n;i++) EEPROM.update(addr+i, p[i]);
}
static void settingsLoad(){
  if (!settingsValidInEeprom()){
    settingsDefaults();
    G.crc = crc8((uint8_t*)&G, sizeof(Settings)-1);
    eepromWriteBlock(SETTINGS_ADDR, (uint8_t*)&G, sizeof(Settings));
    DBGLN("[EEPROM] defaults written");
  } else {
    for (size_t i=0;i<sizeof(Settings);++i)
      ((uint8_t*)&G)[i] = EEPROM.read(SETTINGS_ADDR+i);
    DBGLN("[EEPROM] loaded OK");
  }
  gDirty = false;
}
static void settingsSave(){
  G.magic = SETTINGS_MAGIC;
  G.version = SETTINGS_VER;
  G.crc = crc8((uint8_t*)&G, sizeof(Settings)-1);
  eepromWriteBlock(SETTINGS_ADDR, (uint8_t*)&G, sizeof(Settings));
  gDirty = false;
  DBGLN("[EEPROM] saved");
}
static void settingsMarkDirty(){ gDirty = true; }

// Menu
struct MenuItem { const char* label; UiState target; };
MenuItem MENU[] = {
  { "Regler horloge", UI_SET_CLOCK },
  { "Heure distrib",  UI_SET_FEED  },
  { "Dose (tours)",   UI_SET_DOSE  },
  { "Vitesse (rpm)",  UI_SET_SPEED },
  { "Infos",          UI_INFO      }
};
const int MENU_COUNT = sizeof(MENU)/sizeof(MENU[0]);
int menuSel = 0;
int menuTop = 0; // premier item visible (2 lignes)

// ====== Helpers LCD ======
void lcdPrintPadded(const char* s) {
  int n = 0; while (s[n] && n < LCD_COLS) n++;
  lcd.print(s);
  for (int i = n; i < LCD_COLS; ++i) lcd.print(' ');
}
void lcdPrintLine(char prefix, const char* text) {
  char buf[17];
  buf[0] = prefix;
  int i = 0;
  while (i < 15 && text[i]) { buf[1 + i] = text[i]; i++; }
  while (i < 15) { buf[1 + i] = ' '; i++; }
  buf[16] = '\0';
  lcd.print(buf);
}
void lcdShow2(const char* l1, const char* l2) {
  lcd.setCursor(0,0); lcdPrintPadded(l1);
  lcd.setCursor(0,1); lcdPrintPadded(l2);
}
void drawMenu() {
  if (menuSel < menuTop) menuTop = menuSel;
  if (menuSel > menuTop + 1) menuTop = menuSel - 1;
  lcd.setCursor(0,0); lcdPrintLine(menuSel == menuTop ? '>' : ' ', MENU[menuTop].label);
  lcd.setCursor(0,1);
  int second = menuTop + 1;
  if (second < MENU_COUNT) lcdPrintLine(menuSel == second ? '>' : ' ', MENU[second].label);
  else lcdPrintPadded("");
  DBG("[UI] MENU draw: sel=%d top=%d\n", menuSel, menuTop);
}

// ====== Ecran principal ======
void showHome(bool forceClear=false) {
  static unsigned long lastRefresh = 0;
  unsigned long nowMs = millis();
  if (forceClear) { lcd.clear(); lastRefresh = 0; }
  if (nowMs - lastRefresh >= 200) { // 5x/s
    Now n = getNow();
    char l1[17], l2[17];
    snprintf(l1, sizeof(l1), "Heure %02d:%02d:%02d", n.h, n.m, n.s);
    snprintf(l2, sizeof(l2), "Distrib %02d:%02d",   feedH, feedM);
    lcd.setCursor(0,0); lcdPrintPadded(l1);
    lcd.setCursor(0,1); lcdPrintPadded(l2);
    lastRefresh = nowMs;
  }
}

// ====== Réglages écrans ======
void handleSetClock() {
  static bool init = false;
  static int hh=12, mm=0;
  if (!init) {
    lcd.clear();
    Now n = getNow(); hh = n.h; mm = n.m;
    init = true;
    DBG("[UI] Enter SET_CLOCK (start %02d:%02d)\n", hh, mm);
  }

  lcd.setCursor(0,0); lcdPrintPadded("Regler horloge");
  char l2[17]; snprintf(l2, sizeof(l2), "HH:MM  %02d:%02d", hh, mm);
  lcd.setCursor(0,1); lcdPrintPadded(l2);

  if (btnPressed(BTN_UP))   { hh = (hh + 1) % 24; DBG("[SET_CLOCK] hh=%d\n", hh); }
  if (btnPressed(BTN_DOWN)) { hh = (hh + 23) % 24; DBG("[SET_CLOCK] hh=%d\n", hh); }
  if (btnPressed(BTN_OK))   { mm = (mm + 1) % 60;  DBG("[SET_CLOCK] mm=%d\n", mm); }
  if (btnPressed(BTN_MENU)) {
    if (rtcAvailable) {
      rtc.adjust(DateTime(2024,1,1, hh, mm, 0));
      DBG("[RTC] adjust -> 2024-01-01 %02d:%02d:00\n", hh, mm);
    } else {
      softClockSet(hh, mm, 0);
    }
    init = false; ui = UI_HOME; lcd.clear();
    DBGLN("[UI] Exit SET_CLOCK -> HOME");
  }
}

void handleSetFeed() {
  static bool init = false;
  if (!init) { lcd.clear(); init = true; DBG("[UI] Enter SET_FEED\n"); }
  lcd.setCursor(0,0); lcdPrintPadded("Heure distrib");
  char l2[17]; snprintf(l2, sizeof(l2), "%02d:%02d  UP/DN OK+", feedH, feedM);
  lcd.setCursor(0,1); lcdPrintPadded(l2);

  if (btnPressed(BTN_UP))   { feedH = (feedH + 1) % 24; DBG("[SET_FEED] H=%d\n", feedH); G.feedH = feedH; G.feedM = feedM; settingsMarkDirty(); }
  if (btnPressed(BTN_DOWN)) { feedH = (feedH + 23) % 24; DBG("[SET_FEED] H=%d\n", feedH); G.feedH = feedH; G.feedM = feedM; settingsMarkDirty(); }
  if (btnPressed(BTN_OK))   { feedM = (feedM + 1) % 60;  DBG("[SET_FEED] M=%d\n", feedM); G.feedH = feedH; G.feedM = feedM; settingsMarkDirty(); }
  if (btnPressed(BTN_MENU)) { 
    if (gDirty) settingsSave(); 
    init = false; ui = UI_HOME; lcd.clear(); DBGLN("[UI] Exit SET_FEED -> HOME"); 
  }
}

void handleSetDose(){
  static bool first=true;
  if(first){ lcd.clear(); first=false; }

  if (btnPressed(BTN_UP))   { doseTurns += 0.1f; G.doseTurns = doseTurns; settingsMarkDirty(); }
  if (btnPressed(BTN_DOWN)) { doseTurns = max(0.1f, doseTurns - 0.1f); G.doseTurns = doseTurns; settingsMarkDirty(); }
  if (btnPressed(BTN_OK))   { if (gDirty) settingsSave(); ui = UI_HOME; lcd.clear(); showHome(true); first=true; return; }
  if (btnPressed(BTN_MENU)) { if (gDirty) settingsSave(); ui = UI_MENU; lcd.clear(); drawMenu();    first=true; return; }

  // Affichage (sans printf)
  lcd.setCursor(0,0); lcd.print("Dose/tour:");
  lcd.setCursor(0,1); 
  lcd.print("= ");
  lcd.print(doseTurns, 1);
  lcd.print(" tr");
}

void handleSetSpeed() {
  static bool init = false;
  if (!init) { lcd.clear(); init = true; DBGLN("[UI] Enter SET_SPEED"); }
  lcd.setCursor(0,0); lcdPrintPadded("Vitesse (rpm)");
  char l2[17]; snprintf(l2, sizeof(l2), "%d  UP/DN", maxRPM);
  lcd.setCursor(0,1); lcdPrintPadded(l2);

  if (btnPressed(BTN_UP))   { if (maxRPM < 200) maxRPM++; DBG("[SET_SPEED] rpm=%d\n", maxRPM); G.maxRPM = maxRPM; settingsMarkDirty(); }
  if (btnPressed(BTN_DOWN)) { if (maxRPM > 5)   maxRPM--; DBG("[SET_SPEED] rpm=%d\n", maxRPM); G.maxRPM = maxRPM; settingsMarkDirty(); }
  if (btnPressed(BTN_MENU)) { if (gDirty) settingsSave(); init = false; ui = UI_HOME; lcd.clear(); DBGLN("[UI] Exit SET_SPEED -> HOME"); }
}

void handleInfo() {
  static bool init = false;
  static unsigned long t0 = 0;
  static unsigned long tAlt = 0;   // minuteur d’alternance
  static bool showDose = true;     // true: afficher Dose, false: afficher RPM

  if (!init) {
    lcd.clear();
    init = true;
    t0 = millis();
    tAlt = millis();
    showDose = true;
    DBGLN("[UI] Enter INFO");
  }

  // Ligne 1: état RTC
  lcd.setCursor(0, 0);
  lcdPrintPadded(rtcAvailable ? "RTC: presente" : "RTC: absente");

  // Alternance toutes les 2 s
  if (millis() - tAlt >= 2000UL) {
    showDose = !showDose;
    tAlt += 2000UL; // évite dérive si un tick est manqué
  }

  // Ligne 2: soit "Dose x.xx", soit "RPM 1234"
  char l2[17];

  if (showDose) {
    char fd[8];                   // "x.xx" (avec signe éventuel)
    dtostrf(doseTurns, 0, 2, fd); // 2 décimales, largeur minimale
    // dtostrf peut mettre un espace en tête: on l’enlève
    char* pfd = fd;
    while (*pfd == ' ') pfd++;
    snprintf(l2, sizeof(l2), "Dose %s", pfd);  // ex: "Dose 1.25"
  } else {
    snprintf(l2, sizeof(l2), "Vitesse/min %d", maxRPM); // ex: "RPM 120"
  }

  lcd.setCursor(0, 1);
  lcdPrintPadded(l2);

  // Sortie après MENU ou 4 s
  if (btnPressed(BTN_MENU) || millis() - t0 > 4000UL) {
    init = false;
    ui = UI_HOME;
    lcd.clear();
    DBGLN("[UI] Exit INFO -> HOME");
  }
}


// ====== Distribution immédiate (double‑clic OK) ======
unsigned long lastOkMs = 0;
bool waitingSecondOk = false;
const unsigned long DOUBLE_MS = 600; // fenêtre double‑clic

// Garde anti‑double dose (simple, non persistante)
const unsigned long GUARD_SECONDS = 2; // 60 s entre 2 doses
unsigned long lastDispenseDay = 0;
unsigned long lastDispenseSecInDay = 0;

unsigned long nowSecInDay() {
  Now n = getNow();
  return (unsigned long)n.h * 3600UL + (unsigned long)n.m * 60UL + (unsigned long)n.s;
}

bool canDispenseNow() {
  Now n = getNow();
  unsigned long nowS = (unsigned long)n.h*3600UL + (unsigned long)n.m*60UL + (unsigned long)n.s;
  unsigned long lastS = lastDispenseSecInDay;
  unsigned long dt = (n.day == lastDispenseDay) ? (nowS >= lastS ? (nowS - lastS) : (86400UL - lastS + nowS))
                                                : 86400UL; // jour différent -> OK
  DBG("[GUARD] now=%lu s, last=%lu s, dayNow=%lu, dayLast=%lu, dt=%lu s\n",
      nowS, lastS, n.day, lastDispenseDay, dt);
  return dt >= GUARD_SECONDS;
}

#if USE_DRV8825
void stepPulse(unsigned int usHalf) {
  digitalWrite(PIN_STEP, HIGH);
  delayMicroseconds(usHalf);
  digitalWrite(PIN_STEP, LOW);
  delayMicroseconds(usHalf);
}
#endif

void runDoseTurns(float turns, int rpm) {
#if USE_DRV8825
  if (rpm < 5) rpm = 5;
  if (rpm > 200) rpm = 200;

  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  pinMode(PIN_EN,   OUTPUT);

  digitalWrite(PIN_DIR, LOW); // sens par défaut
  digitalWrite(PIN_EN, LOW);   // activer driver

  const long totalSteps = (long)(turns * STEPS_PER_REV + 0.5f);
  double period_s = 60.0 / (rpm * (double)STEPS_PER_REV);
  unsigned int usHalf = (unsigned int)(period_s * 1e6 / 2.0);
  if (usHalf < 200) usHalf = 200;

  // Sécurité: timeout attendu (marge ×2 + 300 ms)
  const unsigned long expectedMs = (unsigned long)((turns * 60000.0) / (rpm > 0 ? rpm : 1));
  const unsigned long timeoutMs  = expectedMs * 2UL + 300UL;
  gMotionAborted = false;

  DBG("[MOTOR] turns=%.3f rpm=%d steps=%ld usHalf=%u expected=%lums timeout=%lums\n",
      turns, rpm, totalSteps, usHalf, expectedMs, timeoutMs);

  unsigned long t0 = millis();
  for (long i=0; i<totalSteps; ++i) {
    stepPulse(usHalf);

    // vérifs périodiques (toutes les 64 étapes)
    if ((i & 0x3F) == 0) {
      if (millis() - t0 > timeoutMs) {
        DBGLN("[SAFE] Timeout mouvement -> ABORT");
        gMotionAborted = true;
        break;
      }
      #ifdef FAULT_PIN
      if (digitalRead(FAULT_PIN) == LOW) {
        DBGLN("[SAFE] nFAULT=LOW -> ABORT");
        gMotionAborted = true;
        break;
      }
      #endif
    }
  }
  unsigned long t1 = millis();

  // Désactiver le driver quoi qu'il arrive
  digitalWrite(PIN_EN, HIGH);

  if (gMotionAborted) {
    DBGLN("[MOTOR] aborted (sécurité)");
  } else {
    DBG("[MOTOR] done in %lu ms\n", (t1 - t0));
  }
#else
  gMotionAborted = false;
  unsigned long ms = (unsigned long)( (turns * 60000.0) / (rpm > 0 ? rpm : 1) );
  DBG("[SIM] turns=%.3f rpm=%d sleep %lu ms\n", turns, rpm, ms);
  delay(ms);
#endif
}

void dispenseNow() {
  if (!canDispenseNow()) {
    DBGLN("[DISPENSE] blocked by guard");
    lcdShow2("Trop tot", "Attendre un peu");
    delay(800);
    return;
  }

  DBGLN("[DISPENSE] START");
  lcd.clear();
  lcdShow2("Distribution...", "Patientez");

  runDoseTurns(doseTurns, maxRPM);

  // Si sécurité déclenchée: ne pas valider la dose
  if (gMotionAborted) {
    lcdShow2("ERREUR MOTEUR", "Abandon");
    delay(1200);
    DBGLN("[DISPENSE] ABORTED");
    return;
  }

  Now n = getNow();
  lastDispenseDay = n.day;
  lastDispenseSecInDay = (unsigned long)n.h*3600UL + (unsigned long)n.m*60UL + (unsigned long)n.s;

  lcdShow2("OK", " ");
  delay(600);
  DBGLN("[DISPENSE] END");
}

// ====== Setup / Loop ======
void setup() {
  DBG_BEGIN();
  DBGLN("\n=== Boot ===");
#if defined(ARDUINO_AVR_MEGA2560) || defined(ARDUINO_AVR_ADK)
  DBGLN("[BOARD] Mega2560");
#else
  DBGLN("[BOARD] Uno/Nano");
#endif

  pinMode(BTN_OK,   INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  for (int p = 0; p < MAX_PINS; ++p) lastBtnLevel[p] = HIGH;

  lcd.init();
  lcd.backlight();
  lcd.clear();
  DBGLN("[LCD] init OK");

  // RTC optionnelle
  rtcAvailable = rtc.begin();
  if (rtcAvailable) {
    DBGLN("[RTC] detected");
  } else {
    DBGLN("[RTC] not found, using soft clock");
    softClockSet(12, 0, 0);
    softDay = 0;
  }

#if USE_DRV8825
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH); // disable au boot
  DBGLN("[DRV8825] EN=HIGH (disabled)");
  #ifdef FAULT_PIN
    pinMode(FAULT_PIN, INPUT_PULLUP);
    DBGLN("[SAFE] FAULT_PIN en INPUT_PULLUP");
  #endif
#else
  DBGLN("[DRV8825] disabled (simulation)");
#endif

  // ====== CHARGEMENT EEPROM -> variables utilisateur ======
  settingsLoad();
  feedH = constrain(G.feedH, 0, 23);
  feedM = constrain(G.feedM, 0, 59);
  doseTurns = (isnan(G.doseTurns) ? 1.0f : G.doseTurns);
  if (G.maxRPM < 5) G.maxRPM = 5; if (G.maxRPM > 200) G.maxRPM = 200;
  maxRPM = G.maxRPM;

  ui = UI_HOME;
  showHome(true);
  DBGLN("[UI] HOME ready");
}

void loop() {
  // Accès MENU depuis HOME
  if (ui == UI_HOME && btnPressed(BTN_MENU)) {
    ui = UI_MENU; menuSel = 0; menuTop = 0; lcd.clear();
    drawMenu();
    DBGLN("[UI] -> MENU");
  }

  switch (ui) {
    case UI_HOME: {
  // Double‑clic OK = distribution
  if (btnPressed(BTN_OK)) {
    unsigned long now = millis();
    if (waitingSecondOk && (now - lastOkMs) <= DOUBLE_MS) {
      DBGLN("[DC] second OK within window -> dispense");
      waitingSecondOk = false; 
      lastOkMs = 0;
      dispenseNow();
      lcd.clear(); 
      showHome(true);
    } else {
      waitingSecondOk = true; 
      lastOkMs = now;
      DBGLN("[DC] first OK, waiting second...");
      lcdShow2("OK x1 - encore", "OK pour DISTRIB");
    }
  }

  // Fin de fenêtre double‑clic
  if (waitingSecondOk && (millis() - lastOkMs) > DOUBLE_MS) {
    waitingSecondOk = false;
    DBGLN("[DC] window expired");
    lcd.clear(); 
    showHome(true);
  }

  if (!waitingSecondOk) showHome();
} break;


    case UI_MENU: {
      if (btnPressed(BTN_UP))   { menuSel = (menuSel + MENU_COUNT - 1) % MENU_COUNT; drawMenu(); }
      if (btnPressed(BTN_DOWN)) { menuSel = (menuSel + 1) % MENU_COUNT; drawMenu(); }
      if (btnPressed(BTN_OK))   { ui = MENU[menuSel].target; lcd.clear(); DBG("[UI] MENU select -> state %d\n", ui); }
      if (btnPressed(BTN_MENU)) { ui = UI_HOME; lcd.clear(); DBGLN("[UI] MENU -> HOME"); }
      if (ui == UI_MENU) drawMenu();
    } break;

    case UI_SET_CLOCK: handleSetClock(); break;
    case UI_SET_FEED:  handleSetFeed();  break;
    case UI_SET_DOSE:  handleSetDose();  break;
    case UI_SET_SPEED: handleSetSpeed(); break;
    case UI_INFO:      handleInfo();     break;
  }
}
