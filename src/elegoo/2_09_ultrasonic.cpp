#include <Arduino.h>

//www.elegoo.com
//2016.11.20
// Rewritten to not require SR04 library

#define TRIG_PIN 18
#define ECHO_PIN 19

long getDistance() {
  // Send trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Measure echo pulse duration
  long duration = pulseIn(ECHO_PIN, HIGH);

  // Calculate distance in cm (speed of sound = 343m/s = 0.0343cm/us)
  // Distance = duration * 0.0343 / 2
  long distance = duration * 0.0343 / 2;

  return distance;
}

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  delay(1000);
}

void loop() {
  long a = getDistance();
  Serial.print(a);
  Serial.println("cm");
  delay(1000);
}
