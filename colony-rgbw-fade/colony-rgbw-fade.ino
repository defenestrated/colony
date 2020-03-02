#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 2


const int num_pixels = 20;
const float pi = 3.14159265359;
float theta[num_pixels][4];
float speeds[num_pixels][4];
float accurate[num_pixels][4];
int rounded[num_pixels][4];
float masterfade[num_pixels];

float fade_speed = 0.005;
float fade_total_time = 9000; // time from all off to all on

float randomdetail = 100000;
float minspeed = 0.00001;
float maxspeed = 0.1;
float maxbrightness[4] = {80, 255, 30, 70};  // GREEN FIRST - grbw

unsigned long starttime = 0;
int speedsettimer = 30; // in seconds

unsigned long fade_start = 0, fade_progress = 0;
boolean fading = true, debug = true;

String command;

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(num_pixels, PIN, NEO_RGBW + NEO_KHZ800);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.


void setup() {
  if (debug) Serial.begin(9600);
  delay(10);
  if (debug) Serial.println("---------");
  if (debug) Serial.println(pi);
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

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(10);
  fade_start = millis();
}

void loop() {

  if(fading) {

    fade_progress = millis() - fade_start;

    float s = 0;
    for (int i = 0; i < num_pixels; i++) {
      s += masterfade[i];
    }

    if (s == num_pixels) fading = false; // 1's accross the board

    else { // fade away
      for (int i = 0; i < num_pixels; i++) {
        float where_am_i = float(i) / float(num_pixels);
        if (where_am_i < fade_progress / fade_total_time) {
        if (masterfade[i] < 1-fade_speed) {
            masterfade[i] += fade_speed;
          }
        else masterfade[i] = 1;
        }
      }
    }
    /* if (debug) Serial.println(fade_progress / fade_total_time); */
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
      }
    }



    //     COMMANDS

    if (command == "reset") {
      command = "";
      if (debug) Serial.println("resetting");
      for (int i = 0; i < num_pixels; i++) {
        masterfade[i] = 0;
      }
      fading = true;
      fade_start = millis();
    }

  }
  strip.show();
  delay(10);
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
