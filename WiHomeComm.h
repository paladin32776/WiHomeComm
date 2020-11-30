// WiHome Communications Class
// Author: Gernot Fattinger (2019-2020)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <pgmspace.h>
#include "Arduino.h"
#include "EnoughTimePassed.h"
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "Json_NVM.h"
#include "ConfigFileJSON.h"
#include "SignalLED.h"
#include "NoBounceButtons.h"
#include "RGBstrip.h"

#ifndef WIHOMECOMM_H
#define WIHOMECOMM_H

//#define WIHOMECOMM_RECONNECT_INTERVAL 10000
#define WIHOMECOMM_WAITFOR_CONNECT_INTERVAL 250
#define WIHOMECOMM_MAX_CONNECT_COUNT 0
#define WIHOMECOMM_FINDHUB_INTERVAL 60000 //ms

#define WIHOMECOMM_UNKNOWN 0
#define WIHOMECOMM_CONNECTED 1
#define WIHOMECOMM_NOHUB 2
#define WIHOMECOMM_DISCONNECTED 3
#define WIHOMECOMM_SOFTAP 4

const char html_config_form1[] = {"<!DOCTYPE html><html><body><h2 style='font-family:verdana;'>WiHome Setup</h2><form action='/save_and_restart.php' style='font-family:verdana;'>  SSID:<br>  <input type='text' name='ssid' value='"};
const char html_config_form2[] = {"'>  <br>  Password:<br>  <input type='text' name='password' value='"};
const char html_config_form3[] = {"'>  <br>  Client Name:<br>  <input type='text' name='client' value='"};
const char html_config_form4[] = {"'>  <br> Homekit Reset:<br> <input type='checkbox' name='homekit_reset' value=1> <br>  <input type='submit' value='Save and Connect'></form> </body></html>"};

class WiHomeComm
{
  private:
    // UserData variables and configuration
    char ssid[32];
    char password[32];
    char client[32];
    bool homekit_reset = false;
    // ConfigFileJSON:
    ConfigFileJSON* config;
    // SoftAP configuration
    char ssid_softAP[32];
    ESP8266WebServer* webserver = NULL;
    DNSServer* dnsServer = NULL;
    const byte DNS_PORT = 53;
    // WiHome UDP communication configuration
    WiFiUDP Udp;
    unsigned int localUdpPort = 24559; //24557;
    char incomingPacket[255];
    IPAddress hubip;
    EnoughTimePassed* etp_findhub = NULL;
    bool hub_discovered = false;
    bool wihome_protocol = true;
    // Settings for WiFi persistence
    EnoughTimePassed* etp_Wifi = NULL;
    bool needMDNS = true;
    enum WIHOME_STATES
    {
      WH_INIT,
      WH_STOP_SOFTAP,
      WH_STOP_MDNS,
      WH_STOP_UDP,
      WH_STOP_STA,
      WH_START_STA,
      WH_WAITFOR_STA,
      WH_START_MDNS,
      WH_START_OTA,
      WH_START_UDP,
      WH_CONNECTED,
      WH_ERROR = 255,
    };
    enum WIHOME_STATES connect_state = WH_INIT;

    // Status led:
    SignalLED* status_led;
    int handle_status_led = 0;
    unsigned int* led_status;
    SignalLED* relay;
    RGBstrip* rgbstrip;
    // Storage for config button info:
    NoBounceButtons* nbb;
    unsigned char button;
    unsigned char softAP_trigger;
    bool handle_button = false;
    // Methods:
    bool ConnectStation();
    void ConnectSoftAP();
    bool LoadUserData();
    void SaveUserData();
    void CreateConfigWebServer(int port);
    void DestroyConfigWebServer();
    void handleRoot();
    void handleNotFound();
    void handleSaveAndRestart();
    void handleClient();
    void findhub();
    void serve_packet(DynamicJsonDocument& doc);
    void init(bool _wihome_protocol);
    void check_status_led();
    void check_button();
    // Template functions to assemble JSON object from variable number of input parameters:
    template<typename Tparameter, typename Tvalue>
    void assembleJSON(DynamicJsonDocument& doc, Tparameter parameter, Tvalue value)
    {
      doc[parameter]=value;
    }
    template<typename Tparameter, typename Tvalue, typename... Args>
    void assembleJSON(DynamicJsonDocument& doc, Tparameter parameter, Tvalue value, Args... args)
    {
      doc[parameter]=value;
      assembleJSON(doc, args...);
    }

  public:
    WiHomeComm();
    WiHomeComm(bool _wihome_protocol);  // Optional ommission of wihome UDP communication funcitonality
    void set_status_led(SignalLED* _status_led);
    void set_status_led(SignalLED* _status_led, unsigned int* _led_status);
    void set_status_led(SignalLED* _status_led, SignalLED* _relay);
    void set_status_led(SignalLED* _status_led, RGBstrip* _rgbstrip);
    void set_button(NoBounceButtons* _nbb, unsigned char _button);
    void set_button(NoBounceButtons* _nbb, unsigned char _button, unsigned char _softAP_trigger);
    byte status(); // get connection status
    void check();
    void check(DynamicJsonDocument& doc);
    void send(DynamicJsonDocument& doc);
    bool softAPmode = false;
    bool is_homekit_reset();
    // Template functions to write s variable number of input parameters as JSON object:
    template<typename... Args>
    bool sendJSON(Args... args)
    {
        DynamicJsonDocument doc(1024);
        assembleJSON(doc, args...);
        send(doc);
    }
};

#endif // WIHOMECOMM_H
