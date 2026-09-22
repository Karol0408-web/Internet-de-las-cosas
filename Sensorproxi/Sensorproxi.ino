#include <NewPing.h>

#define TRIGGER_PIN 33
#define ECHO_PIN 32
#define MAX_DISTANCE 400

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

void setup() {
  Serial.begin(115200);
}

void loop() {
  delay(100);

  unsigned int distance = sonar.ping_cm();

  Serial.print("Distancia: ");

  if (distance == 0) {
    Serial.println("SIN ECO");
  } else {
    Serial.print(distance);
    Serial.println(" cm");
  }
}