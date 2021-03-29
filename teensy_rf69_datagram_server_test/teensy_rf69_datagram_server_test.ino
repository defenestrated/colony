// rf69_reliable_datagram_server.pde
// -*- mode: C++ -*-
// Example sketch showing how to create a simple addressed, reliable messaging server
// with the RHReliableDatagram class, using the RH_RF69 driver to control a RF69 radio.
// It is designed to work with the other example rf69_reliable_datagram_client
// Tested on Moteino with RFM69 http://lowpowerlab.com/moteino/
// Tested on miniWireless with RFM69 www.anarduino.com/miniwireless
// Tested on Teensy 3.1 with RF69 on PJRC breakout board

#include <RHReliableDatagram.h>
#include <RH_RF69.h>
#include <SPI.h>
#include <RHHardwareSPI1.h>

#define CLIENT_ADDRESS 101
#define SERVER_ADDRESS 1

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 915.0

// who am i? (server address)
#define MY_ADDRESS    1

#define RFM69_INT     37  //  -> G0
#define RFM69_CS      38  //  -> CS
#define RFM69_RST     36  //  -> RST
#define LED           13

boolean debug = true;
// Singleton instance of the radio driver
//RH_RF69 driver;
//RH_RF69 driver(15, 16); // For RF69 on PJRC breakout board with Teensy 3.1
//RH_RF69 rf69(4, 2); // For MoteinoMEGA https://lowpowerlab.com/shop/moteinomega
//RH_RF69 driver(8, 7); // Adafruit Feather 32u4
RH_RF69 driver(RFM69_CS, RFM69_INT, hardware_spi1);

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram rf69_manager(driver, SERVER_ADDRESS);

void setup() 
{

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

  
  Serial.begin(9600);
  while (!Serial) 
    ;

  Serial.println("setting up");
    
  if (!rf69_manager.init()) {
    if (debug) Serial.println("RFM69 radio init failed");
  }
  if (debug) Serial.println("RFM69 radio init OK!");
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for low power module)
  // No encryption
  if (!driver.setFrequency(RF69_FREQ)) {
    if (debug) Serial.println("setFrequency failed");
  }

  // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
  // ishighpowermodule flag set like this:
  driver.setTxPower(20, true);

  
  // The encryption key has to be the same as the one in the server
  uint8_t key[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  driver.setEncryptionKey(key);

  pinMode(LED, OUTPUT);

  if (debug) Serial.print("RFM69 radio @");  if (debug) Serial.print((int)RF69_FREQ);  if (debug) Serial.println(" MHz");


}

uint8_t data[] = "And hello back to you";
// Dont put this on the stack:
uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];

void loop()
{
  if (rf69_manager.available())
  {
    // Wait for a message addressed to us from the client
    uint8_t len = sizeof(buf);
    uint8_t from;
    if (rf69_manager.recvfromAck(buf, &len, &from))
    {
      Serial.print("got request from : 0x");
      Serial.print(from, HEX);
      Serial.print(": ");
      Serial.println((char*)buf);

      // Send a reply back to the originator client
      if (!rf69_manager.sendtoWait(data, sizeof(data), from))
        Serial.println("sendtoWait failed");
    }
  }
}
