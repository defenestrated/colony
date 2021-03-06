#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#include <SPI.h>
#include <RH_RF69.h>
#include <RHReliableDatagram.h>

#define NEOPIN 7 // neopixel data pin

/************ Radio Setup ***************/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 915.0

// who am i? (server address)
#define MY_ADDRESS    1

#define RFM69_INT     3  //  -> G0
#define RFM69_CS      4  //  -> CS
#define RFM69_RST     2  //  -> RST
#define LED           13

// Singleton instance of the radio driver
RH_RF69 rf69(RFM69_CS, RFM69_INT);
// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, MY_ADDRESS);

/****************************************/

/************* other setup **************/

const int num_pixels = 25;
const float pi = 3.14159265359;
float theta[num_pixels][4];
float speeds[num_pixels][4];
float accurate[num_pixels][4];
int rounded[num_pixels][4];
float masterfade[num_pixels];

float fade_on_speed = 0.005, fade_off_speed = 0.05;
float fade_on_total_time = 2000, fade_off_total_time = 3000; // time from all off to all on

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
  debug = true;


// outrigger addresses: 106, 105, 104, 103, 102, 101
boolean outriggers[6]; // all set to false initially, individually set to true once they say hello
int outrigger_count = 0;

String command;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(num_pixels, NEOPIN, NEO_RGBW + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

void setup()
{
  if (debug) Serial.begin(115200);
  // if (debug) while (!Serial) { delay(1); } // wait until serial console is open, remove if not tethered to computer


  if (debug) Serial.println("---------");
  if (debug) Serial.print("outriggers: ");
  if (debug) for (int o = 0; o < 6; o++) {
      Serial.print(outriggers[o]);
      Serial.print(" ");
  }
  if (debug) Serial.println();

  for (int i = 0; i < num_pixels; i++) { // for each LED
    if (debug) Serial.print("LED ");
    if (debug) Serial.print(i);
    if (debug) Serial.println();
    for (int v = 0; v < 4; v++) { // then for each color value
      float seed1 = random(randomdetail);
      float seed2 = random(randomdetail);
      theta[i][v] = (seed1/randomdetail)*pi*2;
      speeds[i][v] = setspeed(seed2);
      accurate[i][v] = sinmap(theta[i][v], v);
      rounded[i][v] = (int) round(accurate[i][v]);
      if (debug) Serial.print(theta[i][v]);
      if (debug) Serial.print("|");
      if (debug) Serial.print(speeds[i][v]);
      if (debug) Serial.print(" // ");
      if (debug) Serial.print(accurate[i][v]);
      if (debug) Serial.print("|");
      if (debug) Serial.print(rounded[i][v]);
      if (debug) Serial.println();
    }
  }
  if (debug) Serial.println();

  pinMode(LED, OUTPUT);
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);

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
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  rf69.setEncryptionKey(key);

  pinMode(LED, OUTPUT);

  if (debug) Serial.print("RFM69 radio @");  if (debug) Serial.print((int)RF69_FREQ);  if (debug) Serial.println(" MHz");


  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(10);
  /* fade_start = millis(); */
}


// Dont put this on the stack:
uint8_t data[] = "And hello back to you";
// Dont put this on the stack:
uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];

void loop() {
  if (rf69_manager.available())
  {
    // Wait for a message addressed to us from the client
    uint8_t len = sizeof(buf);
    uint8_t from;
    if (rf69_manager.recvfromAck(buf, &len, &from)) {
      buf[len] = 0; // zero out remaining string

      if (debug) {
        Serial.print("Got packet from #"); if (debug) Serial.print(from);
        Serial.print(" [RSSI :");
        Serial.print(rf69.lastRssi());
        Serial.print("] : ");
        Serial.println((char*)buf);
      }

      if (strcmp(buf, "hello") == 0) {
        outriggers[106-from] = true;

        if (debug) {
          Serial.print("hello outrigger ");
          Serial.print(from);
          Serial.print(" -- outriggers now:");
          for (int o = 0; o < 6; o++) {
            Serial.print(outriggers[o]);
            Serial.print(" ");
          }
        }
      }
      if (from == 101 && strcmp(buf, "complete") == 0) {
        command = "go";
      }
      if (from == 101 && strcmp(buf, "uncomplete") == 0) {
        command = "stop";
      }




      Blink(LED, 40, 3); //blink LED 3 times, 40ms between blinks

      // Send a reply back to the originator client
      if (!rf69_manager.sendtoWait(data, sizeof(data), from))
        if (debug) Serial.println("Sending failed (no ack)");
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
      case 115: // s
        command = "stop";
        break;
      case 103: // g
        command = "go";
        break;
      }
    }



    //     COMMANDS

    if (command == "go") {
      if (debug) Serial.println("going");
      fading = true;
      fade_direction = true;
      fade_start = millis();
    }
    else if (command == "stop") {
      if (debug) Serial.println("stopping");
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


void Blink(byte PIN, byte DELAY_MS, byte loops) {
  for (byte i=0; i<loops; i++)  {
    digitalWrite(PIN,HIGH);
    delay(DELAY_MS);
    digitalWrite(PIN,LOW);
    delay(DELAY_MS);
  }
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
