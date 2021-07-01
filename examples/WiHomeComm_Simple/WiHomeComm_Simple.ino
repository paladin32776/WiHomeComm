#include <Arduino.h>
#include "WiHomeComm.h"
#include "NoBounceButtons.h"
#include "SignalLED.h"

WiHomeComm* whc;
NoBounceButtons nbb;
SignalLED led(15, SLED_OFF, false);
char button0;

void setup()
{
  Serial.begin(115200);
  delay(500);
  // Serial.println();
  // Serial.print("*** WIHOME STARTING ***");
  // Serial.println();
  whc = new WiHomeComm(true);
  whc->set_status_led(&led);
  button0 = nbb.create(0);
	whc->set_button(&nbb, button0, NBB_LONG_CLICK);
}

void loop()
{
  nbb.check();
  led.check();
  whc->check();
  delay(10);
}
