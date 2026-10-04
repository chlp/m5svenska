// m5svenska — тренажёр шведских слов для M5Stack CoreS3 / CoreS3 Lite (SE)
#include <M5Unified.h>
#include <Preferences.h>
#include <vector>

#include "words.h"
#include "fonts/font_b40.h"
#include "fonts/font_b28.h"
#include "fonts/font_b22.h"
#include "fonts/font_r16.h"

// ---------------------------------------------------------------- палитра
// Тёмно-синий фон + цвета шведского флага как акценты.
constexpr uint32_t C_BG        = 0x0A1628;
constexpr uint32_t C_CARD      = 0x12305A;
constexpr uint32_t C_CARD_EDGE = 0x1F4E86;
constexpr uint32_t C_TILE      = 0x16294A;
constexpr uint32_t C_TILE_EDGE = 0x2B4870;
constexpr uint32_t C_YELLOW    = 0xFECC02;
constexpr uint32_t C_BLUE      = 0x2E8BD8;
constexpr uint32_t C_TEXT      = 0xF2F5FA;
constexpr uint32_t C_MUTED     = 0x8DA2BF;
constexpr uint32_t C_GREEN     = 0x2EA043;
constexpr uint32_t C_RED       = 0xD9453D;

constexpr int W = 320, H = 240;
constexpr int MAX_LEVEL = 5;          // уровень знания слова (Leitner)
constexpr uint32_t DIM_AFTER_MS = 90000;

// ---------------------------------------------------------------- шрифты
enum FontId { F_B40, F_B28, F_B22, F_R16 };
static const uint8_t* const FONTS[] = {font_b40, font_b28, font_b22, font_r16};

M5Canvas cv(&M5.Display);
static int curFont = -1;

static void useFont(FontId f) {
  if (curFont == f) return;
  cv.unloadFont();
  cv.loadFont(FONTS[f]);
  curFont = f;
}

// Рисует строку по центру точки, подбирая самый крупный шрифт из списка,
// который влезает в maxW. Если не влез даже последний — перенос на 2 строки.
static void drawFit(const char* s, int cx, int cy, int maxW, uint32_t color,
                    std::initializer_list<FontId> fonts) {
  cv.setTextColor(color);
  cv.setTextDatum(middle_center);
  FontId last = F_R16;
  for (FontId f : fonts) {
    useFont(f);
    last = f;
    if (cv.textWidth(s) <= maxW) {
      cv.drawString(s, cx, cy);
      return;
    }
  }
  // перенос по пробелу, ближайшему к середине
  String str(s);
  int mid = str.length() / 2, best = -1;
  for (int i = 0; i < (int)str.length(); i++)
    if (str[i] == ' ' && (best < 0 || abs(i - mid) < abs(best - mid))) best = i;
  useFont(last);
  int lh = cv.fontHeight();
  if (best < 0) {  // длинное составное слово: режем посередине по границе UTF-8 и ставим дефис
    int cut = mid;
    while (cut > 0 && (str[cut] & 0xC0) == 0x80) cut--;
    cv.drawString(str.substring(0, cut) + "-", cx, cy - lh / 2);
    cv.drawString(str.substring(cut), cx, cy + lh / 2);
    return;
  }
  cv.drawString(str.substring(0, best), cx, cy - lh / 2);
  cv.drawString(str.substring(best + 1), cx, cy + lh / 2);
}

static void drawText(const char* s, int x, int y, FontId f, uint32_t color,
                     textdatum_t datum) {
  useFont(f);
  cv.setTextColor(color);
  cv.setTextDatum(datum);
  cv.drawString(s, x, y);
}

// ---------------------------------------------------------------- состояние
Preferences prefs;
static uint8_t level[WORD_COUNT];
static int unsavedAnswers = 0;

// Сложность: в игре участвуют только первые N самых частотных слов.
static const int TIERS[] = {250, 1000, 10000};
constexpr int TIER_COUNT = sizeof(TIERS) / sizeof(TIERS[0]);
static int tier = 0;
static int limit() { return min<int>(TIERS[tier], WORD_COUNT); }

enum Screen { MENU, QUIZ, CARDS };
static Screen screen = MENU;
static bool dirty = true;

struct Rect {
  int x, y, w, h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// меню
static const Rect MENU_TILES[4] = {{8, 96, 148, 66}, {164, 96, 148, 66},
                                   {8, 168, 148, 66}, {164, 168, 148, 66}};
static const Rect TIER_BAR = {8, 58, 304, 30};

// квиз
enum QuizMode { SV_RU, RU_SV, MIX };
static QuizMode quizMode = SV_RU;
static bool dirSvRu = true;          // направление текущего вопроса
static int qWord = 0;
static int qOpts[4];
static int qCorrect = 0;
static int qChosen = -1;
static uint32_t qAnsweredAt = 0;
static int scoreOk = 0, scoreTotal = 0, streak = 0;
static int recent[10];
static int recentPos = 0;
static const Rect OPT_RECTS[4] = {{8, 124, 148, 54}, {164, 124, 148, 54},
                                  {8, 184, 148, 54}, {164, 184, 148, 54}};
static const Rect BACK_BTN = {0, 0, 110, 30};

// карточки
static std::vector<uint16_t> deck;
static int cardPos = 0;
static bool autoPlay = false;
static uint32_t cardShownAt = 0;
static const Rect AUTO_BTN = {230, 0, 90, 30};
constexpr uint32_t AUTO_MS = 4000;

// ---------------------------------------------------------------- прогресс
// Уровни хранятся в NVS по 4 бита на слово. Индекс = ранг слова в words.h,
// поэтому при пересборке словаря с другим порядком прогресс сбрасывается (ключ "lv4n").
static uint8_t packed[(WORD_COUNT + 1) / 2];

static void loadProgress() {
  prefs.begin("svenska", false);
  prefs.remove("lvl");  // формат первой версии (355 слов)
  memset(level, 0, sizeof(level));
  if (prefs.getUShort("lv4n", 0) == WORD_COUNT &&
      prefs.getBytesLength("lv4") == sizeof(packed)) {
    prefs.getBytes("lv4", packed, sizeof(packed));
    for (int i = 0; i < WORD_COUNT; i++) level[i] = (packed[i / 2] >> (i % 2 * 4)) & 0x0F;
  }
  tier = constrain(prefs.getUChar("tier", 0), 0, TIER_COUNT - 1);
}

static void saveProgress() {
  memset(packed, 0, sizeof(packed));
  for (int i = 0; i < WORD_COUNT; i++) packed[i / 2] |= level[i] << (i % 2 * 4);
  prefs.putBytes("lv4", packed, sizeof(packed));
  prefs.putUShort("lv4n", WORD_COUNT);
  unsavedAnswers = 0;
}

static int learnedCount() {
  int n = 0;
  for (int i = 0; i < limit(); i++) n += level[i] >= 3;
  return n;
}

// ---------------------------------------------------------------- выбор слов
// Похожи ли два слова по смыслу (общее значимое слово в переводе или оригинале):
// такие нельзя давать одновременно в вариантах — оба ответа были бы верными.
static void tokens(const char* s, std::vector<String>& out, bool sv) {
  String cur;
  auto flush = [&]() {
    if (cur.length() >= (sv ? 2 : 5) &&
        !(sv && (cur == "en" || cur == "ett" || cur == "att")))
      out.push_back(cur);
    cur = "";
  };
  for (const char* p = s; *p; p++) {
    if (strchr(" ,()/.", *p)) flush();
    else cur += *p;
  }
  flush();
}

static bool overlaps(const char* a, const char* b, bool sv) {
  std::vector<String> ta, tb;
  tokens(a, ta, sv);
  tokens(b, tb, sv);
  for (auto& x : ta)
    for (auto& y : tb)
      if (x == y) return true;
  return false;
}

static bool conflicts(int a, int b) {
  return a == b || overlaps(WORDS[a].ru, WORDS[b].ru, false) ||
         overlaps(WORDS[a].sv, WORDS[b].sv, true);
}

static bool isRecent(int idx) {
  for (int r : recent)
    if (r == idx) return true;
  return false;
}

// Взвешенный случайный выбор: новые и ошибочные слова выпадают чаще.
static uint32_t weight(int i) {
  int k = MAX_LEVEL + 1 - level[i];
  return isRecent(i) ? 0 : k * k;
}

static int pickWord() {
  uint32_t total = 0;
  for (int i = 0; i < limit(); i++) total += weight(i);
  uint32_t r = esp_random() % total;
  for (int i = 0; i < limit(); i++) {
    uint32_t w = weight(i);
    if (r < w) return i;
    r -= w;
  }
  return 0;
}

static void newQuestion() {
  qWord = pickWord();
  recent[recentPos++ % 10] = qWord;
  dirSvRu = quizMode == MIX ? (esp_random() & 1) : quizMode == SV_RU;
  qCorrect = esp_random() % 4;
  qChosen = -1;
  int n = 0;
  int opts[4];
  for (int tries = 0; n < 3 && tries < 2000; tries++) {
    int c = esp_random() % limit();
    if (tries < 1500 && WORDS[c].pos != WORDS[qWord].pos) continue;
    bool bad = conflicts(c, qWord);
    for (int j = 0; j < n && !bad; j++) bad = conflicts(c, opts[j]);
    if (!bad) opts[n++] = c;
  }
  for (int i = 0, j = 0; i < 4; i++) qOpts[i] = i == qCorrect ? qWord : opts[j++];
  dirty = true;
}

static void startQuiz(QuizMode m) {
  quizMode = m;
  scoreOk = scoreTotal = streak = 0;
  for (int& r : recent) r = -1;
  screen = QUIZ;
  newQuestion();
}

static void shuffleDeck() {
  deck.resize(limit());
  for (int i = 0; i < limit(); i++) deck[i] = i;
  for (int i = limit() - 1; i > 0; i--) std::swap(deck[i], deck[esp_random() % (i + 1)]);
  cardPos = 0;
}

static void startCards() {
  if ((int)deck.size() != limit()) shuffleDeck();
  screen = CARDS;
  cardShownAt = millis();
  dirty = true;
}

static void goMenu() {
  if (unsavedAnswers) saveProgress();
  screen = MENU;
  dirty = true;
}

// ---------------------------------------------------------------- отрисовка
static void drawLevelDots(int cx, int y, int lvl) {
  int x0 = cx - (MAX_LEVEL - 1) * 6;
  for (int i = 0; i < MAX_LEVEL; i++) {
    if (i < lvl) cv.fillCircle(x0 + i * 12, y, 3, C_YELLOW);
    else cv.drawCircle(x0 + i * 12, y, 3, C_MUTED);
  }
}

static void drawTopBar(const char* center) {
  drawText("\xE2\x80\xB9 Меню", 10, 14, F_R16, C_MUTED, middle_left);  // ‹
  drawText(center, W / 2, 14, F_R16, C_TEXT, middle_center);
}

static void drawSwedishFlag(int x, int y) {
  cv.fillRoundRect(x, y, 32, 20, 3, 0x006AA7);
  cv.fillRect(x + 10, y, 5, 20, C_YELLOW);
  cv.fillRect(x, y + 8, 32, 5, C_YELLOW);
}

static void drawMenu() {
  cv.fillScreen(C_BG);
  drawSwedishFlag(12, 12);
  drawText("Svenska", 52, 22, F_B28, C_YELLOW, middle_left);
  char buf[48];
  snprintf(buf, sizeof(buf), "выучено %d из %d", learnedCount(), limit());
  drawText(buf, 12, 46, F_R16, C_MUTED, middle_left);

  // переключатель «топ N»
  const Rect& b = TIER_BAR;
  cv.fillRoundRect(b.x, b.y, b.w, b.h, 15, C_TILE);
  cv.drawRoundRect(b.x, b.y, b.w, b.h, 15, C_TILE_EDGE);
  int sw = b.w / TIER_COUNT;
  for (int i = 0; i < TIER_COUNT; i++) {
    int sx = b.x + i * sw;
    if (i == tier) cv.fillRoundRect(sx + 2, b.y + 2, sw - 4, b.h - 4, 13, C_YELLOW);
    snprintf(buf, sizeof(buf), "Топ %d", TIERS[i]);
    drawText(buf, sx + sw / 2, b.y + b.h / 2, F_R16, i == tier ? C_BG : C_TEXT, middle_center);
  }

  struct { const char* title; const char* sub; uint32_t accent; } tiles[4] = {
    {"SV \xE2\x86\x92 RU", "квиз", C_YELLOW},
    {"RU \xE2\x86\x92 SV", "квиз", C_BLUE},
    {"Mix", "вперемешку", C_GREEN},
    {"Kort", "карточки", 0xC678DD},
  };
  for (int i = 0; i < 4; i++) {
    const Rect& r = MENU_TILES[i];
    cv.fillRoundRect(r.x, r.y, r.w, r.h, 12, C_TILE);
    cv.drawRoundRect(r.x, r.y, r.w, r.h, 12, C_TILE_EDGE);
    cv.fillRoundRect(r.x + 10, r.y + 12, 4, r.h - 24, 2, tiles[i].accent);
    useFont(F_B28);
    FontId tf = cv.textWidth(tiles[i].title) <= r.w - 30 ? F_B28 : F_B22;
    drawText(tiles[i].title, r.x + 22, r.y + 26, tf, C_TEXT, middle_left);
    drawText(tiles[i].sub, r.x + 22, r.y + 50, F_R16, C_MUTED, middle_left);
  }
}

static void drawQuiz() {
  cv.fillScreen(C_BG);
  const char* dirLabel = dirSvRu ? "SV \xE2\x86\x92 RU" : "RU \xE2\x86\x92 SV";
  drawTopBar(quizMode == MIX ? (dirSvRu ? "Mix · SV \xE2\x86\x92 RU" : "Mix · RU \xE2\x86\x92 SV") : dirLabel);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d/%d", scoreOk, scoreTotal);
  drawText(buf, W - 10, 14, F_R16, C_TEXT, middle_right);
  if (streak >= 3) {
    snprintf(buf, sizeof(buf), "\xC3\x97%d", streak);  // ×N
    useFont(F_R16);
    int sw = cv.textWidth("000/000");
    drawText(buf, W - 18 - sw, 14, F_R16, C_YELLOW, middle_right);
  }

  // карточка вопроса
  cv.fillRoundRect(8, 32, 304, 84, 14, C_CARD);
  cv.drawRoundRect(8, 32, 304, 84, 14, C_CARD_EDGE);
  const Word& w = WORDS[qWord];
  drawFit(dirSvRu ? w.sv : w.ru, W / 2, 68, 284, dirSvRu ? C_YELLOW : C_TEXT, {F_B40, F_B28, F_B22});
  drawLevelDots(W / 2, 104, level[qWord]);

  // варианты
  for (int i = 0; i < 4; i++) {
    const Rect& r = OPT_RECTS[i];
    uint32_t fill = C_TILE, edge = C_TILE_EDGE, txt = C_TEXT;
    if (qChosen >= 0) {
      if (i == qCorrect) fill = edge = C_GREEN;
      else if (i == qChosen) fill = edge = C_RED;
      else txt = C_MUTED;
    }
    cv.fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
    cv.drawRoundRect(r.x, r.y, r.w, r.h, 10, edge);
    const Word& o = WORDS[qOpts[i]];
    drawFit(dirSvRu ? o.ru : o.sv, r.x + r.w / 2, r.y + r.h / 2, r.w - 12, txt, {F_B22, F_R16});
  }
}

static void drawCards() {
  cv.fillScreen(C_BG);
  char buf[24];
  snprintf(buf, sizeof(buf), "Kort  %d / %d", cardPos + 1, limit());
  drawTopBar(buf);
  cv.fillRoundRect(AUTO_BTN.x + 22, 4, 62, 22, 11, autoPlay ? C_YELLOW : C_TILE);
  drawText("авто", AUTO_BTN.x + 53, 15, F_R16, autoPlay ? C_BG : C_MUTED, middle_center);

  int idx = deck[cardPos];
  const Word& w = WORDS[idx];
  cv.fillRoundRect(8, 34, 304, 168, 16, C_CARD);
  cv.drawRoundRect(8, 34, 304, 168, 16, C_CARD_EDGE);
  drawText("SVENSKA", 24, 50, F_R16, C_MUTED, middle_left);
  drawFit(w.sv, W / 2, 86, 284, C_YELLOW, {F_B40, F_B28, F_B22});
  cv.drawFastHLine(40, 118, 240, C_CARD_EDGE);
  drawText("РУССКИЙ", 24, 134, F_R16, C_MUTED, middle_left);
  drawFit(w.ru, W / 2, 166, 284, C_TEXT, {F_B28, F_B22, F_R16});
  drawLevelDots(W - 52, 50, level[idx]);
  // ранг по частоте и уровень CEFR
  static const char* const CEFR[] = {"", "A1", "A2", "B1", "B2", "C1", "C2"};
  snprintf(buf, sizeof(buf), "#%d %s", idx + 1, CEFR[w.cefr]);
  drawText(buf, 120, 50, F_R16, C_MUTED, middle_left);

  drawText("\xE2\x80\xB9 назад", 14, 222, F_R16, C_MUTED, middle_left);
  drawText("далее \xE2\x80\xBA", W - 14, 222, F_R16, C_MUTED, middle_right);
  if (autoPlay) {
    uint32_t el = millis() - cardShownAt;
    int pw = min<uint32_t>(el, AUTO_MS) * 140 / AUTO_MS;
    cv.fillRoundRect(90, 220, 140, 4, 2, C_TILE);
    cv.fillRoundRect(90, 220, pw, 4, 2, C_YELLOW);
  }
}

static void render() {
  switch (screen) {
    case MENU: drawMenu(); break;
    case QUIZ: drawQuiz(); break;
    case CARDS: drawCards(); break;
  }
  cv.pushSprite(0, 0);
  dirty = false;
}

// ---------------------------------------------------------------- ввод
static void onTap(int x, int y) {
  switch (screen) {
    case MENU:
      if (TIER_BAR.hit(x, y)) {
        int t = constrain((x - TIER_BAR.x) * TIER_COUNT / TIER_BAR.w, 0, TIER_COUNT - 1);
        if (t != tier) {
          tier = t;
          prefs.putUChar("tier", tier);
          deck.clear();
        }
        dirty = true;
        return;
      }
      if (MENU_TILES[0].hit(x, y)) startQuiz(SV_RU);
      else if (MENU_TILES[1].hit(x, y)) startQuiz(RU_SV);
      else if (MENU_TILES[2].hit(x, y)) startQuiz(MIX);
      else if (MENU_TILES[3].hit(x, y)) startCards();
      break;

    case QUIZ:
      if (BACK_BTN.hit(x, y)) { goMenu(); return; }
      if (qChosen >= 0) {  // тап во время показа ответа — сразу следующий
        newQuestion();
        return;
      }
      // Зона ответов делится на 4 квадранта без зазоров и до края экрана:
      // у нижнего края тач менее точен, строгие прямоугольники промахивались.
      if (y >= OPT_RECTS[0].y - 4) {
        int i = (x >= W / 2 ? 1 : 0) + (y >= (OPT_RECTS[0].y + OPT_RECTS[0].h + OPT_RECTS[2].y) / 2 ? 2 : 0);
        qChosen = i;
        qAnsweredAt = millis();
        scoreTotal++;
        bool ok = i == qCorrect;
        if (ok) {
          scoreOk++;
          streak++;
          if (level[qWord] < MAX_LEVEL) level[qWord]++;
        } else {
          streak = 0;
          level[qWord] = 0;
        }
        if (++unsavedAnswers >= 5) saveProgress();
        render();
        return;
      }
      break;

    case CARDS:
      if (BACK_BTN.hit(x, y)) { goMenu(); return; }
      if (AUTO_BTN.hit(x, y)) {
        autoPlay = !autoPlay;
        cardShownAt = millis();
        dirty = true;
        return;
      }
      if (x < W / 3) cardPos = (cardPos + limit() - 1) % limit();
      else cardPos = (cardPos + 1) % limit();
      cardShownAt = millis();
      dirty = true;
      break;
  }
}

// Отладка по serial:
//   'S'          — снимок экрана: "SHOT 320 240\n" + RGB565 (big-endian)
//   'T x y\n'    — эмуляция тапа в точку (x, y)
static void handleSerial() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'S') {
      Serial.printf("SHOT %d %d\n", W, H);
      Serial.write((const uint8_t*)cv.getBuffer(), W * H * 2);
      Serial.flush();
    } else if (c == 'T') {
      int x = Serial.parseInt(), y = Serial.parseInt();
      onTap(x, y);
      if (dirty) render();
    }
  }
}

// ---------------------------------------------------------------- main
static uint32_t lastTouch = 0;
static bool dimmed = false;

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = false;  // звука в проекте нет
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(160);

  cv.setColorDepth(16);
  cv.setPsram(true);
  cv.createSprite(W, H);

  loadProgress();
  lastTouch = millis();
  render();
}

void loop() {
  M5.update();
  handleSerial();
  uint32_t now = millis();

  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    Serial.printf("touch %d %d\n", t.x, t.y);
    lastTouch = now;
    if (dimmed) {  // первый тап только будит экран
      dimmed = false;
      M5.Display.setBrightness(160);
    } else {
      onTap(t.x, t.y);
    }
  }

  if (!dimmed && now - lastTouch > DIM_AFTER_MS && !(screen == CARDS && autoPlay)) {
    dimmed = true;
    M5.Display.setBrightness(20);
  }

  if (screen == QUIZ && qChosen >= 0) {
    uint32_t wait = qChosen == qCorrect ? 900 : 2200;
    if (now - qAnsweredAt > wait) newQuestion();
  }

  if (screen == CARDS && autoPlay) {
    if (now - cardShownAt >= AUTO_MS) {
      cardPos = (cardPos + 1) % limit();
      cardShownAt = now;
    }
    static uint32_t lastFrame = 0;
    if (now - lastFrame > 100) {  // анимация полоски прогресса
      lastFrame = now;
      dirty = true;
    }
  }

  if (dirty) render();
  delay(10);
}
