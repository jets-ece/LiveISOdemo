#define LED_PIN 2

enum FsmState { IDLE_ST, RUNNING_ST, PAUSED_ST, ERR_ST };
static const char *const names[] = {"IDLE", "RUNNING", "PAUSED", "ERROR"};

FsmState state = IDLE_ST;
String line;

void sendState() {
  Serial.print("STATE ");
  Serial.println(names[state]);
}

void handle(String cmd) {
  const char *err = NULL;
  if (cmd == "START") {
    if (state == IDLE_ST) state = RUNNING_ST; else err = "START needs IDLE";
  } else if (cmd == "PAUSE") {
    if (state == RUNNING_ST) state = PAUSED_ST; else err = "PAUSE needs RUNNING";
  } else if (cmd == "RESUME") {
    if (state == PAUSED_ST) state = RUNNING_ST; else err = "RESUME needs PAUSED";
  } else if (cmd == "FAULT") {
    state = ERR_ST;
  } else if (cmd == "RESET") {
    state = IDLE_ST;
  } else if (cmd == "STATUS") {
    /* no state change */
  } else {
    err = "unknown command";
  }
  if (err != NULL) {
    Serial.print("ERR ");
    Serial.println(err);
  }
  digitalWrite(LED_PIN, state == RUNNING_ST ? HIGH : LOW);
  sendState();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(200);
  Serial.println("READY");
  sendState();
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      line.toUpperCase();
      if (line.length() > 0) handle(line);
      line = "";
    } else if (line.length() < 40) {
      line += c;
    }
  }
}

