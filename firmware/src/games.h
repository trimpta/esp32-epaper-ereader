#pragma once
// The five games the simulator ships (docs/SIMULATOR.md "Games"), ported to the panel.
// All of them reuse the reader's input model rather than inventing controls: the rotary
// moves a cursor, MENU acts on the focused cell, EXIT leaves. Only Minesweeper and Sudoku
// use the rotary's own click as a distinct second action (flag / cycle down), same split
// the simulator makes.
//
// docs/FLASH_BUDGET.md sizes all of this as "ordinary C++ business logic" — the largest
// state here is Sudoku's 81-cell grid plus its given-mask.

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>

enum class Game { LightsOut, Minesweeper, Sudoku, Hangman, TicTacToe };

namespace games {
void start(Game g);
void scroll(int delta);  // rotary detent: moves the cursor
void primary();          // MENU: toggle / reveal / cycle up / guess / place
void secondary();        // rotary click: flag (Minesweeper), cycle down (Sudoku), else primary
void draw(Adafruit_GFX& gfx, U8G2_FOR_ADAFRUIT_GFX& u8f);
const char* statusText();
}  // namespace games
