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

#ifndef WIHOMECOMM_H
#define WIHOMECOMM_H

#define WIHOMECOMM_MAX_CONNECT_COUNT 2
#define WIHOMECOMM_FINDHUB_INTERVAL 60000 //ms

const char html_config_form1[] = {"<!DOCTYPE html><html><body><h2 style='font-family:verdana;'>WiHome Configuration</h2><form action='/save_and_restart.php' style='font-family:verdana;'>  SSID:<br>  <input type='text' name='ssid' value='"};
const char html_config_form2[] = {"'>  <br>  Password:<br>  <input type='text' name='password' value='"};
const char html_config_form3[] = {"'>  <br>  MQTT Broker:<br>  <input type='text' name='mqtt_broker' value='"};
const char html_config_form4[] = {"'>  <br>  Client Name:<br>  <input type='text' name='client' value='"};
const char html_config_form5[] = {"'>  <br><br>  <input type='submit' value='Save and Connect'></form> </body></html>"};

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
    WiFiUDP Udp,Udp_discovery;
    unsigned int localUdpPort = 24557;
    unsigned int discoveryUdpPort = 24558;
    char incomingPacket[255];
    IPAddress hubip;
    EnoughTimePassed* etp_findhub;
    // Settings for WiFi persistence
    EnoughTimePassed* etp_Wifi;
    bool needMDNS = true;
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
    void serve_findclient();
    JsonObject& serve_packet(DynamicJsonBuffer* jsonBuffer);
  public:
    WiHomeComm();  // setup object with desired intervall
    JsonObject& check(DynamicJsonBuffer* jsonBuffer);
    void send(JsonObject& root);
    bool softAPmode = false;
};

#endif // WIHOMECOMM_H
