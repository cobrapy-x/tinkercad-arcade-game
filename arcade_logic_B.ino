/*
  ARCADE GAME - ARDUINO B: GAME LOGIC
  ------------------------------------
  This board runs the whole game. It holds the road in memory, moves the
  obstacles, tracks the player and the score, and sends a complete picture
  of the screen to Arduino C.

  Pin connections:
    D2      <- Arduino A, pin D8   (LEFT signal in)
    D3      <- Arduino A, pin D9   (RIGHT signal in)
    D1 (TX) -> Arduino C, pin D0 (RX)   (screen data out)
    GND     -> Arduino A GND and Arduino C GND  (shared reference, required)
    A0      -- deliberately left unconnected, used as a randomness source

  IMPORTANT: the serial port on this board is the data link to Arduino C.
  Do not add Serial.print() debugging here. Any extra text would be mixed
  into the screen data and would corrupt the picture Arduino C draws.
*/

// ----- Pins -----
const int IN_LEFT   = 2;   // HIGH while A's left button is held
const int IN_RIGHT  = 3;   // HIGH while A's right button is held
const int SEED_PIN  = A0;  // floating pin, read once for random noise

// ----- Screen and road size -----
const int LANES      = 3;   // the road is 3 lanes tall
const int COLS       = 16;  // each lane is 16 characters wide
const int PLAYER_COL = 1;   // the player never moves sideways, only up and down
const int FRAME_CELLS = 64; // 3 road lanes plus 1 status line, 16 chars each

// ----- Characters drawn on screen -----
const char PLAYER     = '>';  // the player while alive
const char PLAYER_DEAD = 'X'; // the player after a crash
const char ROCK       = '#';  // an obstacle
const char EMPTY      = ' ';  // clear road

// ----- Game states -----
const int STATE_WAITING = 0;
const int STATE_PLAYING = 1;
const int STATE_CRASHED = 2;

// ----- Settings -----
const unsigned long START_TICK_MS   = 400;  // ms between obstacle moves at the start
const unsigned long MIN_TICK_MS     = 150;  // speed-up stops once below this
const unsigned long TICK_STEP_MS    = 30;   // how much faster per difficulty step
const int           SCORE_PER_STEP  = 20;   // score interval between speed-ups
const unsigned long SEND_INTERVAL   = 100;  // shortest gap between frames sent to C
const unsigned long RESTART_LOCK_MS = 1000; // ignore presses this long after a crash
const int           SPAWN_CHANCE    = 45;   // percent chance of a new obstacle per tick
const int           SPAWN_COOLDOWN  = 2;    // ticks to wait after spawning one

// ----- Game state -----
char road[LANES][COLS];   // the road itself, one character per cell
int  playerLane = 1;      // 0 is the top lane, 2 is the bottom lane
int  score = 0;
int  state = STATE_WAITING;

unsigned long tickTime = START_TICK_MS;  // current ms between obstacle moves
unsigned long lastTick = 0;              // when the last obstacle move happened
unsigned long lastSend = 0;              // when the last frame was sent
unsigned long gameOverTime = 0;          // when the crash happened
bool needSend = true;                    // set when the screen has changed
int  cooldown = 0;                       // stops obstacles spawning back to back

// Previous readings of the two signal wires, used for edge detection.
bool lastLeft  = false;
bool lastRight = false;

// Fills every cell of the road with blank space.
void clearRoad() {
  for (int lane = 0; lane < LANES; lane++) {
    for (int col = 0; col < COLS; col++) {
      road[lane][col] = EMPTY;
    }
  }
}

// Resets everything and puts the game into the playing state.
void startGame() {
  clearRoad();
  playerLane = 1;              // start in the middle lane
  score = 0;
  tickTime = START_TICK_MS;
  cooldown = 3;                // small grace period before the first obstacle
  state = STATE_PLAYING;
  lastTick = millis();
  needSend = true;
}

// Switches to the crashed state and notes the time, so the restart lockout works.
void crash() {
  state = STATE_CRASHED;
  gameOverTime = millis();
}

// Moves the player one lane. direction is -1 for up, +1 for down.
void movePlayer(int direction) {
  int newLane = playerLane + direction;
  if (newLane < 0 || newLane >= LANES) return;  // cannot leave the road
  playerLane = newLane;

  // Moving sideways into an occupied cell counts as hitting the obstacle.
  if (road[playerLane][PLAYER_COL] == ROCK) crash();

  needSend = true;
}

// One step of the game: the whole road shifts left, then maybe a new obstacle
// appears at the far right, then we check whether the player has been hit.
void gameTick() {
  // Slide every lane one column to the left. The leftmost column falls off
  // the end and the rightmost column is cleared, ready for a new obstacle.
  for (int lane = 0; lane < LANES; lane++) {
    for (int col = 0; col < COLS - 1; col++) {
      road[lane][col] = road[lane][col + 1];
    }
    road[lane][COLS - 1] = EMPTY;
  }

  // Decide whether to spawn a new obstacle at the right edge.
  if (cooldown > 0) {
    cooldown--;                                  // too soon after the last one
  } else if (random(100) < SPAWN_CHANCE) {
    int lane = random(300) / 100;                // gives 0, 1 or 2
    if (lane > 2) lane = 2;                      // safety net, should never trigger
    road[lane][COLS - 1] = ROCK;
    cooldown = SPAWN_COOLDOWN;
  }

  // Did an obstacle just slide into the player's cell?
  if (road[playerLane][PLAYER_COL] == ROCK) {
    crash();
  } else {
    score++;
    // Every SCORE_PER_STEP points, shorten the tick so the game speeds up.
    if (score % SCORE_PER_STEP == 0 && tickTime > MIN_TICK_MS) {
      tickTime -= TICK_STEP_MS;
    }
  }

  needSend = true;
}

// Builds and transmits one complete 66 byte frame to Arduino C.
// Format: '{' then 48 road characters, then 16 status characters, then '}'.
void sendFrame() {
  // Build the bottom status line. It must be exactly 16 characters, because
  // Arduino C counts characters and throws away frames of the wrong length.
  char bottom[17];
  if (state == STATE_WAITING) {
    strcpy(bottom, "Press to start! ");
  } else if (state == STATE_PLAYING) {
    snprintf(bottom, 17, "Score: %-9d", score);   // %-9d pads the number to 9 wide
  } else {
    snprintf(bottom, 17, "CRASH! Score%4d", score);
  }

  Serial.write('{');   // start marker, tells C a new frame begins here

  // Send the three road lanes. The player is not stored in the road array,
  // so it is inserted into the stream here as the frame is built.
  for (int lane = 0; lane < LANES; lane++) {
    for (int col = 0; col < COLS; col++) {
      char c = road[lane][col];
      if (lane == playerLane && col == PLAYER_COL) {
        c = (state == STATE_CRASHED) ? PLAYER_DEAD : PLAYER;
      }
      Serial.write(c);
    }
  }

  // Send the 16 character status line as the fourth screen row.
  for (int i = 0; i < COLS; i++) {
    Serial.write(bottom[i]);
  }

  Serial.write('}');   // end marker, tells C the frame is complete
}

void setup() {
  // These two pins receive signals from Arduino A. No pull-up is used here,
  // because A actively drives each wire HIGH or LOW.
  pinMode(IN_LEFT,  INPUT);
  pinMode(IN_RIGHT, INPUT);

  // 9600 baud must match the speed Arduino C is listening at.
  Serial.begin(9600);

  // Reading an unconnected analogue pin returns electrical noise, which is
  // different on every power-up. This makes each game's obstacles different.
  randomSeed(analogRead(SEED_PIN));

  clearRoad();
}

void loop() {
  // ----- 1) Read the signals coming from Arduino A -----
  bool left  = (digitalRead(IN_LEFT)  == HIGH);
  bool right = (digitalRead(IN_RIGHT) == HIGH);

  // A "new press" means the wire is HIGH now but was LOW last time round.
  // Without this check, holding a button would move the player every single
  // loop, which is thousands of times per second.
  bool leftPressed  = left  && !lastLeft;
  bool rightPressed = right && !lastRight;
  lastLeft  = left;
  lastRight = right;

  // ----- 2) Update the game -----
  if (state == STATE_PLAYING) {
    if (leftPressed)  movePlayer(-1);   // up one lane
    if (rightPressed) movePlayer(+1);   // down one lane

    // Advance the obstacles once enough time has passed. Checking millis()
    // instead of using delay() means button presses are still noticed
    // while we wait for the next step.
    if (state == STATE_PLAYING && millis() - lastTick >= tickTime) {
      lastTick = millis();
      gameTick();
    }
  } else if (leftPressed || rightPressed) {
    // Either we are waiting to start, or we crashed. After a crash, ignore
    // presses for a second so the fatal press does not restart the game
    // before the player has read their score.
    if (state == STATE_WAITING || millis() - gameOverTime > RESTART_LOCK_MS) {
      startGame();
    }
  }

  // ----- 3) Send the screen to Arduino C -----
  // Only send when something actually changed, and never more often than
  // SEND_INTERVAL, because one frame takes about 69 ms to transmit at 9600 baud.
  if (needSend && millis() - lastSend >= SEND_INTERVAL) {
    sendFrame();
    lastSend = millis();
    needSend = false;
  }
}
