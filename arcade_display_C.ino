/*
  ARCADE GAME - ARDUINO C: DISPLAY DRIVER
  ----------------------------------------
  This board knows nothing about the game. It receives a complete picture
  of the screen from Arduino B and paints it onto two stacked LCD modules,
  which together behave as one 16 character by 4 row display.

  Pin connections:
    D0 (RX) <- Arduino B, pin D1 (TX)   (screen data in)
    A4 (SDA) -> SDA pin of BOTH LCD modules
    A5 (SCL) -> SCL pin of BOTH LCD modules
    5V       -> VCC pin of both LCD modules
    GND      -> GND pin of both LCD modules, and to Arduino B GND

  Both LCDs share the same two signal wires. They are told apart only by
  their I2C address, so the two modules must be set to different addresses.

  IMPORTANT: the serial port on this board is the data link from Arduino B.
  Do not add Serial.print() debugging here. Anything printed would travel
  out on pin D1 and could interfere with the link on real hardware.
*/

#include <Adafruit_LiquidCrystal.h>

// ----- Displays -----
// The number in brackets is an offset from the base I2C address 0x20.
// lcd(0) is address 0x20 (32), lcd(1) is address 0x21 (33).
Adafruit_LiquidCrystal lcdTop(0);     // shows screen rows 0 and 1
Adafruit_LiquidCrystal lcdBottom(1);  // shows screen rows 2 and 3

// ----- Settings -----
const int COLS        = 16;  // characters per row
const int FRAME_CELLS = 64;  // 4 rows of 16, sent as one message
const char FRAME_START = '{';
const char FRAME_END   = '}';

// ----- Receive buffer -----
char incoming[FRAME_CELLS];  // the frame currently being received
char shown[FRAME_CELLS];     // what is already on the LCDs right now
int  count = 0;              // how many characters of this frame have arrived
bool receiving = false;      // true once a start marker has been seen

// Writes one character to the correct physical LCD.
// Screen rows 0 and 1 live on the top module. Rows 2 and 3 live on the
// bottom module, which numbers its own rows 0 and 1, so we subtract 2.
void putChar16x4(int col, int row, char c) {
  if (row < 2) {
    lcdTop.setCursor(col, row);
    lcdTop.print(c);
  } else {
    lcdBottom.setCursor(col, row - 2);
    lcdBottom.print(c);
  }
}

// Draws the received frame, but only the cells that are different from
// what is already on screen. Repainting all 64 cells every time would be
// far too slow to look like smooth animation.
void drawFrame() {
  for (int i = 0; i < FRAME_CELLS; i++) {
    if (incoming[i] != shown[i]) {
      putChar16x4(i % COLS, i / COLS, incoming[i]);  // position to column and row
      shown[i] = incoming[i];
    }
  }
}

void setup() {
  // Both modules are 16 columns by 2 rows.
  lcdTop.begin(COLS, 2);
  lcdBottom.begin(COLS, 2);
  lcdTop.setBacklight(1);
  lcdBottom.setBacklight(1);

  // Startup message, replaced as soon as the first frame arrives from B.
  lcdTop.print("Waiting for B...");

  // Fill the "already shown" buffer with a character that can never appear
  // in a real frame. That forces the first frame to redraw every cell.
  for (int i = 0; i < FRAME_CELLS; i++) {
    shown[i] = 0;
  }

  // 9600 baud must match the speed Arduino B is transmitting at.
  Serial.begin(9600);
}

void loop() {
  // ----- 1) Receive characters from Arduino B -----
  // Serial.available() reports how many characters have arrived and are
  // waiting to be read. We handle all of them before doing anything else.
  while (Serial.available()) {
    char c = Serial.read();

    if (c == FRAME_START) {
      // A new frame begins. Throw away anything received before this point,
      // which matters if this board started listening mid-message.
      receiving = true;
      count = 0;

    } else if (c == FRAME_END) {
      // ----- 2) Draw, but only if the frame is complete -----
      // A frame is only trusted if it had a start marker and contained
      // exactly 64 characters. Anything else is silently discarded, so a
      // damaged frame leaves the previous picture on screen instead of
      // filling it with garbage.
      if (receiving && count == FRAME_CELLS) {
        drawFrame();
      }
      receiving = false;

    } else if (receiving && count < FRAME_CELLS) {
      // A normal screen character. Store it and move along.
      incoming[count] = c;
      count++;
    }
    // Any character arriving outside a frame is ignored on purpose.
  }
}
