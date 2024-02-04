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
#include <ArduinoJson.h>
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

const char html_config_form_begin[] = {"<!DOCTYPE html><html><body><h2 style='font-family:verdana;'>WiHome Setup</h2><form action='/save_and_restart.php' style='font-family:verdana;'>"};
const char html_config_form_end[] = {"<br>  <input type='submit' value='Save and Connect'></form> </body></html>"};

const char html_main_begin[] = {"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width'></head><body style='font-family:verdana;'><h2 style='font-family:verdana;'>WiHome Main Page</h2>"};
const char html_main_form_begin[] = {"<form action='/' style='font-family:verdana;'>"};
const char html_main_form_end[] = {"<br><input type='submit' name='submit' value='save'><input type='submit' name='submit' value='reload'></form> </body></html>"};

class WiHomeComm
{
  private:
    bool testbool = true;
    // UserData variables and configuration
    char ssid[32];
    char password[32];
    char client[32];
    bool homekit_reset = false;
    // ConfigFileJSON:
    ConfigFileJSON* config;
    // SoftAP configuration
    char ssid_softAP[32];
    ESP8266WebServer* config_webserver = NULL;
    ESP8266WebServer* main_webserver = NULL;
    DNSServer* dnsServer = NULL;
    const byte DNS_PORT = 53;
    // WiHome UDP communication configuration
    WiFiUDP Udp;
    unsigned int localUdpPort = 24557; //24559;
    char incomingPacket[255];
    IPAddress hubip;
    EnoughTimePassed* etp_findhub = NULL;
    bool hub_discovered = false;
    bool wihome_protocol = true;
    bool connect_wifi = true;
    // Settings for WiFi persistence
    EnoughTimePassed* etp_Wifi = NULL;
    bool needMDNS = true;
    enum WIHOME_STATES
    {
      WH_INIT,        // 0
      WH_STOP_SOFTAP, // 1
      WH_STOP_MDNS,   // 2
      WH_STOP_UDP,    // 3
      WH_STOP_STA,    // 4
      WH_START_STA,   // 5
      WH_WAITFOR_STA, // 6
      WH_START_MDNS,  // 7
      WH_START_OTA,   // 8
      WH_START_UDP,   // 9
      WH_CONNECTED,   // 10
      WH_NO_WIFI,     // 11
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
    void LoadUserData();
    void SaveUserData();
    // Methods for common code between Config and Main web server:
    void AddFormItems(String &html, bool show_secure=false);
    // Config web server for SoftAP mode:
    void CreateConfigWebServer(int port);
    void DestroyConfigWebServer();
    void handleRootConfig();
    void handleNotFoundConfig();
    void handleSaveAndRestartConfig();
    void handleClientConfig();
    // Main web server for regular Wifi connection:
    void CreateMainWebServer(int port);
    void DestroyMainWebServer();
    void handleRootMain();
    void handleSaveMain();
    void handleClientMain();
    // WiHome communication methods:
    void findhub();
    void serve_packet(DynamicJsonDocument& doc);
    void init(bool _wihome_protocol, bool _connect_wifi);
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
    // Additional config parameters:
    unsigned int N_config_paras = 0;
    void* pParas[16]; // Storage for up to 16 pointers to additional parameters
    enum datatypes
    {
      TYPE_BOOL,
      TYPE_BYTE,
      TYPE_INT,
      TYPE_UINT,
      TYPE_FLOAT,
      TYPE_CSTR,
      TYPE_STRING,
    } tParas[16]; // Storage for datatypes of up to 16 parameters
    const char* pPrompts[16]; // Storage for up to 16 pointers to prompts for additional parameters
    const char* pNames[16]; // Storage for up to 16 pointers to names of additional parameters
    bool hParas[16]; // Storage for indicating hidden (on the main web page) parameters
    // Functions to handle config parameters:
    unsigned int parameter_index_by_name(const char* pName);
    // Pointer to html string to display on main web server page:
    String* main_html;
  public:
    WiHomeComm();
    WiHomeComm(bool _wihome_protocol);  // Optional argument to deactivate wihome UDP communication protocol
    WiHomeComm(bool _wihome_protocol, bool _connect_wifi); // Optional argument to not connect to a Wifi AP
    void set_status_led(SignalLED* _status_led);
    void set_status_led(SignalLED* _status_led, unsigned int* _led_status);
    void set_status_led(SignalLED* _status_led, SignalLED* _relay);
    void set_status_led(SignalLED* _status_led, RGBstrip* _rgbstrip);
    void set_button(NoBounceButtons* _nbb, unsigned char _button);
    void set_button(NoBounceButtons* _nbb, unsigned char _button, unsigned char _softAP_trigger);
    void get_client_name(char* target);
    byte status(); // get connection status
    void check();
    void check(DynamicJsonDocument& doc);
    void send(DynamicJsonDocument& doc);
    bool softAPmode = false;
    bool is_homekit_reset();
    // Template functions to write s variable number of input parameters as JSON object:
    template<typename... Args>
    void sendJSON(Args... args)
    {
        DynamicJsonDocument doc(1024);
        assembleJSON(doc, args...);
        send(doc);
    }
    // Methods for handling external parameters on config & main web page:
    void add_config_parameter(void* pPara, const char* pName, const char* pPrompt, datatypes tPara);
    void add_config_parameter(char* pPara, const char* pName, const char* pPrompt);
    void add_config_parameter(float* pPara, const char* pName, const char* pPrompt);
    void add_config_parameter(bool* pPara, const char* pName, const char* pPrompt);
    void update_config_parameter(int n, const char* value);
    void get_config_parameter_string(char* str, int n);
    void get_config_parameter_string_by_name(char* str, const char* pName);
    void secure_parameter(const char* pName);
    // Methods to handle external html content for main web page:
    void attach_html(String* _main_html);
    void detach_html();
};

#endif // WIHOMECOMM_H
