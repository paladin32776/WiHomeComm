// WiHome Communications Class
// Author: Gernot Fattinger (2019)
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
#include "SignalLED.h"

#ifndef WIHOMECOMM_H
#define WIHOMECOMM_H

#define WIHOMECOMM_RECONNECT_INTERVAL 10000
#define WIHOMECOMM_MAX_CONNECT_COUNT 0
#define WIHOMECOMM_FINDHUB_INTERVAL 60000 //ms

const char html_config_form1[] = {"<!DOCTYPE html><html><body><h2 style='font-family:verdana;'>WiHome Setup</h2><form action='/save_and_restart.php' style='font-family:verdana;'>  SSID:<br>  <input type='text' name='ssid' value='"};
const char html_config_form2[] = {"'>  <br>  Password:<br>  <input type='text' name='password' value='"};
const char html_config_form3[] = {"'>  <br>  Client Name:<br>  <input type='text' name='client' value='"};
const char html_config_form4[] = {"'>  <br><br>  <input type='submit' value='Save and Connect'></form> </body></html>"};

class WiHomeComm
{
  private:
    // UserData variables and configuration
    unsigned long int NVM_Offset_UserData = 0;
    byte valid_ud_id = 188;
    byte ud_id;
    char ssid[32];
    char password[32];
    char client[32];
    byte connect_count = 0;
    // SoftAP configuration
    char ssid_softAP[32];
    ESP8266WebServer* webserver;
    DNSServer* dnsServer;
    const byte DNS_PORT = 53;
    // WiHome UDP communication configuration
    WiFiUDP Udp;
    unsigned int localUdpPort = 24557;
    char incomingPacket[255];
    IPAddress hubip;
    EnoughTimePassed* etp_findhub;
    bool hub_discovered = false;
    bool wihome_protocol = true;
    // Settings for WiFi persistence
    EnoughTimePassed* etp_Wifi;
    bool needMDNS = true;
    // Status led:
    SignalLED* status_led;
    int handle_status_led = 0;
    unsigned int* led_status;
    SignalLED* relay;
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
    JsonObject& serve_packet(DynamicJsonBuffer* jsonBuffer);
    // Template functions to assemble JSON object from variable number of input parameters:
    template<typename Tparameter, typename Tvalue>
    void assembleJSON(JsonObject& root, Tparameter parameter, Tvalue value)
    {
      root[parameter]=value;
    }
    template<typename Tparameter, typename Tvalue, typename... Args>
    void assembleJSON(JsonObject& root, Tparameter parameter, Tvalue value, Args... args)
    {
      root[parameter]=value;
      assembleJSON(root, args...);
    }
    void init(bool _wihome_protocol);
    void check_status_led();
  public:
    WiHomeComm();
    WiHomeComm(bool _wihome_protocol);  // Optional ommission of wihome UDP communication funcitonality
    void set_status_led(SignalLED* _status_led);
    void set_status_led(SignalLED* _status_led, unsigned int* _led_status);
    void set_status_led(SignalLED* _status_led, SignalLED* _relay);
    byte status(); // get connection status
    void check();
    JsonObject& check(DynamicJsonBuffer* jsonBuffer);
    void send(JsonObject& root);
    bool softAPmode = false;
    // Template functions to send variable number of input parameters as JSON object:
    template<typename... Args>
    bool sendJSON(Args... args)
    {
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        assembleJSON(root, args...);
        //root.printTo(Serial);
        send(root);
    }
};

#endif // WIHOMECOMM_H
