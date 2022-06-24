/*
	 This file is part of YAESU-ICOM-TUNERADAPTER.

    This project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This project is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this project.  If not, see http://www.gnu.org/licenses/;.	  

    Copyright 2022 Christian Obersteiner, DL1COM
*/

#include <Arduino.h>

#include <SoftwareSerial.h>
#include "buttonhw.h"

#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)

#define PIN_BTN 2
#define PIN_LED 13
#define PIN_START 5
#define PIN_KEY 6

#define PIN_CAT_RX 7
#define PIN_CAT_TX 8

#define CAT_BAUD 4800

typedef enum ProgramState {
  STATE_NONE = 0,
  STATE_IDLE = 1,
  STATE_START_TUNING = 2,
  STATE_WAIT_FOR_TUNER = 3,
  STATE_WAIT_FOR_FAIL = 4
}ProgramState_t;

SoftwareSerial SerialCAT(PIN_CAT_RX, PIN_CAT_TX);
ButtonHW ptt_button(PIN_BTN);
ProgramState_t current_state = STATE_NONE;

// Stores the Transceiver mode to recover it after tuning
String trx_mode = "";

void getTrxSettings() {
  // empy receive buffer
  while(SerialCAT.available()){
    SerialCAT.read();
  }

  SerialCAT.print("MD0;"); // get Mode
  delay(100);
  while(SerialCAT.available()){
    trx_mode = SerialCAT.readStringUntil(";");
  }
  DEBUG_PRINT("Mode: ");
  DEBUG_PRINTLN(trx_mode);


}

void restoreTrxSettings() {  
  SerialCAT.print(trx_mode);
}

void setToTuningMode() {
  SerialCAT.print("MD03;"); // CW
  SerialCAT.print("PC010;"); // 10 W
}

void setPTTOn() {
  DEBUG_PRINTLN("PTT on");
  SerialCAT.print("TX1;");
}

void setPTTOff() {
  DEBUG_PRINTLN("PTT off");
  SerialCAT.print("TX0;");
}

void setup() {
  pinMode(PIN_BTN, INPUT);
  pinMode(PIN_START, OUTPUT);
  pinMode(PIN_KEY, INPUT);
  pinMode(PIN_LED, OUTPUT);

  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Native USB only
  }  

  SerialCAT.begin(CAT_BAUD);

  current_state = STATE_IDLE;

  DEBUG_PRINTLN("YAEUS-ICOM-TUNERADAPTER");
}

void loop() {
  ptt_button.update();

  static bool button_pressed = false;
  static long timestamp = 0;

  switch(current_state) {
    case STATE_IDLE:
      if (ptt_button.isPressedEdge()) {
        button_pressed = true;
        timestamp = millis();
      }

      // When button is released, decide what to do
      // Short press -> tune
      // Long press (>500ms) -> set tuner to bypass
      if (ptt_button.isReleasedEdge() && button_pressed){
        button_pressed = false;
        long duration = millis() - timestamp;

        // Short press
        if (duration < 500) {
          DEBUG_PRINTLN("Start tuning");
          // Get and store current mode
          getTrxSettings();
          // Set to CW and 10 W
          setToTuningMode();

          // Ask tuner to start and transmit
          digitalWrite(PIN_START, HIGH);
          timestamp = millis();
          current_state = STATE_START_TUNING;
          setPTTOn();
          delay(100); // Delay next state to allow the tuner to react
        } 
        else // Long press
        {
          DEBUG_PRINTLN("Resetting Tuner");
          digitalWrite(PIN_START, HIGH);
          delay(70); // 70 ms should indicate the tuner to bypass
          digitalWrite(PIN_START, LOW);
          current_state = STATE_IDLE;
        }
      }
      break;
    case STATE_START_TUNING:
      if (digitalRead(PIN_KEY) == LOW) {
        // Tuner has reacted, we can de-assert START
        DEBUG_PRINTLN("Wait for tuner");
        digitalWrite(PIN_START, LOW);        
        current_state = STATE_WAIT_FOR_TUNER;        
      }
      break;
    case STATE_WAIT_FOR_TUNER:
      if (digitalRead(PIN_KEY) == HIGH) {
        // Tuner has finished tuning, now we wait if it asserts the KEY
        // line again to indicate a failure
        DEBUG_PRINTLN("Wait for fail");
        setPTTOff();
        restoreTrxSettings();

        timestamp = millis();
        current_state = STATE_WAIT_FOR_FAIL;        
      }
      break;
    case STATE_WAIT_FOR_FAIL:
      if (digitalRead(PIN_KEY) == LOW) {
        // Tuner has indicated a fail by pulling KEY line low again
        DEBUG_PRINTLN("Tuning failed");
        digitalWrite(PIN_LED, HIGH);
        current_state = STATE_IDLE;
        return;
      } else if (timestamp + 300 < millis()) {
        // Tuner didn't indicate a fail within the given time, should be ok
        current_state = STATE_IDLE;
        DEBUG_PRINTLN("Tuning success");
        digitalWrite(PIN_LED, LOW);
        return;
      }
      break;
    default:
      break;
  }
}
