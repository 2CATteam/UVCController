#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

//Instantiate LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

//Pins for panel buttons and encoder
#define encoderB 4
#define encoder1 2
#define encoder2 3
//#define reset 4

//Pins for door switches, solenoids, and relays
#define doorSwitch A0
#define doorOut 38
#define lights 23

#define low_threshold 1
#define high_threshold 250

//Counter is always the variable the encoder changes. Does different things across the states.
volatile int counter = 0;

//Keeps track of needed information about the current run
int seconds = 0;
float rads = 0;
int toRun = 0;

//Keeps track of when to start counting from
unsigned long start = 0;
//Emissivity is calculated by taking readings over tiny intervals. 
unsigned long rad_last_checked = 0;

//Machine is modeled as a finite state machine. Keeps track of state and things to print for each state.
int state = 0;
int prevState = -1;
String states[][4] = {
  {" Warm up"," Open doors"," Start run cycle",""}, //Home screen
  {"","","     (W/cm^2)",""}, //Selecting emissivity running
  {"","      Run for:","",""}, //Selecting time done
  {" Time done: ",""," Pause"," Cancel"}, //Running em
  {" Time left: ",""," Pause"," Cancel"}, //Running time
  {"Paused because door opened!","","Close door to resume","Click to cancel"}, //Paused (Em)
  {"Paused because door opened!","","Close door to resume","Click to cancel"}, //Paused (Time)
  {"","Paused manually.","Click to continue",""}, //Paused (Em)
  {"","Paused manually.","Click to continue",""}, //Paused (Time)
  {" Time done: "," Still warming up "," Pause"," Cancel"},
  {"","Paused manually.","Click to continue",""},
  {"Paused because door opened!","","Close door to resume","Click to cancel"},
  {"","     Batch done"," Click to continue",""}
};

//Holds stuff about to be printed. Used for strcpy
char toPrint[20] = "";

//Makes it so button isn't constantly triggering.
bool enableButton = true;

//Keeps track of if the doors are open
bool doorsOpen = false;

void setup() {

  //Begin communication to screen
  lcd.init();
  lcd.init();
  lcd.backlight();

  //Show startup
  lcd.print("Please wait...");
  pinMode(encoderB, INPUT_PULLUP);
  pinMode(encoder1, INPUT_PULLUP);
  pinMode(encoder2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoder1), count, CHANGE);
  pinMode(doorOut, OUTPUT);
  pinMode(lights, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  digitalWrite(8, HIGH);
  digitalWrite(9, LOW);
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);
  analogReference(INTERNAL1V1);

  noInterrupts();
  
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4  = 0;
  OCR4A = 65535;
  TCCR4B |= (1 << WGM12);
  TIMSK4 |= (1 << OCIE4A);

  interrupts();

  //Start serial
  Serial.begin(115200);
}

void loop() {
  //tone(buzzer, 60);
  doLCD();
  doFunctions();
}

void doFunctions() {
  switch (state) {
    //Base case needs to let you move to selected state
    case 0:
      //When the button is pushed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Load proper config values and state shift
        if (counter % 3 == 1) {
          Serial.println("Opening doors");
          openDoors();
        /*} else if (counter % 4 == 3) {
          EEPROM.get(0, toRun);
          counter = 0;
          state = 1;*/
        } else if (counter % 3 == 2) {
          EEPROM.get(8, toRun);
          counter = 0;
          state = 2;
        } else {
          state = 9;
          start = millis();
          toRun = 180;
          counter = 0;
          on();
        }
      }
      break;
      
    //Emission-based run config - set rad goal and start counting
    case 1:
      //When the button is pushed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Initialize for emission - state shift, save toRun, set start time, and set rad time.
        state = 3;
        toRun = toRun + counter * 5;
        EEPROM.put(0, toRun);
        start = millis();
        rad_last_checked = millis();
        on();
      }
      break;
      
    //Time-based run config - set time goal and start counting.
    case 2:
      //When the button is pushed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Initialize for time - state shift, save toRun, set start time, and set rad time.
        state = 4;
        toRun = toRun + counter * 5;
        EEPROM.put(8, toRun);
        start = millis();
        rad_last_checked = millis();
        on();
      }
      break;
      
    //Emission-based running - allow for pausing and canceling, and check for doors
    case 3:
      //When the button is pushed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Pause or cancel
        if (counter % 2 == 0) {
          //State shift and save rads left
          off();
          state = 7;
          toRun = rads;
        } else {
          //Cancel and go to base state
          off();
          state = 0;
        }
      }
      //When done, beep and finish
      if (rads <= 0) {
        off();
        state = 12;
      }
      
      Serial.println(analogRead(doorSwitch));
      //Pause when door is open
      if (analogRead(doorSwitch) == 0) {
        Serial.println("Em-based flip");
        off();
        openDoors();
        state = 5;
      }
      break;
      
    //Time-based running - allow for pausing and canceling, and check for doors
    case 4:
      //When the button is pressed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Pause or cancel
        if (counter % 2 == 0) {
          //State shift and save seconds left
          off();
          state = 8;
          toRun = seconds;
        } else {
          off();
          state = 0;
        }
      }
      //When done, beep and finish
      if (seconds <= 0) {
        off();
        state = 12;
      }
      //Pause when door is open
      Serial.println(analogRead(doorSwitch));
      if (analogRead(doorSwitch) == 0) {
        Serial.println("Time-based flip off");
        toRun = seconds;
        off();
        openDoors();
        state = 6;
      }
      break;
      
    //Pauses when door is open (Emission)
    case 5:
      Serial.println(analogRead(doorSwitch));
      if (analogRead(doorSwitch) > low_threshold && analogRead(doorSwitch) < high_threshold) {
        Serial.println("Resuming due to closed (Em)");
        state = 3;
        delay(3000);
        rad_last_checked = millis();
        start = millis();
        on();
      } else if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        state = 0;
        off();
      }
      break;
      
    //Paused when door is open (Time)
    case 6:
      Serial.println(analogRead(doorSwitch));
      //Resume when door is closed
      if (analogRead(doorSwitch) > low_threshold && analogRead(doorSwitch) < high_threshold) {
        Serial.println("Resuming due to closed (Time)");
        state = 4;
        delay(3000);
        rad_last_checked = millis();
        start = millis();
        on();
      } else if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        state = 0;
        off();
      }
      break;
      
    //Paused manually (Emission)
    case 7:
      //When button is pressed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Resume
        state = 3;
        start = millis() - seconds * 1000;
        rad_last_checked = millis();
        on();
      }
      break;
      
    //Paused manually (Time)
    case 8:
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Resume
        state = 4;
        start = millis();
        rad_last_checked = millis();
        on();
      }
      break;

    //Warm-up sequence
    case 9:
      //When the button is pressed
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Pause or cancel
        if (counter % 2 == 0) {
          //State shift and save seconds left
          off();
          state = 10;
          toRun = seconds;
        } else {
          off();
          state = 0;
        }
      }
      //When done, beep and finish
      if (seconds <= 0) {
        off();
        state = 0;
      }
      //Pause when door is open
      Serial.println(analogRead(doorSwitch));
      if (analogRead(doorSwitch) == 0) {
        Serial.println("Warming up flip off");
        toRun = seconds;
        off();
        state = 11;
      }
      break;

    //Paused manually (Warming up)
    case 10:
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        //Resume
        state = 9;
        start = millis();
        rad_last_checked = millis();
        on();
      }
      break;

    //Paused when door is open (Warming up)
    case 11:
      Serial.println(analogRead(doorSwitch));
      //Resume when door is closed
      if (analogRead(doorSwitch) > low_threshold && analogRead(doorSwitch) < high_threshold) {
        Serial.println("Resuming due to closed (Warming up)");
        state = 9;
        delay(3000);
        rad_last_checked = millis();
        start = millis();
        on();
      } else if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        state = 12;
        off();
      }
      break;

    case 12:
      if (digitalRead(encoderB) == LOW && enableButton) {
        enableButton = false;
        state = 0;
      }
  }
  //Re-enable button if needed
  if (digitalRead(encoderB) == HIGH) {
    enableButton = true;
    //delay(10);
  }
}

void doLCD() {
  //If a change is registered, print the base for that state.
  if (state != prevState) {
    lcd.clear();
    counter = 0;
    for (int i = 0; i < 4; i++) {
      lcd.setCursor(0,i);
      lcd.print(states[state][i]);
    }
    prevState = state;
  } else { prevState = state; }

  //Route LCD to proper function
  switch (state) {
    
    //Base state only needs to worry about switching between two menu options
    case 0:
      //Set > character to right place
      if (counter % 3 == 0) {
        lcd.setCursor(0,0);
        lcd.print(">");
        lcd.setCursor(0,1);
        lcd.print(" ");
        lcd.setCursor(0,2);
        lcd.print(" ");
        lcd.setCursor(0,3);
        lcd.print(" ");
      } else if (counter % 3 == 1) {
        lcd.setCursor(0,0);
        lcd.print(" ");
        lcd.setCursor(0,1);
        lcd.print(">");
        lcd.setCursor(0,2);
        lcd.print(" ");
        lcd.setCursor(0,3);
        lcd.print(" ");
      } else if (counter % 3 == 2) {
        lcd.setCursor(0,0);
        lcd.print(" ");
        lcd.setCursor(0,1);
        lcd.print(" ");
        lcd.setCursor(0,2);
        lcd.print(">");
        lcd.setCursor(0,3);
        lcd.print(" ");
      }/* else if (counter % 4 == 3) {
        lcd.setCursor(0,0);
        lcd.print(" ");
        lcd.setCursor(0,1);
        lcd.print(" ");
        lcd.setCursor(0,2);
        lcd.print(" ");
        lcd.setCursor(0,3);
        lcd.print(">");
      }*/
      break;
      
    //Em-based config. Allow to input ems.
    case 1:
      //Move to proper place to print
      lcd.setCursor(8,1);
      
      //Min value is 0
      if (toRun + counter * 5 <= 0) {
        counter = - (toRun / 5);
        toRun = - (counter * 5);
      }
      
      //Print a formatted string
      strcpy(toPrint, "");
      sprintf(toPrint, "%04d", toRun + counter * 5);
      lcd.print(toPrint);
      break;
      
    //Time-based config
    case 2:
      //Move to proper place
      lcd.setCursor(8,2);
      
      //Min value is 0
      if (toRun + counter * 5 <= 0) {
        counter = - (toRun / 5);
        toRun = - (counter * 5);
      }
      
      //Print the formatted string
      strcpy(toPrint, "");
      sprintf(toPrint, "%02d:%02d", (toRun + counter * 5) / 60, (toRun + counter * 5) % 60);
      lcd.print(toPrint);
      break;
      
    //Em-based running
    case 3:
      //Clear string
      strcpy(toPrint, "");

      //Update and print how long it's been running
      seconds = (millis() - start) / 1000;
      sprintf(toPrint, "%02d:%02d", seconds / 60, seconds % 60);
      lcd.setCursor(12, 0);
      lcd.print(toPrint);

      //Update and print rads left
      rads -= radRead();
      sprintf(toPrint, "%04d", (int)rads);
      lcd.setCursor(11, 1);
      lcd.print(toPrint);

      //Set cursor to right position
      if (counter % 2 == 0) {
        lcd.setCursor(0,2);
        lcd.print(">");
        lcd.setCursor(0,3);
        lcd.print(" ");
      } else {
        lcd.setCursor(0,3);
        lcd.print(">");
        lcd.setCursor(0,2);
        lcd.print(" ");
      }
      
      break;
      
    //Time-based running
    case 4:
      //Clear string
      strcpy(toPrint, "");

      //Update and print seconds left
      seconds = toRun - ((millis() - start) / 1000);
      sprintf(toPrint, "%02d:%02d", seconds / 60, seconds % 60);
      lcd.setCursor(12, 0);
      lcd.print(toPrint);

      //Update and print rads done
      rads += radRead();
      sprintf(toPrint, "%04d", (int)rads);
      lcd.setCursor(11, 1);
      //lcd.print(toPrint);

      //Place > character properly
      if (counter % 2 == 0) {
        lcd.setCursor(0,2);
        lcd.print(">");
        lcd.setCursor(0,3);
        lcd.print(" ");
      } else {
        lcd.setCursor(0,3);
        lcd.print(">");
        lcd.setCursor(0,2);
        lcd.print(" ");
      }

    case 9:
      //Clear string
      strcpy(toPrint, "");

      //Update and print seconds left
      seconds = toRun - ((millis() - start) / 1000);
      sprintf(toPrint, "%02d:%02d", seconds / 60, seconds % 60);
      lcd.setCursor(12, 0);
      lcd.print(toPrint);

      //Place > character properly
      if (counter % 2 == 0) {
        lcd.setCursor(0,2);
        lcd.print(">");
        lcd.setCursor(0,3);
        lcd.print(" ");
      } else {
        lcd.setCursor(0,3);
        lcd.print(">");
        lcd.setCursor(0,2);
        lcd.print(" ");
      }
      
      break;
    //Other states don't need live updating in the same way
  }
}

//TODO: Implement real reading here
float radRead() {
  float toReturn = .005 * (millis() - rad_last_checked);
  rad_last_checked = millis();
  return toReturn;
}

void on() {
  Serial.println("Turning on");
  digitalWrite(doorOut, LOW);
  delay(500);
  digitalWrite(lights, HIGH);
}

void off() {
  Serial.println("Turning off");
  digitalWrite(lights, LOW);
}

void openDoors() {
  Serial.println("Opening doors function");
  digitalWrite(doorOut, HIGH);
  noInterrupts();
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4  = 0;
  OCR4A = 65535;
  TCCR4B |= (1 << WGM12);
  TCCR4B |= (1 << CS12) | (1 << CS10);
  TIMSK4 |= (1 << OCIE4A);
  interrupts();
}

//Close door
ISR(TIMER4_COMPA_vect){
  Serial.println("Closing doors");
  digitalWrite(doorOut, LOW);
  noInterrupts();
  TCCR4A = 0;
  TCCR4B = 0;
  TCNT4  = 0;
  OCR4A = 65535;
  TCCR4B |= (1 << WGM12);
  TIMSK4 |= (1 << OCIE4A);
  interrupts();
}

//Interrupt function
void count() {
  //Debounce feature
  static unsigned long last_interrupt = 0;
  unsigned long curr_interrupt = millis();
  if (curr_interrupt - last_interrupt > 125) {
    //Properly increment
    if (digitalRead(encoder1) == digitalRead(encoder2)) {
      counter--;
    } else {
      counter++;
    }
  }
  last_interrupt = curr_interrupt;
}
