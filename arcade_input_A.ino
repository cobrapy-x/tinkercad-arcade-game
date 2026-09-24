/*
  ARCADE GAME - ARDUINO A: INPUT HANDLER
  ---------------------------------------
  This board does one job: read two buttons and report their state
  to Arduino B over two plain wires.

  Pin connections:
    D2  <- Left pushbutton  (other leg of the button goes to GND)
    D3  <- Right pushbutton (other leg of the button goes to GND)
    D8  -> Arduino B, pin D2   (LEFT signal out)
    D9  -> Arduino B, pin D3   (RIGHT signal out)
    GND -> Arduino B, GND      (shared reference, required)

  No external resistors are used. The buttons rely on the pull-up
  resistors built into the Arduino chip.
*/

// ----- Pins -----
const int BTN_LEFT  = 2;   // left button reads LOW when pressed
const int BTN_RIGHT = 3;   // right button reads LOW when pressed
const int OUT_LEFT  = 8;   // held HIGH while the left button is down
const int OUT_RIGHT = 9;   // held HIGH while the right button is down

// ----- Settings -----
const int DEBOUNCE_MS = 20;  // short pause to ignore contact bounce

// ----- State -----
// These remember the previous reading so we can spot the exact moment
// a button goes from not-pressed to pressed.
bool lastLeft  = false;
bool lastRight = false;

// Returns true if the button on this pin is currently held down.
// The test is == LOW because INPUT_PULLUP holds an untouched pin HIGH,
// and pressing the button connects the pin to GND.
bool isPressed(int pin) {
  return digitalRead(pin) == LOW;
}

void setup() {
  // INPUT_PULLUP switches on a resistor inside the chip that holds the
  // pin at 5 V. This is what lets us wire the button straight to GND.
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  // These two pins drive wires going to Arduino B.
  pinMode(OUT_LEFT,  OUTPUT);
  pinMode(OUT_RIGHT, OUTPUT);

  // Start both signal wires LOW so B does not see a false press at boot.
  digitalWrite(OUT_LEFT,  LOW);
  digitalWrite(OUT_RIGHT, LOW);

  Serial.begin(9600);
  Serial.println("Arduino A ready");  // startup message, this board has no display
}

void loop() {
  // ----- 1) Read the buttons -----
  bool left  = isPressed(BTN_LEFT);
  bool right = isPressed(BTN_RIGHT);

  // ----- 2) Copy the state onto the signal wires -----
  // The wire stays HIGH for as long as the button is physically held.
  // Arduino B is the board that decides what counts as a new press.
  digitalWrite(OUT_LEFT,  left  ? HIGH : LOW);
  digitalWrite(OUT_RIGHT, right ? HIGH : LOW);

  // ----- 3) Serial Monitor -----
  // Print only on the rising edge, meaning the first loop where the button
  // is down but was up last time. Without this check the monitor would
  // fill with hundreds of lines per second while a button is held.
  if (left  && !lastLeft)  Serial.println("LEFT pressed");
  if (right && !lastRight) Serial.println("RIGHT pressed");

  // ----- 4) Remember this reading for next time -----
  lastLeft  = left;
  lastRight = right;

  // A mechanical button does not switch cleanly. Its contacts bounce for a
  // few milliseconds, which the Arduino would read as many rapid presses.
  // Pausing here steps over that noise.
  delay(DEBOUNCE_MS);
}
