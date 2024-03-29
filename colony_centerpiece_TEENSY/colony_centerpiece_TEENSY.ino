/*  OctoWS2811 BasicTest.ino - Basic RGB LED Test
    http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
    Copyright (c) 2013 Paul Stoffregen, PJRC.COM, LLC

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

  Required Connections
  --------------------
    pin 2:  LED Strip #1    OctoWS2811 drives 8 LED Strips.
    pin 14: LED strip #2    All 8 are the same length.
    pin 7:  LED strip #3
    pin 8:  LED strip #4    A 100 ohm resistor should used
    pin 6:  LED strip #5    between each Teensy pin and the
    pin 20: LED strip #6    wire to the LED strip, to minimize
    pin 21: LED strip #7    high frequency ringining & noise.
    pin 5:  LED strip #8
    pin 15 & 16 - Connect together, but do not use
    pin 4 - Do not use
    pin 3 - Do not use as PWM.  Normal use is ok.

  This test is useful for checking if your LED strips work, and which
  color config (WS2811_RGB, WS2811_GRB, etc) they require.
*/

#include <OctoWS2811.h>
#include <SPI.h>
#include <RH_RF69.h>
#include <RHReliableDatagram.h>
#include <RHHardwareSPI1.h>

const int px_per_channel = 72;

DMAMEM int displayMemory[px_per_channel*8]; // 8 ints is 32 bytes
int drawingMemory[px_per_channel*8]; // RGBW needs 32 bytes, RGB needs only 24

const int config = WS2811_GRBW | WS2811_800kHz;

OctoWS2811 leds(px_per_channel, displayMemory, drawingMemory, config);



/************ Radio Setup ***************/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 915.0

// who am i? (server address)
#define MY_ADDRESS    1

#define RFM69_INT     37  //  -> G0
#define RFM69_CS      38  //  -> CS
#define RFM69_RST     36  //  -> RST
#define LED           13

// Singleton instance of the radio driver USING SPI1
RH_RF69 rf69(RFM69_CS, RFM69_INT, hardware_spi1);
// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, MY_ADDRESS);

/****************************************/

/************* other setup **************/

const int channels = 6;
const float pi = 3.14159265359;
float theta[channels][px_per_channel][4];
float speeds[channels][px_per_channel][4];
float accurate[channels][px_per_channel][4];
int rounded[channels][px_per_channel][4];
float masterfade[channels][px_per_channel];

float fade_on_speed = 0.005, fade_off_speed = 0.05;
float fade_on_total_time = 6000, fade_off_total_time = 3000; // time from all off to all on

float randomdetail = 100000;
float minspeed = 0.00001;
float maxspeed = 0.05;
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



void setup() {
  if (debug) Serial.begin(115200);
  // if (debug) while (!Serial) { delay(1); } // wait until serial console is open, remove if not tethered to computer


  if (debug) Serial.println("---------");
  if (debug) Serial.print("outriggers: ");
  if (debug) for (int o = 0; o < 6; o++) {
      Serial.print(outriggers[o]);
      Serial.print(" ");
  }
  if (debug) Serial.println();


  SPI1.setCS(38);
  SPI1.setMISO(39);
  SPI1.begin();


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

  if (debug) Serial.print("RFM69 radio @");  if (debug) Serial.print((int)RF69_FREQ);  if (debug) Serial.println(" MHz");


  leds.begin();
  leds.show();
  changespeeds();
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
      for (int ch = 0; ch < channels; ch++) {
        for (int px = 0; px < px_per_channel; px++) {
          s += masterfade[ch][px];
        }
      }

      if (fade_direction && s == channels * px_per_channel) fading = false; // 1's accross the board, fading up
      if (!fade_direction && s == 0) fading = false; // 0's, fading down

      else { // do the fade!

        for (int ch = 0; ch < channels; ch++) {
          for (int px = 0; px < px_per_channel; px++) {

            if (fade_direction) {
              /* float where_am_i = float(i) / float(num_pixels); // linear */
              float where_am_i = pow(float(px) / float(px_per_channel), 0.5); // exponential

              if (where_am_i < fade_progress / fade_on_total_time) {
                if (masterfade[ch][px] < 1-fade_on_speed) {
                  masterfade[ch][px] += fade_on_speed;
                }
                else masterfade[ch][px] = 1;
              }
            }
            else if (!fade_direction) {
              float where_am_i = pow(1- (float(px) / float(px_per_channel)), 2.5); // exponential
              if (where_am_i < fade_progress / fade_off_total_time) {
                if (masterfade[ch][px] > fade_off_speed) {
                  masterfade[ch][px] -= fade_off_speed;
                }
                else masterfade[ch][px] = 0;
              }
            }

          }
        }

      /* if (debug) Serial.println(fade_progress / fade_on_total_time); */
    }
  }

  if (millis()-starttime > speedsettimer*1000) {
    changespeeds();
  }

  for (int ch = 0; ch < channels; ch++) {
    for (int px = 0; px < px_per_channel; px++) {
      for (int v = 0; v < 4; v++) {
        if (theta[ch][px][v] < pi*2) theta[ch][px][v] += speeds[ch][px][v];
        else theta[ch][px][v] = 0;
        accurate[ch][px][v] = sinmap(theta[ch][px][v], v) * masterfade[ch][px];
        rounded[ch][px][v] = (int) round(accurate[ch][px][v]);
      }
      leds.setPixel(ch * px_per_channel + px, rounded[ch][px][0], rounded[ch][px][1], rounded[ch][px][2], rounded[ch][px][3]);
    }
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
      for (int ch = 0; ch < channels; ch++) {
        for (int px = 0; px < px_per_channel; px++) {
          masterfade[ch][px] = 0;
        }
      }
      fading = true;
      fade_start = millis();
    }
    command = "";

  }
  leds.show();
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

  for (int ch = 0; ch < channels; ch++) {
    for (int px = 0; px < px_per_channel; px++) {
      for (int v = 0; v < 4; v++) {
        speeds[ch][px][v] = setspeed(random(randomdetail));
        /* if (debug) Serial.print(speeds[i][v]); */
        /* if (debug) Serial.print("\t"); */
      }
    }
  }
  if (debug) Serial.println();


  starttime = millis();
}
