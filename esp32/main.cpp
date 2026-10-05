// Temporary USB-CDC echo firmware (Task 1). Replaced by the engine in Task 10.
#include <Arduino.h>

void setup() {
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
}

void loop() {
    static String line;
    while (Serial.available()) {
        char c = char(Serial.read());
        if (c == '\n') { Serial.println("echo " + line); line = ""; }
        else if (c != '\r') line += c;
    }
    delay(1);
}
