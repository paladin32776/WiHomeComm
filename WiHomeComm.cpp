// Class to handle Wifi communication
// for WiHome devices
// Author: Gernot Fattinger (2019-2020)

#include "WiHomeComm.h"

WiHomeComm::WiHomeComm() // setup WiHomeComm object
{
  init(true, true);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol) // setup WiHomeComm object
{
  init(_wihome_protocol, true);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol, bool _connect_wifi) // setup WiHomeComm object
{
  init(_wihome_protocol, _connect_wifi);
}

void WiHomeComm::init(bool _wihome_protocol, bool _connect_wifi)
{
  main_html = NULL;
  wihome_protocol = _wihome_protocol;
  connect_wifi = _connect_wifi;
  Serial.printf("WiHomeComm initializing ...\n");
  config = new ConfigFileJSON("wihome.cfg");
  Serial.printf("Loading user data from SPIFFS file:\n");
  LoadUserData();
  Serial.printf("SSID: %s, password: %s, client: %s\n",ssid,password,client);
  strcpy(ssid_softAP, "WiHome_SoftAP");
  hubip = IPAddress(0,0,0,0);
  etp_Wifi = new EnoughTimePassed(WIHOMECOMM_WAITFOR_CONNECT_INTERVAL);
  connect_state = WH_INIT;
}

byte WiHomeComm::status()
{
  // 4: SoftAP mode, 3: WiFi not connected, 2: WiFi connected but hub not found,
  // 1: WiFi connected and hub found, 0: unknown problem
  if (WiFi.getMode()==WIFI_STA)
  {
    if (connect_state == WH_CONNECTED)
    {
      if (hub_discovered || !wihome_protocol)
        return WIHOMECOMM_CONNECTED;
      else
        return WIHOMECOMM_NOHUB;
    }
    else
      return WIHOMECOMM_DISCONNECTED;
  }
  else if (WiFi.getMode()==WIFI_AP)
    return WIHOMECOMM_SOFTAP;
  else
    return WIHOMECOMM_UNKNOWN;
}

void WiHomeComm::set_status_led(SignalLED* _status_led)
{
  status_led = _status_led;
  handle_status_led = 1;
}

void WiHomeComm::set_status_led(SignalLED* _status_led, unsigned int* _led_status)
{
  status_led = _status_led;
  led_status = _led_status;
  handle_status_led = 2;
}

void WiHomeComm::set_status_led(SignalLED* _status_led, SignalLED* _relay)
{
  status_led = _status_led;
  relay = _relay;
  handle_status_led = 3;
}

void WiHomeComm::set_status_led(SignalLED* _status_led, RGBstrip* _rgbstrip)
{
  status_led = _status_led;
  rgbstrip = _rgbstrip;
  handle_status_led = 4;
}

void WiHomeComm::check_status_led()
{
  if (handle_status_led)
  {
    // Logic for LED status display:
    if (status()==1)
    {
      if (handle_status_led==1)
        status_led->set(SLED_OFF);
      else if (handle_status_led==2)
        status_led->set(*led_status);
      else if (handle_status_led==3)
        status_led->set(relay->get());
      else if (handle_status_led==4)
        status_led->set(rgbstrip->get_on());
    }
    else if (status()==2)
      status_led->set(SLED_BLINK_FAST_3);
    else if (status()==3)
      status_led->set(SLED_BLINK_FAST_1);
    else if (status()==4)
      status_led->set(SLED_BLINK_SLOW);
    else
      status_led->set(SLED_BLINK_FAST);
  }
}

void WiHomeComm::set_button(NoBounceButtons* _nbb, unsigned char _button)
{
  set_button(_nbb, _button, NBB_LONG_CLICK);
}

void WiHomeComm::set_button(NoBounceButtons* _nbb, unsigned char _button, unsigned char _softAP_trigger)
{
  nbb = _nbb;
  button = _button;
  softAP_trigger = _softAP_trigger;
  handle_button = true;
}

void WiHomeComm::get_client_name(char* target)
{
  strcpy(target, client);
}

void WiHomeComm::check_button()
{
  if (handle_button)
  {
    if ((softAPmode == false) && (nbb->action(button) == softAP_trigger))
    {
      softAPmode = true;
      nbb->reset(button);
    }
    else if ((softAPmode == true) && (nbb->action(button) == NBB_CLICK))
    {
      softAPmode = false;
      nbb->reset(button);
    }
  }
}

void WiHomeComm::check()
{
  DynamicJsonDocument doc(128);
  check(doc);
}

void WiHomeComm::check(DynamicJsonDocument& doc)
{
  check_status_led();
  check_button();
  if (softAPmode==false)
  {
    if (ConnectStation() && wihome_protocol)
      serve_packet(doc);
  }
  else
    ConnectSoftAP();
}

bool WiHomeComm::ConnectStation()
{
  switch (connect_state)
  {
    case WH_INIT:
      hub_discovered = false;
      connect_state = WH_STOP_SOFTAP;
      break;
    case WH_STOP_SOFTAP:
      DestroyConfigWebServer();
      if (WiFi.getMode() == WIFI_AP)
      {
        if (WiFi.softAPdisconnect(true))
        {
          connect_state = WH_STOP_MDNS;
          Serial.println("Stopped softAP mode.");
        }
        else
          connect_state = WH_ERROR;
      }
      else
        connect_state = WH_STOP_MDNS;
      break;
    case WH_STOP_MDNS:
      if (wihome_protocol && MDNS.isRunning())
      {
        Serial.println("Running mDNS responder detected.");
        if (MDNS.removeService("esp"))
          Serial.println("mDNS service 'esp' stopped.");
        if (MDNS.end())
        {
          Serial.println("mDNS responder stopped.");
          connect_state = WH_STOP_UDP;
        }
        else
        {
          Serial.println("[ERROR] Could not stop mDNS responder.");
          connect_state = WH_ERROR;
        }
      }
      else
        connect_state = WH_STOP_UDP;
      break;
    case WH_STOP_UDP:
      if (wihome_protocol)
      {
        Udp.stop();
        if (etp_findhub)
          delete etp_findhub;
        etp_findhub = NULL;
        hub_discovered = false;
        Serial.println("UDP services stopped.");
      }
      connect_state = WH_STOP_STA;
      break;
    case WH_STOP_STA:
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(true);
      if (!WiFi.isConnected() && WiFi.getMode() == WIFI_OFF)
        connect_state = WH_START_STA; // everything should be disconnected and turned off at this point
      else
        connect_state = WH_ERROR;
      break;
    case WH_START_STA:
      if (connect_wifi)
      {  
        Serial.printf("Connecting to %s with password %s ",ssid, password);
        WiFi.begin(ssid,password);
        WiFi.hostname(client);
        WiFi.setAutoReconnect(true);
        connect_state = WH_WAITFOR_STA;
      }
      else
        connect_state = WH_NO_WIFI;
      break;
    case WH_WAITFOR_STA:
      if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA)
      {
        Serial.printf("\nConnected to station (IP=%s, name=%s).\n",
                      WiFi.localIP().toString().c_str(), WiFi.hostname().c_str());
        connect_state = WH_START_MDNS;
      }
      else if (etp_Wifi->enough_time())
        Serial.printf(".");
      break;
    case WH_START_MDNS:
      if (wihome_protocol)
      {
        if (MDNS.begin(client))
        {
          Serial.println("mDNS responder started.");
          MDNS.addService("esp", "tcp", 8080); // Announce esp tcp service on port 8080
          connect_state = WH_START_OTA;
        }
        else
        {
          Serial.println("Error setting up MDNS responder!");
          connect_state = WH_ERROR;
        }
      }
      else
        connect_state = WH_START_OTA;
      break;
    case WH_START_OTA:
      ArduinoOTA.setPort(8266);
      ArduinoOTA.setHostname(client);
      ArduinoOTA.begin();
      Serial.println("OTA service started.");
      connect_state = WH_START_UDP;
      break;
    case WH_START_UDP:
      if (wihome_protocol)
      {
        Udp.begin(localUdpPort);
        etp_findhub = new EnoughTimePassed(WIHOMECOMM_FINDHUB_INTERVAL);
        Serial.println("UDP services created.");
      }
      connect_state = WH_CONNECTED;
      break;
    case WH_CONNECTED:
      ArduinoOTA.handle();
      if (wihome_protocol)
        findhub();
      break;
    case WH_NO_WIFI:
      break;
  }
  if (connect_state == WH_CONNECTED)
  {
    if (!main_webserver)
    {
      if (config_webserver)
        DestroyConfigWebServer();
      CreateMainWebServer(80);
    }
    else
      handleClientMain();
  }
  if ((connect_state == WH_CONNECTED) || (connect_state == WH_NO_WIFI))
    return true;
  return false;
}

void WiHomeComm::ConnectSoftAP()
{
  connect_state = WH_INIT;
  if (WiFi.status()!=WL_DISCONNECTED || WiFi.getMode()!=WIFI_AP)
  {
    hub_discovered = false;
    Serial.printf("Going to SoftAP mode:\n");
    WiFi.softAPdisconnect(true);
    if (WiFi.isConnected())
      WiFi.disconnect(true);
    while (WiFi.status()==WL_CONNECTED)
      delay(50);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    if (WiFi.softAP(ssid_softAP))
    {
      Serial.printf("Soft AP created!\n");
      Serial.printf("SoftAP IP: %s\n",WiFi.softAPIP().toString().c_str());
      Serial.printf("Status/Mode: %d/%d\n",WiFi.status(),WiFi.getMode());
    }
    else
    {
      Serial.printf("Soft AP creation FAILED.\n");
    }
  }
  else
  {
    if (!config_webserver)
    {
      if (main_webserver)
        DestroyMainWebServer();
      CreateConfigWebServer(80);
    }
    else
      handleClientConfig();
  }

}

void WiHomeComm::LoadUserData()
{
  config->get("ssid", ssid, "password", password, "client", client, "homekit_reset", &homekit_reset);
  if (N_config_paras>0)
      for (int n=0; n<N_config_paras; n++)
      {
        switch(tParas[n])
        {
          case TYPE_CSTR:
            config->get(pNames[n], (char*) pParas[n]);
          case TYPE_FLOAT:
            config->get(pNames[n], (float*) pParas[n]);
        }
      }
  config->dump();
}

void WiHomeComm::SaveUserData()
{
  config->set_nowrite("ssid", ssid, "password", password, "client", client, "homekit_reset", homekit_reset);
    if (N_config_paras>0)
      for (int n=0; n<N_config_paras; n++)
      {
        switch(tParas[n])
        {
          case TYPE_CSTR:
            config->set_nowrite(pNames[n], (char*) pParas[n]);
          case TYPE_FLOAT:
            config->set_nowrite(pNames[n], *((float*) pParas[n]));
        }
      }
  config->set("dummy", 0);
  config->dump();
}

void WiHomeComm::CreateConfigWebServer(int port)
{
  // Setup the DNS server redirecting all the domains to the apIP
  IPAddress apIP(192, 168, 4, 1);
  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", apIP);
  config_webserver = new ESP8266WebServer(port);
  config_webserver->on("/", std::bind(&WiHomeComm::handleRootConfig, this));
  config_webserver->onNotFound(std::bind(&WiHomeComm::handleRootConfig, this));
  config_webserver->on("/save_and_restart.php", std::bind(&WiHomeComm::handleSaveAndRestartConfig, this));
  config_webserver->begin();
  Serial.println("HTTP server started.");
}

void WiHomeComm::DestroyConfigWebServer()
{
  if(config_webserver)
  {
    config_webserver->stop();
    delete config_webserver;
    config_webserver = NULL;
    Serial.println("Destroyed web server.");
  }
  if (dnsServer)
  {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = NULL;
    Serial.println("Destroyed DNS server.");
  }
}

void WiHomeComm::handleRootConfig()
{
  LoadUserData();
  String html = html_config_form_begin;
  html += html_config_form1;
  html += ssid;
  html += html_config_form2;
  html += password;
  html += html_config_form3;
  html += client;
  if (N_config_paras>0)
    for (int n=0; n<N_config_paras; n++)
    {
      char str[32];
      get_config_parameter_string(str, n);
      html += "'><br>";
      html += pPrompts[n];
      html += "<br><input type='text' name='";
      html += pNames[n];
      html += "' value='";
      html += str;
    }
  html += html_config_form_end;
    config_webserver->send(200, "text/html", html);
}

void WiHomeComm::handleSaveAndRestartConfig()
{
  String message = "Save and Restart\n\n";
  homekit_reset = false;
  message += "URI: ";
  message += config_webserver->uri();
  message += "\nMethod: ";
  message += (config_webserver->method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += config_webserver->args();
  message += "\n";
  for (uint8_t i=0; i<config_webserver->args(); i++)
  {
    message += " " + config_webserver->argName(i) + ": " + config_webserver->arg(i) + "\n";
    if ((config_webserver->argName(i)).compareTo("ssid")==0)
      strcpy(ssid, (config_webserver->arg(i)).c_str());
    if ((config_webserver->argName(i)).compareTo("password")==0)
      strcpy(password, (config_webserver->arg(i)).c_str());
    if ((config_webserver->argName(i)).compareTo("client")==0)
      strcpy(client, (config_webserver->arg(i)).c_str());
    if ((config_webserver->argName(i)).compareTo("homekit_reset")==0)
      homekit_reset = true;
    if (N_config_paras>0)
      for (int n=0; n<N_config_paras; n++)
        if ((config_webserver->argName(i)).compareTo(pNames[n])==0)
          update_config_parameter(n, (config_webserver->arg(i)).c_str());   
  }
  Serial.println("--- Data to be saved begin ---");
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(client);
  Serial.println(homekit_reset);
  Serial.println("--- Data to be saved end ---");
  Serial.println("--- Additional parameters begin ---");
  if (N_config_paras>0)
    for (int n=0; n<N_config_paras; n++)
    {
      char str[32];
      get_config_parameter_string(str, n);
      Serial.print(pNames[n]);
      Serial.print(": ");
      Serial.println(str);
    }
  Serial.println("--- Additional parameters end ---");    
  SaveUserData();
  message += "Userdata saved.\n";
  Serial.println("Userdata saved.");
  config_webserver->send(200, "text/plain", message);
  delay(500);
  softAPmode = false;
  ESP.restart();
}

void WiHomeComm::handleClientConfig()
{
  dnsServer->processNextRequest();
  config_webserver->handleClient();
}

void WiHomeComm::CreateMainWebServer(int port)
{
  main_webserver = new ESP8266WebServer(port);
  main_webserver->on("/", std::bind(&WiHomeComm::handleRootMain, this));
  main_webserver->onNotFound(std::bind(&WiHomeComm::handleRootMain, this));
  // main_webserver->on("/save.php", std::bind(&WiHomeComm::handleSaveMain, this));
  main_webserver->begin();
  Serial.println("HTTP main server started.");
}

void WiHomeComm::DestroyMainWebServer()
{
  if(main_webserver)
  {
    main_webserver->stop();
    delete main_webserver;
    main_webserver = NULL;
    Serial.println("Destroyed main web server.");
  }
}

void WiHomeComm::handleRootMain()
{
  // Get submit value from web page submission:
  char submit[32];
  strcpy(submit,"");
  for (uint8_t i=0; i<main_webserver->args(); i++)
    if ((main_webserver->argName(i)).compareTo("submit")==0)
      strcpy(submit, (main_webserver->arg(i)).c_str());
  // If submit=save, save the values in the web form to NVM:
  if (strcmp(submit,"save")==0)
  {
    for (uint8_t i=0; i<main_webserver->args(); i++)
    {
      if (N_config_paras>0)
        for (int n=0; n<N_config_paras; n++)
          if ((main_webserver->argName(i)).compareTo(pNames[n])==0)
            update_config_parameter(n, (main_webserver->arg(i)).c_str());   
    }
    SaveUserData();
    Serial.println("Userdata saved.");
  }
  // Display web page with form:
  LoadUserData();
  String html = html_main_begin;
  html += "Client: ";
  html += client;
  html += "<br>";
  if (main_html)
    html += (*main_html);
  html += html_main_form_begin;
  if (N_config_paras>0)
    for (int n=0; n<N_config_paras; n++)
    {
      char str[32];
      get_config_parameter_string(str, n);
      html += "<br>";
      html += pPrompts[n];
      html += "<br><input type='text' name='";
      html += pNames[n];
      html += "' value='";
      html += str;
      html += "'>";
    }
  html += html_main_form_end;
    main_webserver->send(200, "text/html", html);
}

void WiHomeComm::handleClientMain()
{
  main_webserver->handleClient();
}

void WiHomeComm::findhub()
{
  if (etp_findhub->enough_time() && wihome_protocol)
  {
    Serial.printf("\nBroadcast findhub message.\n");
    IPAddress ip = WiFi.localIP();
    IPAddress subnetmask = WiFi.subnetMask();
    IPAddress broadcast_ip(0,0,0,0);
    for (int n=0; n<4; n++)
      broadcast_ip[n] = (ip[n] & subnetmask[n]) | ~subnetmask[n];
    DynamicJsonDocument doc(1024);
    doc["cmd"]="findhub";
    doc["client"]=client;
    Udp.beginPacket(broadcast_ip, localUdpPort);
    serializeJson(doc, Udp);
    Udp.endPacket();
  }
}

void WiHomeComm::serve_packet(DynamicJsonDocument& doc)
{
  int packetSize;
  if (wihome_protocol)
    packetSize = Udp.parsePacket();
  if (packetSize && wihome_protocol)
  {
    // Serial.printf("\nReceived %d bytes from %s, port %d\n", packetSize,
    //               Udp.remoteIP().toString().c_str(), Udp.remotePort());
    int len = Udp.read(incomingPacket, 255);
    if (len > 0)
      incomingPacket[len] = 0;
    // Serial.printf("UDP packet contents: %s\n", incomingPacket);

    DeserializationError error = deserializeJson(doc, incomingPacket);
    // Test if parsing succeeds.
    if (error)
    {
      Serial.println("deserializeJson() failed");
    }
    else
    {
      if (doc.containsKey("cmd"))
      {
        if (doc["cmd"]=="findclient" && doc.containsKey("client"))
        {
          if (strcmp(doc["client"],client)==0)
          {
            doc["cmd"] = "clientid";
            Udp.beginPacket(Udp.remoteIP(), localUdpPort);
            serializeJson(doc, Udp);
            Udp.endPacket();
            hubip = Udp.remoteIP();
            hub_discovered = true;
          }
        }
        if (doc["cmd"]=="hubid")
        {
          Serial.printf("Found hub: %s\n",Udp.remoteIP().toString().c_str());
          hubip = Udp.remoteIP();
          hub_discovered = true;
        }
      }
    }
  }
}

void WiHomeComm::send(DynamicJsonDocument& doc)
{
  if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA && !needMDNS && wihome_protocol)
  {
    doc["client"]=client;
    Udp.beginPacket(hubip, localUdpPort);
    serializeJson(doc, Udp);
    Udp.endPacket();
  }
}

bool WiHomeComm::is_homekit_reset()
{
  bool _homekit_reset = homekit_reset;
  homekit_reset = false;
  SaveUserData();
  return _homekit_reset;
}

void WiHomeComm::add_config_parameter(void* pPara, const char* pName, const char* pPrompt, datatypes tPara)
{
  pParas[N_config_paras] = pPara;
  pPrompts[N_config_paras] = pPrompt;
  tParas[N_config_paras] = tPara;
  pNames[N_config_paras] = pName;
  N_config_paras++;
  LoadUserData();
}

void WiHomeComm::add_config_parameter(char* pPara, const char* pName, const char* pPrompt)
{
  add_config_parameter((void*)pPara, pName, pPrompt, TYPE_CSTR);
}

void WiHomeComm::add_config_parameter(float* pPara, const char* pName, const char* pPrompt)
{
  add_config_parameter((void*)pPara, pName, pPrompt, TYPE_FLOAT);
}

void WiHomeComm::update_config_parameter(int n, const char* value)
{
  switch (tParas[n])
  {
    case TYPE_CSTR:
      strcpy((char*) pParas[n], value);
      break;
    case TYPE_FLOAT:
      *((float*) pParas[n]) = String(value).toFloat();
      break;
  }
}

void WiHomeComm::get_config_parameter_string(char* str, int n)
{
  switch (tParas[n])
  {
    case TYPE_CSTR:
      strcpy(str, (char*) pParas[n]);
      break;
    case TYPE_FLOAT:
      dtostrf(*((float*) pParas[n]), 10, 7, str);
      break;
  }
}

void WiHomeComm::attach_html(String* _main_html)
{
  main_html = _main_html;
}

void WiHomeComm::detach_html()
{
  main_html = NULL;
}

