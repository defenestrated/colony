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

/************ Radio Setup ***************/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 915.0

// Where to send packets to!
#define DEST_ADDRESS   1
// change addresses for each client board, any number :)
#define MY_ADDRESS     101


/************** load cell setup *************/

#define LOADCELL_DOUT_PIN  10
#define LOADCELL_SCK_PIN  9

/********************************************/


#if defined (__AVR_ATmega32U4__) // Feather 32u4 w/Radio
#define RFM69_CS      8
#define RFM69_INT     7
#define RFM69_RST     4
#define LED           13
#endif

#if defined(ADAFRUIT_FEATHER_M0) // Feather M0 w/Radio
#define RFM69_CS      8
#define RFM69_INT     3
#define RFM69_RST     4
#define LED           13
#endif

#if defined (__AVR_ATmega328P__)  // Feather 328P w/wing
#define RFM69_INT     3  //
#define RFM69_CS      4  //
#define RFM69_RST     2  // "A"
#define LED           13
#endif

#if defined(ESP8266)    // ESP8266 feather w/wing
#define RFM69_CS      2    // "E"
#define RFM69_IRQ     15   // "B"
#define RFM69_RST     16   // "D"
#define LED           0
#endif

#if defined(ESP32)    // ESP32 feather w/wing
#define RFM69_RST     13   // same as LED
#define RFM69_CS      33   // "B"
#define RFM69_INT     27   // "A"
#define LED           13
#endif

/* Teensy 3.x w/wing
  #define RFM69_RST     9   // "A"
  #define RFM69_CS      10   // "B"
  #define RFM69_IRQ     4    // "C"
  #define RFM69_IRQN    digitalPinToInterrupt(RFM69_IRQ )
*/

/* WICED Feather w/wing
  #define RFM69_RST     PA4     // "A"
  #define RFM69_CS      PB4     // "B"
  #define RFM69_IRQ     PA15    // "C"
  #define RFM69_IRQN    RFM69_IRQ
*/

// Singleton instance of the radio driver
RH_RF69 rf69(RFM69_CS, RFM69_INT);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(rf69, MY_ADDRESS);


int16_t packetnum = 0;  // packet counter, we increment per xmission

HX711 scale;
float calibration_factor = 225820; //225820 for the 5kg sparkfun load cell
int thresh_min = 170, thresh_max = 200;
int weight;

boolean triggered = false, has_manners = true;

boolean debug = false;


void setup()
{
  if (debug) Serial.begin(115200);
  if (debug) while (!Serial) { delay(1); } // wait until serial console is open, remove if not tethered to computer


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
}


// Dont put this on the stack:
uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
uint8_t data[] = "  OK";

void loop() {
  if (has_manners) sayhello();

  delay(250);
  /* delay(1000);  // Wait 1 second between transmits, could also 'sleep' here! */

  weight = int(scale.get_units() * 1000);
  if (debug) Serial.print("reading: "); if (debug) Serial.println(weight);


  if (weight > thresh_min && weight < thresh_max && triggered == false) {
    triggered = true;

    char radiopacket[30] = "go";
     /* strcat(radiopacket, sensor_reading); */
    /* itoa(weight, radiopacket + 9, 10); */
    //  itoa(sensor_reading, radiopacket+13, 10);

    transmit(radiopacket);
  }

  if (weight < thresh_min && triggered == true) {
    triggered = false;

    char radiopacket[30] = "reset";
    transmit(radiopacket);
  }
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
        Blink(LED, 40, 3); //blink LED 3 times, 40ms between blinks
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
