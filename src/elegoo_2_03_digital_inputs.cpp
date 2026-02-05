#include <Arduino.h>n//www.elegoo.com
//2016.12.08

int ledPin = 18;
int buttonApin = 19;
int buttonBpin = 21;

void setup() 
{
  pinMode(ledPin, OUTPUT);
  pinMode(buttonApin, INPUT_PULLUP);  
  pinMode(buttonBpin, INPUT_PULLUP);  
}

void loop() 
{
  if (digitalRead(buttonApin) == LOW)
  {
    digitalWrite(ledPin, HIGH);
  }

  if (digitalRead(buttonBpin) == LOW)
 {
    digitalWrite(ledPin, LOW);
  }
}
