/* ===== ARDUINO B - GAME LOGIC + IR INPUT =====
   IR receiver OUT -> D11   (change IR_PIN if you wired another pin)
   IR receiver VCC -> 5V
   IR receiver GND -> GND
   D1 (TX)         -> display Arduino D0 (RX)
   GND             -> display Arduino GND   (shared ground is required)

   Remote:
     Power   start a new game / restart after a crash
     Pause   freeze the game, press again to resume
     UP      move up a lane
     DOWN    move down a lane
   All other buttons are ignored.
*/

#include <IRremote.hpp>

// ---------- IR input ----------
const int IR_PIN = 11;

const uint8_t KEY_POWER = 0x00;   // power off/on
const uint8_t KEY_PAUSE = 0x05;   // pause/play
const uint8_t KEY_DOWN  = 0x08;   // triangle down
const uint8_t KEY_UP    = 0x0A;   // triangle up

const unsigned long KEY_GAP = 120;   // ms, swallows double-decodes
unsigned long lastKeyTime = 0;

uint8_t lastKey = 0;
bool havePress = false;

// ---------- game ----------
const int LANES = 3;
const int COLS = 16;
const int PLAYER_COL = 1;
const char PLAYER = '>';
const char ROCK = '#';

char road[LANES][COLS];
int playerLane = 1;            // 0 = top, 1 = middle, 2 = bottom
int state = 0;                 // 0 waiting, 1 playing, 2 crashed, 3 paused
int score = 0;

unsigned long tickTime = 400;
unsigned long lastTick = 0;
unsigned long lastSend = 0;
unsigned long gameOverTime = 0;
bool needSend = true;
int cooldown = 0;

void clearRoad() {
  for (int lane = 0; lane < LANES; lane++)
    for (int col = 0; col < COLS; col++)
      road[lane][col] = ' ';
}

void startGame() {
  clearRoad();
  playerLane = 1;
  score = 0;
  tickTime = 400;
  cooldown = 3;
  state = 1;
  lastTick = millis();
  needSend = true;
}

// Send the whole 16x4 screen to the display Arduino: { + 64 chars + }
void sendFrame() {
  char bottom[17];
  if (state == 0)      strcpy(bottom, "Press PWR start ");
  else if (state == 1) snprintf(bottom, 17, "Score: %-9d", score);
  else if (state == 3) snprintf(bottom, 17, "PAUSED Score%4d", score);
  else                 snprintf(bottom, 17, "CRASH! Score%4d", score);

  Serial.write('{');
  for (int lane = 0; lane < LANES; lane++) {
    for (int col = 0; col < COLS; col++) {
      char c = road[lane][col];
      if (lane == playerLane && col == PLAYER_COL) {
        c = (state == 2) ? 'X' : PLAYER;
      }
      Serial.write(c);
    }
  }
  for (int i = 0; i < 16; i++) Serial.write(bottom[i]);
  Serial.write('}');
}

void crash() {
  state = 2;
  gameOverTime = millis();
}

void movePlayer(int direction) {
  int newLane = playerLane + direction;
  if (newLane < 0 || newLane >= LANES) return;
  playerLane = newLane;
  if (road[playerLane][PLAYER_COL] == ROCK) crash();
  needSend = true;
}

void gameTick() {
  for (int lane = 0; lane < LANES; lane++) {
    for (int col = 0; col < COLS - 1; col++) {
      road[lane][col] = road[lane][col + 1];
    }
    road[lane][COLS - 1] = ' ';
  }

  if (cooldown > 0) {
    cooldown--;
  } else if (random(100) < 45) {
    int lane = random(3);
    road[lane][COLS - 1] = ROCK;
    cooldown = 2;
  }

  if (road[playerLane][PLAYER_COL] == ROCK) {
    crash();
  } else {
    score++;
    if (score % 20 == 0 && tickTime > 150) tickTime -= 30;
  }
  needSend = true;
}

// Grab at most one fresh keypress per loop
void readRemote() {
  havePress = false;
  if (!IrReceiver.decode()) return;

  uint8_t key = IrReceiver.decodedIRData.command;
  bool isRepeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
  IrReceiver.resume();                 // always resume, even if we ignore it

  if (isRepeat) return;                // holding a button does not repeat
  if (millis() - lastKeyTime < KEY_GAP) return;
  lastKeyTime = millis();

  lastKey = key;
  havePress = true;
}

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);
  randomSeed(analogRead(A0));
  clearRoad();
}

void loop() {
  readRemote();

  // What a key does depends entirely on which state we are in
  if (havePress) {
    if (state == 0) {
      if (lastKey == KEY_POWER) startGame();

    } else if (state == 1) {
      if      (lastKey == KEY_UP)    movePlayer(-1);
      else if (lastKey == KEY_DOWN)  movePlayer(+1);
      else if (lastKey == KEY_PAUSE) { state = 3; needSend = true; }

    } else if (state == 2) {
      // 1 second lockout so the crash keypress does not restart instantly
      if (lastKey == KEY_POWER && millis() - gameOverTime > 1000) startGame();

    } else if (state == 3) {
      if (lastKey == KEY_PAUSE) {
        state = 1;
        lastTick = millis();         // do not tick the instant we resume
        needSend = true;
      } else if (lastKey == KEY_POWER) {
        startGame();                 // power abandons the paused game
      }
    }
  }

  // The world only moves while playing
  if (state == 1 && millis() - lastTick >= tickTime) {
    lastTick = millis();
    gameTick();
  }

  if (needSend && millis() - lastSend >= 100) {
    sendFrame();
    lastSend = millis();
    needSend = false;
  }
}
