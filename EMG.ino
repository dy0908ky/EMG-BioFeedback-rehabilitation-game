/*
 * EMG 다마고치 바이오피드백 게임 - 완성본 (REST 제거)
 * 하드웨어: 아두이노 우노 R3 + 0.96" I2C OLED (0x3C) + LED 2개
 * 배선: OLED SDA->A4, SCL->A5, VCC->5V, GND->GND
 *       EMG(또는 포텐셔미터)->A0 / 빨강LED->D7 / 초록LED->D8 (각 220ohm)
 *
 * 게임: REF 3초 측정 -> 목표치 = REF*0.85
 *       총 6 trial, 최대 레벨 7, 레벨 4 이상 WIN
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDR   0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const bool DEBUG = true;

const int EMG_PIN   = A0;
const int LED_RED   = 7;
const int LED_GREEN = 8;

const int MAX_LEVEL   = 7;    // 최대 레벨
const int TOTAL_TRIAL = 6;    // 총 6번
const int WIN_LEVEL   = 4;    // 레벨 4 이상이면 WIN

const unsigned long MEASURE_MS = 3000;   // 측정 3초
const int MEASURE_INTERVAL = 20;         // 20ms 간격 -> 약 150회

float refTrial = 0;
float threshold = 0;
int   level = 0;
int   trial = 0;
bool  curFace = false;   // 현재 표정 (true=웃음 유지, false=무표정)

const unsigned char tamaFace[] PROGMEM = {
  0b00000111, 0b11111111, 0b11100000,
  0b00011111, 0b11111111, 0b11111000,
  0b00111111, 0b11111111, 0b11111100,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b01111111, 0b11111111, 0b11111110,
  0b00111111, 0b11111111, 0b11111100,
  0b00111111, 0b11111111, 0b11111100,
  0b00011111, 0b11111111, 0b11111000,
  0b00000111, 0b11111111, 0b11100000,
  0b00000000, 0b00000000, 0b00000000
};

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);
  delay(100);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR);
  display.setTextColor(SSD1306_WHITE);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
}

void loop() {
  runGame();
}

// ===================================================
//  한 판 전체 흐름 (REST 없음)
// ===================================================
void runGame() {
  // --- REF (최대근력) ---
  showTitle("REF", "MEASURING");
  delay(1000);
  countdown();
  refTrial = measureWindow(false);

  threshold = refTrial * 0.85;   // 성공 기준 = REF의 85%

  showResultLine();
  delay(2000);

  // --- 본 게임 (lv 0부터) ---
  level = 0;
  curFace = false;            // 시작은 무표정

  drawScene(level, 1, curFace, -1);
  delay(800);

  for (trial = 1; trial <= TOTAL_TRIAL; trial++) {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, LOW);

    // 측정 중에는 직전 표정(curFace) 그대로 유지하며 카운트다운
    float mainTrial = measureWindow(true);

    bool stepUp = (mainTrial >= threshold);
    if (stepUp) {
      level++;
      if (level > MAX_LEVEL) level = MAX_LEVEL;
      digitalWrite(LED_GREEN, HIGH);
    } else {
      level--;
      if (level < 0) level = 0;
      digitalWrite(LED_RED, HIGH);
    }

    // 결과 반영 시점에 표정 갱신
    // 단, 바닥(lv 0)에서 실패해 정지하면 무표정 유지
    if (stepUp) {
      curFace = true;          // 올라감 -> 웃음
    } else {
      curFace = false;         // 내려감 또는 바닥 정지 -> 무표정
    }

    // trial과 lv 동시 갱신 + 표정 변경
    int shownTrial = (trial < TOTAL_TRIAL) ? trial + 1 : trial;
    drawScene(level, shownTrial, curFace, -1);

    delay(500);                      // LED 짧게 (0.5초)
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, LOW);

    delay(1500);                     // 변한 표정 고정 (1.5초)
  }

  // 결과 판정
  if (level >= WIN_LEVEL) showBigResult("WIN");
  else                    showBigResult("LOSE");
  delay(5000);

  display.clearDisplay();
  display.display();
  delay(500);
}

// ===================================================
//  카운트다운 (측정 전 3,2,1)
// ===================================================
void countdown() {
  for (int n = 3; n >= 1; n--) {
    display.clearDisplay();
    display.setTextSize(4);
    display.setCursor(54, 18);
    display.print(n);
    display.display();
    delay(1000);
  }
}

// ===================================================
//  측정: 3초간 -> 최댓값의 70~90% 구간 평균
//  측정 중엔 직전 표정(curFace) 유지하며 카운트다운 표시
// ===================================================
float measureWindow(bool showCount) {
  float peak = 0;
  long  sumBand = 0;
  int   cntBand = 0;

  unsigned long t0 = millis();
  int lastSec = -1;

  while (millis() - t0 < MEASURE_MS) {
    int raw = analogRead(EMG_PIN);
    float v = raw;
    if (v < 0) v = 0;

    if (v > peak) peak = v;

    if (v >= peak * 0.70 && v <= peak * 0.90) {
      sumBand += v;
      cntBand++;
    }

    if (DEBUG) {
      Serial.print(v); Serial.print('\t');
      Serial.print(threshold); Serial.print('\t');
      Serial.println(peak);
    }

    if (showCount) {
      int remain = (MEASURE_MS - (millis() - t0)) / 1000 + 1;
      if (remain != lastSec) {
        lastSec = remain;
        // 직전 표정(curFace) 유지하며 화면 갱신
        drawScene(level, trial, curFace, remain);
      }
    }

    delay(MEASURE_INTERVAL);
  }

  if (cntBand >= 3) return (float)sumBand / cntBand;
  return peak * 0.80;
}

// ===================================================
//  화면 그리기
// ===================================================
void drawScene(int lv, int tr, bool smile, int countNum) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Trial:");
  display.print(tr);
  display.print("/");
  display.print(TOTAL_TRIAL);
  display.setCursor(80, 0);
  display.print("LV:");
  display.print(lv);

// 계단 7칸 (화면 좌우 끝까지, 세로도 키움)
  int stepW = SCREEN_WIDTH / MAX_LEVEL;   // 128/7 = 약 18px
  int stepH = 7;                          // 세로 한 칸 높이 (5 -> 7)
  int baseX = 0;
  int baseY = 62;                         // 바닥 살짝 내림
  for (int i = 0; i <= MAX_LEVEL; i++) {
    int x = baseX + i * stepW;
    int y = baseY - i * stepH;
    if (x > SCREEN_WIDTH) x = SCREEN_WIDTH;
    display.drawFastHLine(x, y, stepW, SSD1306_WHITE);
    display.drawFastVLine(x, y, stepH, SSD1306_WHITE);
  }

  // 캐릭터 위치
  int cx, cy;
  if (lv == 0) {
    cx = baseX;
    cy = baseY - 16;
  } else {
    cx = baseX + lv * stepW - 8;
    cy = baseY - lv * stepH - 17;
  }
  if (cx > SCREEN_WIDTH - 24) cx = SCREEN_WIDTH - 24;
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;          // 위쪽 한계: 화면 위 끝까지 허용

  display.drawBitmap(cx, cy, tamaFace, 24, 16, SSD1306_WHITE);

  int eyeY = cy + 6;
  display.fillRect(cx + 6,  eyeY, 2, 2, SSD1306_BLACK);
  display.fillRect(cx + 16, eyeY, 2, 2, SSD1306_BLACK);

  int mx = cx + 8;
  int my = cy + 9;
  if (smile) {
    display.drawPixel(mx,     my,   SSD1306_BLACK);
    display.drawPixel(mx+1,   my+1, SSD1306_BLACK);
    display.drawPixel(mx+2,   my+1, SSD1306_BLACK);
    display.drawPixel(mx+3,   my,   SSD1306_BLACK);
    display.drawPixel(mx+4,   my,   SSD1306_BLACK);
    display.drawPixel(mx+5,   my+1, SSD1306_BLACK);
    display.drawPixel(mx+6,   my+1, SSD1306_BLACK);
    display.drawPixel(mx+7,   my,   SSD1306_BLACK);
  } else {
    display.drawFastHLine(mx, my, 7, SSD1306_BLACK);
  }

  if (countNum > 0) {
    display.setTextSize(2);
    display.setCursor(112, 47);
    display.print(countNum);
  }

  display.display();
}

void showTitle(const char* l1, const char* l2) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.println(l1);
  display.setCursor(0, 38);
  display.println(l2);
  display.display();
}

void showResultLine() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 5);
  display.println("Measuring Done");
  display.setCursor(0, 28);
  display.print("Ref :");
  display.println(refTrial, 0);
  display.setCursor(0, 44);
  display.print("Goal:");
  display.println(threshold, 0);
  display.display();
}

void showBigResult(const char* txt) {
  display.clearDisplay();
  display.setTextSize(3);
  int len = strlen(txt);
  int x = (SCREEN_WIDTH - len * 18) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, 20);
  display.println(txt);
  display.display();
}
