#include "games.h"
#include "config.h"
#include "renderer.h"

#include <Arduino.h>
#include <string.h>

namespace {

Game active = Game::LightsOut;
char statusBuf[40] = "";

void setStatus(const char* s) {
  strncpy(statusBuf, s, sizeof(statusBuf) - 1);
  statusBuf[sizeof(statusBuf) - 1] = '\0';
}

// A 1-bit panel can't do the simulator's "difference" composite, so the cursor is drawn in
// whichever color contrasts with the cell it's on — we always know the cell's fill state
// here, so the result is the same guaranteed-visible highlight, just computed instead of
// composited.
void cursorRect(Adafruit_GFX& g, int x, int y, int w, int h, bool cellIsFilled) {
  uint16_t c = cellIsFilled ? PAPER : INK;
  g.drawRect(x, y, w, h, c);
  g.drawRect(x + 1, y + 1, w - 2, h - 2, c);
}

void centerText(U8G2_FOR_ADAFRUIT_GFX& u8f, int y, const char* text) {
  int w = (int)u8f.getUTF8Width(text);
  u8f.setCursor((PAGE_WIDTH_PX - w) / 2, y);
  u8f.print(text);
}

// ---------------------------------------------------------------- Lights Out (5x5)
struct LightsOutState {
  static const int SIZE = 5;
  bool grid[SIZE][SIZE];
  int cursor;
  bool won;
} lo;

void loToggle(int r, int c) {
  if (r < 0 || c < 0 || r >= LightsOutState::SIZE || c >= LightsOutState::SIZE) return;
  lo.grid[r][c] = !lo.grid[r][c];
}

void loPress(int r, int c) {
  loToggle(r, c);
  loToggle(r - 1, c);
  loToggle(r + 1, c);
  loToggle(r, c - 1);
  loToggle(r, c + 1);
}

bool loSolved() {
  for (int r = 0; r < LightsOutState::SIZE; r++) {
    for (int c = 0; c < LightsOutState::SIZE; c++) {
      if (lo.grid[r][c]) return false;
    }
  }
  return true;
}

void loStart() {
  memset(lo.grid, 0, sizeof(lo.grid));
  lo.cursor = 0;
  lo.won = false;
  // Scrambling by pressing random cells from the solved board guarantees the position is
  // reachable — a uniformly random grid often isn't solvable at all.
  int presses = 6 + (int)random(6);
  for (int i = 0; i < presses; i++) {
    loPress((int)random(LightsOutState::SIZE), (int)random(LightsOutState::SIZE));
  }
  if (loSolved()) loPress(2, 2);
  setStatus("Turn every light off");
}

// ---------------------------------------------------------------- Minesweeper (9x6, 8 mines)
struct MinesweeperState {
  static const int COLS = 9, ROWS = 6, MINES = 8;
  bool mine[ROWS][COLS];
  bool revealed[ROWS][COLS];
  bool flagged[ROWS][COLS];
  bool placed;  // mines are laid on the first reveal, so the first pick is never a loss
  bool dead, won;
  int cursor;
} ms;

int msAdjacent(int r, int c) {
  int n = 0;
  for (int dr = -1; dr <= 1; dr++) {
    for (int dc = -1; dc <= 1; dc++) {
      int rr = r + dr, cc = c + dc;
      if (dr == 0 && dc == 0) continue;
      if (rr < 0 || cc < 0 || rr >= MinesweeperState::ROWS || cc >= MinesweeperState::COLS) continue;
      if (ms.mine[rr][cc]) n++;
    }
  }
  return n;
}

void msPlaceMines(int safeR, int safeC) {
  int placed = 0;
  while (placed < MinesweeperState::MINES) {
    int r = (int)random(MinesweeperState::ROWS);
    int c = (int)random(MinesweeperState::COLS);
    if (ms.mine[r][c]) continue;
    if (r == safeR && c == safeC) continue;
    ms.mine[r][c] = true;
    placed++;
  }
  ms.placed = true;
}

void msReveal(int r, int c) {
  if (r < 0 || c < 0 || r >= MinesweeperState::ROWS || c >= MinesweeperState::COLS) return;
  if (ms.revealed[r][c] || ms.flagged[r][c]) return;
  ms.revealed[r][c] = true;
  if (ms.mine[r][c]) {
    ms.dead = true;
    setStatus("Boom. MENU restarts");
    return;
  }
  if (msAdjacent(r, c) == 0) {
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        if (dr || dc) msReveal(r + dr, c + dc);
      }
    }
  }
}

void msCheckWin() {
  for (int r = 0; r < MinesweeperState::ROWS; r++) {
    for (int c = 0; c < MinesweeperState::COLS; c++) {
      if (!ms.mine[r][c] && !ms.revealed[r][c]) return;
    }
  }
  ms.won = true;
  setStatus("Swept it. MENU restarts");
}

void msStart() {
  memset(ms.mine, 0, sizeof(ms.mine));
  memset(ms.revealed, 0, sizeof(ms.revealed));
  memset(ms.flagged, 0, sizeof(ms.flagged));
  ms.placed = ms.dead = ms.won = false;
  ms.cursor = 0;
  setStatus("MENU reveals, dial flags");
}

// ---------------------------------------------------------------- Sudoku
struct SudokuState {
  uint8_t grid[81];
  bool given[81];
  int cursor;
} sd;

bool sdFits(uint8_t* g, int idx, uint8_t v) {
  int r = idx / 9, c = idx % 9;
  for (int i = 0; i < 9; i++) {
    if (g[r * 9 + i] == v) return false;
    if (g[i * 9 + c] == v) return false;
  }
  int br = (r / 3) * 3, bc = (c / 3) * 3;
  for (int dr = 0; dr < 3; dr++) {
    for (int dc = 0; dc < 3; dc++) {
      if (g[(br + dr) * 9 + bc + dc] == v) return false;
    }
  }
  return true;
}

bool sdFill(uint8_t* g, int idx) {
  if (idx >= 81) return true;
  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 8; i > 0; i--) {  // Fisher-Yates, so every puzzle isn't the same grid
    int j = (int)random(i + 1);
    uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int i = 0; i < 9; i++) {
    if (!sdFits(g, idx, order[i])) continue;
    g[idx] = order[i];
    if (sdFill(g, idx + 1)) return true;
    g[idx] = 0;
  }
  return false;
}

void sdStart() {
  memset(sd.grid, 0, sizeof(sd.grid));
  sdFill(sd.grid, 0);
  // Keep 32 clues, same as the simulator's default — enough to stay solvable-feeling
  // without checking uniqueness, which isn't worth the search on-device.
  int toRemove = 81 - 32;
  while (toRemove > 0) {
    int idx = (int)random(81);
    if (sd.grid[idx] == 0) continue;
    sd.grid[idx] = 0;
    toRemove--;
  }
  for (int i = 0; i < 81; i++) sd.given[i] = sd.grid[i] != 0;
  sd.cursor = 0;
  setStatus("MENU +1, dial -1");
}

void sdCycle(int delta) {
  if (sd.given[sd.cursor]) return;
  int v = (int)sd.grid[sd.cursor] + delta;
  if (v > 9) v = 0;
  if (v < 0) v = 9;
  sd.grid[sd.cursor] = (uint8_t)v;
}

// ---------------------------------------------------------------- Hangman
const char* const HANGMAN_WORDS[] = {"paper",  "rotary", "pixel",  "reader", "ghost",
                                      "flash",  "cursor", "binary", "escape", "buffer",
                                      "anchor", "signal", "kernel", "matrix", "orbit"};
const int HANGMAN_WORD_COUNT = sizeof(HANGMAN_WORDS) / sizeof(HANGMAN_WORDS[0]);
const int HANGMAN_MAX_WRONG = 6;

struct HangmanState {
  const char* word;
  bool guessed[26];
  int wrong;
  int cursor;  // 0..25
  bool done;
} hm;

bool hmWon() {
  for (const char* p = hm.word; *p; p++) {
    if (!hm.guessed[*p - 'a']) return false;
  }
  return true;
}

void hmStart() {
  hm.word = HANGMAN_WORDS[(int)random(HANGMAN_WORD_COUNT)];
  memset(hm.guessed, 0, sizeof(hm.guessed));
  hm.wrong = 0;
  hm.cursor = 0;
  hm.done = false;
  setStatus("Dial a letter, MENU guesses");
}

void hmGuess() {
  if (hm.done) {
    hmStart();
    return;
  }
  if (hm.guessed[hm.cursor]) return;
  hm.guessed[hm.cursor] = true;
  bool hit = strchr(hm.word, 'a' + hm.cursor) != nullptr;
  if (!hit) hm.wrong++;
  if (hmWon()) {
    hm.done = true;
    setStatus("Got it. MENU plays again");
  } else if (hm.wrong >= HANGMAN_MAX_WRONG) {
    hm.done = true;
    setStatus("Out of guesses");
  }
}

// ---------------------------------------------------------------- Tic-Tac-Toe
struct TicTacToeState {
  char cells[9];  // ' ', 'X' (player), 'O' (AI)
  int cursor;
  bool over;
} ttt;

char tttWinner(const char* c) {
  static const int L[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                              {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};
  for (auto& l : L) {
    if (c[l[0]] != ' ' && c[l[0]] == c[l[1]] && c[l[1]] == c[l[2]]) return c[l[0]];
  }
  for (int i = 0; i < 9; i++) {
    if (c[i] == ' ') return ' ';
  }
  return 'D';  // draw
}

// Real minimax, not a heuristic — the 3x3 search space is small enough that there's no
// reason to ship a weaker opponent (docs/SIMULATOR.md makes the same point).
int tttMinimax(char* c, bool aiTurn) {
  char w = tttWinner(c);
  if (w == 'O') return 1;
  if (w == 'X') return -1;
  if (w == 'D') return 0;
  int best = aiTurn ? -2 : 2;
  for (int i = 0; i < 9; i++) {
    if (c[i] != ' ') continue;
    c[i] = aiTurn ? 'O' : 'X';
    int score = tttMinimax(c, !aiTurn);
    c[i] = ' ';
    if (aiTurn ? (score > best) : (score < best)) best = score;
  }
  return best;
}

void tttAiMove() {
  int bestIdx = -1, bestScore = -2;
  for (int i = 0; i < 9; i++) {
    if (ttt.cells[i] != ' ') continue;
    ttt.cells[i] = 'O';
    int score = tttMinimax(ttt.cells, false);
    ttt.cells[i] = ' ';
    if (score > bestScore) {
      bestScore = score;
      bestIdx = i;
    }
  }
  if (bestIdx >= 0) ttt.cells[bestIdx] = 'O';
}

void tttReportEnd() {
  char w = tttWinner(ttt.cells);
  if (w == ' ') return;
  ttt.over = true;
  setStatus(w == 'X' ? "You win. MENU restarts"
                     : (w == 'O' ? "AI wins. MENU restarts" : "Draw. MENU restarts"));
}

void tttStart() {
  for (int i = 0; i < 9; i++) ttt.cells[i] = ' ';
  ttt.cursor = 0;
  ttt.over = false;
  setStatus("You are X");
}

// ---------------------------------------------------------------- drawing helpers
void drawLightsOut(Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  const int cell = 16, n = LightsOutState::SIZE;
  int originX = (PAGE_WIDTH_PX - cell * n) / 2;
  int originY = 14;
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      int x = originX + c * cell, y = originY + r * cell;
      bool on = lo.grid[r][c];
      if (on) g.fillRect(x + 1, y + 1, cell - 2, cell - 2, INK);
      g.drawRect(x, y, cell, cell, INK);
      if (r * n + c == lo.cursor) cursorRect(g, x + 1, y + 1, cell - 2, cell - 2, on);
    }
  }
  centerText(u8f, 10, lo.won ? "Solved!" : "Lights Out");
}

void drawMinesweeper(Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  const int cell = 14;
  int originX = (PAGE_WIDTH_PX - cell * MinesweeperState::COLS) / 2;
  int originY = 16;
  for (int r = 0; r < MinesweeperState::ROWS; r++) {
    for (int c = 0; c < MinesweeperState::COLS; c++) {
      int x = originX + c * cell, y = originY + r * cell;
      bool filled = false;
      if (!ms.revealed[r][c]) {
        g.fillRect(x + 1, y + 1, cell - 2, cell - 2, INK);
        filled = true;
        if (ms.flagged[r][c]) {
          g.fillRect(x + 5, y + 4, 4, 4, PAPER);  // flag pip, punched out of the cover
        }
      } else if (ms.mine[r][c]) {
        g.fillCircle(x + cell / 2, y + cell / 2, 3, INK);
      } else {
        int n = msAdjacent(r, c);
        if (n > 0) {
          char buf[2] = {(char)('0' + n), 0};
          u8f.setCursor(x + 4, y + cell - 3);
          u8f.print(buf);
        }
      }
      g.drawRect(x, y, cell, cell, INK);
      if (r * MinesweeperState::COLS + c == ms.cursor) {
        cursorRect(g, x + 1, y + 1, cell - 2, cell - 2, filled);
      }
    }
  }
  centerText(u8f, 11, statusBuf);
}

void drawSudoku(Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  const int cell = 12;
  int originX = (PAGE_WIDTH_PX - cell * 9) / 2;
  int originY = (PAGE_HEIGHT_PX - cell * 9) / 2;
  for (int i = 0; i < 81; i++) {
    int r = i / 9, c = i % 9;
    int x = originX + c * cell, y = originY + r * cell;
    g.drawRect(x, y, cell, cell, INK);
    if (sd.grid[i]) {
      char buf[2] = {(char)('0' + sd.grid[i]), 0};
      u8f.setCursor(x + 3, y + cell - 3);
      u8f.print(buf);
    }
    if (i == sd.cursor) cursorRect(g, x + 1, y + 1, cell - 2, cell - 2, false);
  }
  // Heavier 3x3 box borders, drawn over the cell grid.
  for (int b = 0; b <= 3; b++) {
    g.drawFastVLine(originX + b * 3 * cell, originY, 9 * cell, INK);
    g.drawFastHLine(originX, originY + b * 3 * cell, 9 * cell, INK);
    g.drawFastVLine(originX + b * 3 * cell + 1, originY, 9 * cell, INK);
    g.drawFastHLine(originX, originY + b * 3 * cell + 1, 9 * cell, INK);
  }
}

void drawHangman(Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  // Gallows, revealed one part per wrong guess.
  const int baseX = 14, baseY = 96;
  g.drawFastHLine(baseX, baseY, 40, INK);
  g.drawFastVLine(baseX + 8, 20, baseY - 20, INK);
  g.drawFastHLine(baseX + 8, 20, 26, INK);
  g.drawFastVLine(baseX + 34, 20, 10, INK);
  if (hm.wrong > 0) g.drawCircle(baseX + 34, 35, 5, INK);
  if (hm.wrong > 1) g.drawFastVLine(baseX + 34, 40, 20, INK);
  if (hm.wrong > 2) g.drawLine(baseX + 34, 44, baseX + 26, 52, INK);
  if (hm.wrong > 3) g.drawLine(baseX + 34, 44, baseX + 42, 52, INK);
  if (hm.wrong > 4) g.drawLine(baseX + 34, 60, baseX + 26, 72, INK);
  if (hm.wrong > 5) g.drawLine(baseX + 34, 60, baseX + 42, 72, INK);

  // The word, with unguessed letters as underscores.
  String shown;
  for (const char* p = hm.word; *p; p++) {
    shown += (hm.guessed[*p - 'a'] || hm.done) ? *p : '_';
    shown += ' ';
  }
  u8f.setCursor(80, 40);
  u8f.print(shown);

  // Alphabet, four rows of seven, with the dial's current letter boxed.
  const int cols = 7, cw = 14, ch = 12, ax = 80, ay = 52;
  for (int i = 0; i < 26; i++) {
    int x = ax + (i % cols) * cw, y = ay + (i / cols) * ch;
    char buf[2] = {(char)('A' + i), 0};
    if (hm.guessed[i]) {
      u8f.setCursor(x + 3, y + ch - 3);
      u8f.print("-");
    } else {
      u8f.setCursor(x + 3, y + ch - 3);
      u8f.print(buf);
    }
    if (i == hm.cursor) g.drawRect(x, y - 1, cw - 1, ch, INK);
  }
  u8f.setCursor(80, 12);
  u8f.print(statusBuf);
}

void drawTicTacToe(Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  const int cell = 30;
  int originX = (PAGE_WIDTH_PX - cell * 3) / 2;
  int originY = (PAGE_HEIGHT_PX - cell * 3) / 2 + 4;
  for (int i = 0; i < 9; i++) {
    int r = i / 3, c = i % 3;
    int x = originX + c * cell, y = originY + r * cell;
    g.drawRect(x, y, cell, cell, INK);
    if (ttt.cells[i] == 'X') {
      g.drawLine(x + 7, y + 7, x + cell - 7, y + cell - 7, INK);
      g.drawLine(x + cell - 7, y + 7, x + 7, y + cell - 7, INK);
    } else if (ttt.cells[i] == 'O') {
      g.drawCircle(x + cell / 2, y + cell / 2, cell / 2 - 7, INK);
    }
    if (i == ttt.cursor) cursorRect(g, x + 1, y + 1, cell - 2, cell - 2, false);
  }
  centerText(u8f, 11, statusBuf);
}

}  // namespace

void games::start(Game g) {
  active = g;
  switch (g) {
    case Game::LightsOut: loStart(); break;
    case Game::Minesweeper: msStart(); break;
    case Game::Sudoku: sdStart(); break;
    case Game::Hangman: hmStart(); break;
    case Game::TicTacToe: tttStart(); break;
  }
}

void games::scroll(int delta) {
  switch (active) {
    case Game::LightsOut: {
      int n = LightsOutState::SIZE * LightsOutState::SIZE;
      lo.cursor = (lo.cursor + delta + n) % n;
      break;
    }
    case Game::Minesweeper: {
      int n = MinesweeperState::ROWS * MinesweeperState::COLS;
      ms.cursor = (ms.cursor + delta + n) % n;
      break;
    }
    case Game::Sudoku:
      sd.cursor = (sd.cursor + delta + 81) % 81;
      break;
    case Game::Hangman:
      hm.cursor = (hm.cursor + delta + 26) % 26;
      break;
    case Game::TicTacToe:
      ttt.cursor = (ttt.cursor + delta + 9) % 9;
      break;
  }
}

void games::primary() {
  switch (active) {
    case Game::LightsOut:
      if (lo.won) {
        loStart();
        break;
      }
      loPress(lo.cursor / LightsOutState::SIZE, lo.cursor % LightsOutState::SIZE);
      if (loSolved()) {
        lo.won = true;
        setStatus("Solved! MENU restarts");
      }
      break;
    case Game::Minesweeper: {
      if (ms.dead || ms.won) {
        msStart();
        break;
      }
      int r = ms.cursor / MinesweeperState::COLS, c = ms.cursor % MinesweeperState::COLS;
      if (!ms.placed) msPlaceMines(r, c);
      msReveal(r, c);
      if (!ms.dead) msCheckWin();
      break;
    }
    case Game::Sudoku:
      sdCycle(1);
      break;
    case Game::Hangman:
      hmGuess();
      break;
    case Game::TicTacToe:
      if (ttt.over) {
        tttStart();
        break;
      }
      if (ttt.cells[ttt.cursor] != ' ') break;
      ttt.cells[ttt.cursor] = 'X';
      tttReportEnd();
      if (!ttt.over) {
        tttAiMove();
        tttReportEnd();
      }
      break;
  }
}

void games::secondary() {
  switch (active) {
    case Game::Minesweeper: {
      if (ms.dead || ms.won) break;
      int r = ms.cursor / MinesweeperState::COLS, c = ms.cursor % MinesweeperState::COLS;
      if (!ms.revealed[r][c]) ms.flagged[r][c] = !ms.flagged[r][c];
      break;
    }
    case Game::Sudoku:
      sdCycle(-1);
      break;
    default:
      primary();  // everywhere else the dial's click is just another MENU
      break;
  }
}

void games::draw(Adafruit_GFX& gfx, U8G2_FOR_ADAFRUIT_GFX& u8f) {
  switch (active) {
    case Game::LightsOut: drawLightsOut(gfx, u8f); break;
    case Game::Minesweeper: drawMinesweeper(gfx, u8f); break;
    case Game::Sudoku: drawSudoku(gfx, u8f); break;
    case Game::Hangman: drawHangman(gfx, u8f); break;
    case Game::TicTacToe: drawTicTacToe(gfx, u8f); break;
  }
}

const char* games::statusText() {
  return statusBuf;
}
