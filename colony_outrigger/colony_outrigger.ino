// rf69 demo tx rx.pde
// -*- mode: C++ -*-
// Example sketch showing how to create a simple addressed, reliable messaging client
// with the RH_RF69 class. RH_RF69 class does not provide for addressing or
// reliability, so you should only use RH_RF69  if you do not need the higher
// level messaging abilities.
// It is designed to work with the other example rf69_server.
// Demonstrates the use of AES encryption, setting the frequency and modem
// configuration

#include <SPI.h>
#include <RH_RF69.h>
#include <RHReliableDatagram.h>
#include <HX711.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

/************ Radio Setup ***************/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 915.0

// Where to send packets to!
#define DEST_ADDRESS   1
// change addresses for each client board, any number :)
#define MY_ADDRESS     101

#if defined(ADAFRUIT_FEATHER_M0) // Feather M0 w/Radio
#define RFM69_CS      8
#define RFM69_INT     3
#define RFM69_RST     4
#define LED           13
#endif


/************** load cell setup *************/

#define LOADCELL_DOUT_PIN  10
#define LOADCELL_SCK_PIN  9

/************** neopixel setup *************/

#define NEOPIN  11

/********************************************/


/************* other setup **************/

#define VBATPIN A7 // battery monitor pin


// Singleton instance of the radio driver
RH_RF69 rf69(RFM69_CS, RFM69_INT);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, MY_ADDRESS);

float measuredvbat;

const int num_pixels = 12;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(num_pixels, NEOPIN, NEO_RGBW + NEO_KHZ800);



const float pi = 3.14159265359;
float theta[num_pixels][4];
float speeds[num_pixels][4];
float accurate[num_pixels][4];
int rounded[num_pixels][4];
float masterfade[num_pixels];

float fade_on_speed = 0.005, fade_off_speed = 0.05;
float fade_on_total_time = 1000, fade_off_total_time = 2000; // time from all off to all on

float randomdetail = 100000;
float minspeed = 0.00001;
float maxspeed = 0.1;
float maxbrightness[4] = {80, 255, 30, 70};  // GREEN FIRST - grbw
/* float maxbrightness[4] = {70,200,0,255};  // GREEN FIRST - grbw */

unsigned long starttime = 0;
int speedsettimer = 30; // in seconds

unsigned long fade_start = 0, fade_progress = 0;
boolean
fading = false,
  fade_direction = true,
  debug = true,
  triggered = false,
  has_manners = true;



int16_t packetnum = 0;  // packet counter, we increment per xmission

HX711 scale;
float calibration_factor = 225820; // 225820 for the 5kg sparkfun load cell
int thresh_min = 170, thresh_max = 200;
int weight;



String command;

void setup()
{
  if (debug) Serial.begin(115200);
  // if (debug) while (!Serial) { delay(1); } // wait until serial console is open, remove if not tethered to computer

  measuredvbat = analogRead(VBATPIN);
  measuredvbat *= 2;    // we divided by 2, so multiply back
  measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
  measuredvbat /= 1024; // convert to voltage
  Serial.print("Battery voltage: " ); Serial.println(measuredvbat);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.tare();	//Assuming there is no weight on the scale at start up, reset the scale to 0


  pinMode(LED, OUTPUT);
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);

  if (debug) Serial.println("Feather Addressed RFM69 TX Test!");
  if (debug) Serial.println();

  // manual reset
  digitalWrite(RFM69_RST, HIGH);
  delay(10);
  digitalWrite(RFM69_RST, LOW);
  delay(10);

  if (!rf69_manager.init()) {
    if (debug) Serial.println("RFM69 radio init failed");
    while (1);
  }
  if (debug) Serial.println("RFM69 radio init OK!");
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for low power module)
  // No encryption
  if (!rf69.setFrequency(RF69_FREQ)) {
    if (debug) Serial.println("setFrequency failed");
  }

  // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
  // ishighpowermodule flag set like this:
  rf69.setTxPower(20, true);  // range from 14-20 for power, 2nd arg must be true for 69HCW

  // The encryption key has to be the same as the one in the server
  uint8_t key[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
                  };
  rf69.setEncryptionKey(key);

  pinMode(LED, OUTPUT);

  if (debug) Serial.print("RFM69 radio @");  if (debug) Serial.print((int)RF69_FREQ);  if (debug) Serial.println(" MHz");


  changespeeds();
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(10);
}


// Dont put this on the stack:
uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
uint8_t data[] = "  OK";

void loop() {
  if (has_manners) sayhello();

  if (millis() % 250 < 50) {

    weight = int(scale.get_units() * 1000);
    if (debug) Serial.print("reading: "); if (debug) Serial.println(weight);


    if (weight > thresh_min && weight < thresh_max && triggered == false) {
      triggered = true;
      command = "complete";
      char radiopacket[30] = "complete";
      /* strcat(radiopacket, sensor_reading); */
      /* itoa(weight, radiopacket + 9, 10); */
      //  itoa(sensor_reading, radiopacket+13, 10);

      transmit(radiopacket);
    }

    if (weight < thresh_min && triggered == true) {
      triggered = false;
      command = "uncomplete";

      char radiopacket[30] = "uncomplete";
      transmit(radiopacket);
    }

  }


  if(fading) {
      fade_progress = millis() - fade_start;

      float s = 0;
      for (int i = 0; i < num_pixels; i++) {
        s += masterfade[i];
      }

      if (fade_direction && s == num_pixels) fading = false; // 1's accross the board, fading up
      if (!fade_direction && s == 0) fading = false; // 0's, fading down

      else { // do the fade!
        for (int i = 0; i < num_pixels; i++) {

          if (fade_direction) {
            /* float where_am_i = float(i) / float(num_pixels); // linear */
            float where_am_i = pow(float(i) / float(num_pixels), 2.5); // exponential

            if (where_am_i < fade_progress / fade_on_total_time) {
              if (masterfade[i] < 1-fade_on_speed) {
                masterfade[i] += fade_on_speed;
              }
              else masterfade[i] = 1;
            }
          }
          else if (!fade_direction) {
            float where_am_i = float(num_pixels - i) / float(num_pixels); // linear
            if (where_am_i < fade_progress / fade_off_total_time) {
              if (masterfade[i] > fade_off_speed) {
                masterfade[i] -= fade_off_speed;
              }
              else masterfade[i] = 0;
            }
          }
      }
      /* if (debug) Serial.println(fade_progress / fade_on_total_time); */
    }
  }

  if (millis()-starttime > speedsettimer*1000) {
    changespeeds();
  }

  for (int i = 0; i < num_pixels; i++) {
    for (int v = 0; v < 4; v++) {
      if (theta[i][v] < pi*2) theta[i][v] += speeds[i][v];
      else theta[i][v] = 0;
      accurate[i][v] = sinmap(theta[i][v], v) * masterfade[i];
      rounded[i][v] = (int) round(accurate[i][v]);
    }
    strip.setPixelColor(i, strip.Color(rounded[i][0], rounded[i][1], rounded[i][2], rounded[i][3]));
  }


  if (debug) {
    if (Serial.available() > 0)  {
      int incoming = Serial.read();
      if (debug) Serial.print("input: ");
      if (debug) Serial.print(incoming);
      if (debug) Serial.print("\t letter: ");
      if (debug) Serial.write(incoming);
      if (debug) Serial.println();

      switch(incoming) {
      case 114: // r
        command = "reset";
        break;
      case 99: // c
        command = "complete";
        break;
      case 117: // u
        command = "uncomplete";
        break;
      }
    }





  //     COMMANDS

    if (command == "complete") {
      if (debug) Serial.println("this outrigger completed");
      fading = true;
      fade_direction = true;
      fade_start = millis();
    }
    else if (command == "uncomplete") {
      if (debug) Serial.println("this outrigger was completed, now it's not");
      fading = true;
      fade_direction = false;
      fade_start = millis();
    }
    else if (command == "reset") {
      if (debug) Serial.println("resetting");
      for (int i = 0; i < num_pixels; i++) {
        masterfade[i] = 0;
      }
      fading = true;
      fade_start = millis();
    }
    command = "";

  }
  strip.show();
  if (debug) delay(10);
}

void transmit(char message[]) {
  if (debug) Serial.print("Sending "); if (debug) Serial.println(message);

    // Send a message to the DESTINATION!
    if (rf69_manager.sendtoWait((uint8_t *)message, strlen(message), DEST_ADDRESS)) {
      // Now wait for a reply from the server
      uint8_t len = sizeof(buf);
      uint8_t from;
      if (rf69_manager.recvfromAckTimeout(buf, &len, 2000, &from)) {
        buf[len] = 0; // zero out remaining string

        if (debug) Serial.print("Got reply from #"); if (debug) Serial.print(from);
        if (debug) Serial.print(" [RSSI :");
        if (debug) Serial.print(rf69.lastRssi());
        if (debug) Serial.print("] : ");
        if (debug) Serial.println((char*)buf);
        /* Blink(LED, 40, 3); //blink LED 3 times, 40ms between blinks */
      } else {
        if (debug) Serial.println("No reply, is anyone listening?");
      }
    } else {
      if (debug) Serial.println("Sending failed (no ack)");
    }
}

void Blink(byte PIN, byte DELAY_MS, byte loops) {
  for (byte i = 0; i < loops; i++)  {
    digitalWrite(PIN, HIGH);
    delay(DELAY_MS);
    digitalWrite(PIN, LOW);
    delay(DELAY_MS);
  }
}

void sayhello() {
  char radiopacket[30] = "hello";
  transmit(radiopacket);
  has_manners = false;
}



float sinmap(float theta, int channel) {
  return ((sin(theta) + 1) / 2)*maxbrightness[channel];
}

float setspeed(float seed) {
  float range = maxspeed - minspeed;
  return seed/randomdetail * range + minspeed;
}

void changespeeds() {
  if (debug) Serial.print("changing speeds");

  /* if (debug) Serial.print(starttime); */
  /* if (debug) Serial.print("\t"); */
  /* if (debug) Serial.print(millis()); */
  /* if (debug) Serial.print("\t"); */
  /* if (debug) Serial.print(millis() - starttime); */
  /* if (debug) Serial.println(); */
  for (int i = 0; i < num_pixels; i++) {
    for (int v = 0; v < 4; v++) {
      speeds[i][v] = setspeed(random(randomdetail));
      /* if (debug) Serial.print(speeds[i][v]); */
      /* if (debug) Serial.print("\t"); */
    }
  }
  if (debug) Serial.println();


  starttime = millis();
}
