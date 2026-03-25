#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
// code developed by Sam's Labs for sensing human motion using the MS24-2020D58M4-FMCW 
// =========================================
// DISPLAY / RADAR
// =========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8
#define SCL_PIN 9
#define RADAR_OUT_PIN 5

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =========================================
// SIMPLE ICONS (tiny mood markers)
// =========================================
const unsigned char bmp_heart[] PROGMEM = {
  0x00,0x00,0x0C,0x30,0x1E,0x78,0x3F,0xFC,0x7F,0xFE,0x7F,0xFE,0x3F,0xFC,0x1F,0xF8,
  0x0F,0xF0,0x07,0xE0,0x03,0xC0,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

const unsigned char bmp_zzz[] PROGMEM = {
  0x00,0x00,0x7C,0x00,0x04,0x00,0x08,0x00,0x10,0x00,0x20,0x00,0x7C,0x00,0x00,0x00,
  0x1F,0x00,0x02,0x00,0x04,0x00,0x08,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

const unsigned char bmp_anger[] PROGMEM = {
  0x00,0x00,0x11,0x10,0x2A,0x90,0x44,0x40,0x80,0x20,0x80,0x20,0x44,0x40,0x2A,0x90,
  0x11,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// =========================================
// DESKBUDDY EYE ENGINE
// =========================================
struct EyeData {
  float x, y;
  float w, h;
  float targetX, targetY, targetW, targetH;
  float pupilX, pupilY;
  float targetPupilX, targetPupilY;
  float velX, velY, velW, velH;
  float pVelX, pVelY;
  float k = 0.12;
  float d = 0.60;
  float pk = 0.08;
  float pd = 0.50;
  bool blinking = false;
  unsigned long lastBlink = 0;
  unsigned long nextBlinkTime = 0;
};

EyeData leftEye, rightEye;

// =========================================
// MOODS
// =========================================
#define MOOD_NORMAL 0
#define MOOD_HAPPY 1
#define MOOD_SURPRISED 2
#define MOOD_SLEEPY 3
#define MOOD_ANGRY 4
#define MOOD_SAD 5
#define MOOD_EXCITED 6
#define MOOD_LOVE 7
#define MOOD_SUSPICIOUS 8

int currentMood = MOOD_NORMAL;
int localMood = MOOD_NORMAL;

// =========================================
// RADAR STATE
// =========================================
const unsigned long WINDOW_MS = 2000;
const int ACTIVE_THRESHOLD = 5;
const unsigned long PRESENCE_HOLD_MS = 5000;

int activityCount = 0;
unsigned long lastWindow = 0;
int motionScore = 0;

bool humanPresent = false;
bool sawHighInWindow = false;
unsigned long lastSeenTime = 0;

String activityState = "EMPTY";

// =========================================
// ANIMATION TIMERS
// =========================================
unsigned long lastSaccade = 0;
unsigned long saccadeInterval = 3000;
float breathVal = 0;

// =========================================
// HELPERS
// =========================================
void resetDisplayState() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setRotation(0);
  display.cp437(true);
}

void initEye(EyeData &e, float x, float y, float w, float h) {
  e.x = x; e.y = y; e.w = w; e.h = h;
  e.targetX = x; e.targetY = y; e.targetW = w; e.targetH = h;
  e.pupilX = 0; e.pupilY = 0;
  e.targetPupilX = 0; e.targetPupilY = 0;
  e.velX = e.velY = e.velW = e.velH = 0;
  e.pVelX = e.pVelY = 0;
  e.blinking = false;
  e.lastBlink = millis();
  e.nextBlinkTime = millis() + random(2000, 4500);
}

void updateEye(EyeData &e) {
  e.velX = (e.velX + (e.targetX - e.x) * e.k) * e.d;
  e.velY = (e.velY + (e.targetY - e.y) * e.k) * e.d;
  e.velW = (e.velW + (e.targetW - e.w) * e.k) * e.d;
  e.velH = (e.velH + (e.targetH - e.h) * e.k) * e.d;

  e.x += e.velX;
  e.y += e.velY;
  e.w += e.velW;
  e.h += e.velH;

  e.pVelX = (e.pVelX + (e.targetPupilX - e.pupilX) * e.pk) * e.pd;
  e.pVelY = (e.pVelY + (e.targetPupilY - e.pupilY) * e.pk) * e.pd;

  e.pupilX += e.pVelX;
  e.pupilY += e.pVelY;
}

void drawEyelidMask(float x, float y, float w, float h, int mood, bool isLeft) {
  int ix = (int)x;
  int iy = (int)y;
  int iw = (int)w;
  int ih = (int)h;

  if (mood == MOOD_ANGRY) {
    if (isLeft) {
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SH110X_BLACK);
    } else {
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SH110X_BLACK);
    }
  } else if (mood == MOOD_SAD) {
    if (isLeft) {
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy - 6 + i, ix + iw, iy + i, SH110X_BLACK);
    } else {
      for (int i = 0; i < 16; i++) display.drawLine(ix, iy + i, ix + iw, iy - 6 + i, SH110X_BLACK);
    }
  } else if (mood == MOOD_HAPPY || mood == MOOD_LOVE || mood == MOOD_EXCITED) {
    display.fillRect(ix, iy + ih - 12, iw, 14, SH110X_BLACK);
    display.fillCircle(ix + iw / 2, iy + ih + 6, iw / 1.3, SH110X_BLACK);
  } else if (mood == MOOD_SLEEPY) {
    display.fillRect(ix, iy, iw, ih / 2 + 2, SH110X_BLACK);
  } else if (mood == MOOD_SUSPICIOUS) {
    if (isLeft) display.fillRect(ix, iy, iw, ih / 2 - 2, SH110X_BLACK);
    else display.fillRect(ix, iy + ih - 8, iw, 8, SH110X_BLACK);
  }
}

void drawEye(bool isLeft) {
  EyeData &e = isLeft ? leftEye : rightEye;

  int ix = (int)e.x;
  int iy = (int)e.y;
  int iw = (int)e.w;
  int ih = (int)e.h;

  int r = 8;
  if (iw < 20) r = 3;

  display.fillRoundRect(ix, iy, iw, ih, r, SH110X_WHITE);

  int cx = ix + iw / 2;
  int cy = iy + ih / 2;
  int pw = iw / 2.2;
  int ph = ih / 2.2;

  int px = cx + (int)e.pupilX - (pw / 2);
  int py = cy + (int)e.pupilY - (ph / 2);

  if (px < ix) px = ix;
  if (px + pw > ix + iw) px = ix + iw - pw;
  if (py < iy) py = iy;
  if (py + ph > iy + ih) py = iy + ih - ph;

  display.fillRoundRect(px, py, pw, ph, r / 2, SH110X_BLACK);
  drawEyelidMask(ix, iy, iw, ih, currentMood, isLeft);
}

void updatePhysicsAndMood() {
  unsigned long now = millis();

  // breathing
  breathVal = sin(now / 600.0) * 2.0;

  // blink logic
  if (!leftEye.blinking && now >= leftEye.nextBlinkTime) {
    leftEye.blinking = true;
    rightEye.blinking = true;
    leftEye.lastBlink = now;
    rightEye.lastBlink = now;
  }

  if (leftEye.blinking) {
    if (now - leftEye.lastBlink < 130) {
      leftEye.targetH = 4;
      rightEye.targetH = 4;
    } else {
      leftEye.blinking = false;
      rightEye.blinking = false;
      leftEye.nextBlinkTime = now + random(2200, 4800);
      rightEye.nextBlinkTime = leftEye.nextBlinkTime;
    }
  }

  // saccades
  if (now - lastSaccade > saccadeInterval) {
    float lx = random(-8, 9);
    float ly = random(-5, 6);

    leftEye.targetPupilX = lx;
    leftEye.targetPupilY = ly;
    rightEye.targetPupilX = lx;
    rightEye.targetPupilY = ly;

    leftEye.targetX = 18 + (lx * 0.3);
    leftEye.targetY = 14 + (ly * 0.3);
    rightEye.targetX = 74 + (lx * 0.3);
    rightEye.targetY = 14 + (ly * 0.3);

    lastSaccade = now;
    saccadeInterval = random(1800, 4200);
  }

  if (!leftEye.blinking) {
    float baseW = 36;
    float baseH = 36 + breathVal;

    switch (currentMood) {
      case MOOD_NORMAL:
        leftEye.targetW = baseW; leftEye.targetH = baseH;
        rightEye.targetW = baseW; rightEye.targetH = baseH;
        break;

      case MOOD_HAPPY:
      case MOOD_LOVE:
        leftEye.targetW = 40; leftEye.targetH = 32;
        rightEye.targetW = 40; rightEye.targetH = 32;
        break;

      case MOOD_SURPRISED:
        leftEye.targetW = 30; leftEye.targetH = 45;
        rightEye.targetW = 30; rightEye.targetH = 45;
        break;

      case MOOD_SLEEPY:
        leftEye.targetW = 38; leftEye.targetH = 30;
        rightEye.targetW = 38; rightEye.targetH = 30;
        break;

      case MOOD_ANGRY:
        leftEye.targetW = 34; leftEye.targetH = 32;
        rightEye.targetW = 34; rightEye.targetH = 32;
        break;

      case MOOD_SAD:
        leftEye.targetW = 34; leftEye.targetH = 40;
        rightEye.targetW = 34; rightEye.targetH = 40;
        break;

      case MOOD_EXCITED:
        leftEye.targetW = 40; leftEye.targetH = 34;
        rightEye.targetW = 40; rightEye.targetH = 34;
        break;

      case MOOD_SUSPICIOUS:
        leftEye.targetW = 36; leftEye.targetH = 20;
        rightEye.targetW = 36; rightEye.targetH = 42;
        break;
    }
  }

  updateEye(leftEye);
  updateEye(rightEye);
}

void drawEmoPage() {
  resetDisplayState();
  updatePhysicsAndMood();

  if (currentMood == MOOD_LOVE) {
    display.drawBitmap(56, 0, bmp_heart, 16, 16, SH110X_WHITE);
  } else if (currentMood == MOOD_SLEEPY) {
    display.drawBitmap(110, 0, bmp_zzz, 16, 16, SH110X_WHITE);
  } else if (currentMood == MOOD_ANGRY) {
    display.drawBitmap(56, 0, bmp_anger, 16, 16, SH110X_WHITE);
  }

  drawEye(true);
  drawEye(false);

  display.setCursor(2, 2);
  if (activityState == "ACTIVE") display.print("ACTIVE");
  else if (activityState == "CALM") display.print("CALM");
  else display.print("EMPTY");

  display.setCursor(2, 56);
  display.print("Score:");
  display.print(motionScore);

  display.display();
}

// =========================================
// RADAR STATE MACHINE
// =========================================
void updateRadarState() {
  int rawNow = digitalRead(RADAR_OUT_PIN);

  if (rawNow == HIGH) {
    activityCount++;
    sawHighInWindow = true;
    humanPresent = true;
    lastSeenTime = millis();
  }

  if (humanPresent && (millis() - lastSeenTime > PRESENCE_HOLD_MS)) {
    humanPresent = false;
  }

  if (millis() - lastWindow >= WINDOW_MS) {
    motionScore = activityCount;

    if (!humanPresent) {
      activityState = "EMPTY";
      localMood = MOOD_SLEEPY;
    } else if (motionScore >= ACTIVE_THRESHOLD) {
      activityState = "ACTIVE";
      localMood = MOOD_EXCITED;
    } else {
      activityState = "CALM";
      localMood = MOOD_NORMAL;
    }

    currentMood = localMood;

    Serial.print("RawNow: ");
    Serial.print(rawNow);
    Serial.print(" | SawHigh: ");
    Serial.print(sawHighInWindow ? "YES" : "NO");
    Serial.print(" | Score: ");
    Serial.print(motionScore);
    Serial.print(" | Present: ");
    Serial.print(humanPresent ? "YES" : "NO");
    Serial.print(" | State: ");
    Serial.println(activityState);

    activityCount = 0;
    sawHighInWindow = false;
    lastWindow = millis();
  }
}

// =========================================
// SETUP / LOOP
// =========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(RADAR_OUT_PIN, INPUT);

  if (!display.begin(0x3C, true)) {
    Serial.println("Display init failed!");
    while (1) delay(10);
  }

  randomSeed(micros());

  initEye(leftEye, 18, 14, 36, 36);
  initEye(rightEye, 74, 14, 36, 36);

  resetDisplayState();
  display.setCursor(18, 20);
  display.print("DeskBuddy Face");
  display.setCursor(28, 34);
  display.print("Radar Warmup...");
  display.display();

  Serial.println("Radar warmup...");
  delay(15000);
  Serial.println("Ready!");

  lastWindow = millis();
  currentMood = MOOD_SLEEPY;
  localMood = MOOD_SLEEPY;
}

void loop() {
  updateRadarState();
  drawEmoPage();
  delay(40);
}
