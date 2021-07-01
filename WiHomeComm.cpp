// Class to handle Wifi communication
// for WiHome devices
// Author: Gernot Fattinger (2019-2020)

#include "WiHomeComm.h"

WiHomeComm::WiHomeComm() // setup WiHomeComm object
{
  init(true);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol)// setup WiHomeComm object
{
  init(_wihome_protocol);
}

void WiHomeComm::init(bool _wihome_protocol)
{
  wihome_protocol = _wihome_protocol;
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
      Serial.printf("Connecting to %s with password %s ",ssid, password);
      WiFi.begin(ssid,password);
      WiFi.hostname(client);
      WiFi.setAutoReconnect(true);
      connect_state = WH_WAITFOR_STA;
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
  }
  if (connect_state == WH_CONNECTED)
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
    if (!webserver)
      CreateConfigWebServer(80);
    else
      handleClient();
  }

}

void WiHomeComm::LoadUserData()
{
  config->get("ssid", ssid, "password", password, "client", client, "homekit_reset", &homekit_reset);
}

void WiHomeComm::SaveUserData()
{
  config->set("ssid", ssid, "password", password, "client", client, "homekit_reset", homekit_reset);
  config->dump();
}

void WiHomeComm::CreateConfigWebServer(int port)
{
  // Setup the DNS server redirecting all the domains to the apIP
  IPAddress apIP(192, 168, 4, 1);
  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", apIP);
  webserver = new ESP8266WebServer(port);
  webserver->on("/", std::bind(&WiHomeComm::handleRoot, this));
  webserver->onNotFound(std::bind(&WiHomeComm::handleRoot, this));
  webserver->on("/save_and_restart.php", std::bind(&WiHomeComm::handleSaveAndRestart, this));
  webserver->begin();
  Serial.println("HTTP server started.");
}

void WiHomeComm::DestroyConfigWebServer()
{
  if(webserver)
  {
    webserver->stop();
    delete webserver;
    webserver = NULL;
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

void WiHomeComm::handleRoot()
{
  LoadUserData();
  String html = html_config_form1;
  html += ssid;
  html += html_config_form2;
  html += password;
  html += html_config_form3;
  html += client;
  html += html_config_form4;
    webserver->send(200, "text/html", html);
}

void WiHomeComm::handleSaveAndRestart()
{
  char buf[32];
  String message = "Save and Restart\n\n";
  homekit_reset = false;
  message += "URI: ";
  message += webserver->uri();
  message += "\nMethod: ";
  message += (webserver->method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += webserver->args();
  message += "\n";
  for (uint8_t i=0; i<webserver->args(); i++)
  {
    message += " " + webserver->argName(i) + ": " + webserver->arg(i) + "\n";
    if ((webserver->argName(i)).compareTo("ssid")==0)
      strcpy(ssid, (webserver->arg(i)).c_str());
    if ((webserver->argName(i)).compareTo("password")==0)
      strcpy(password, (webserver->arg(i)).c_str());
    if ((webserver->argName(i)).compareTo("client")==0)
      strcpy(client, (webserver->arg(i)).c_str());
    if ((webserver->argName(i)).compareTo("homekit_reset")==0)
      homekit_reset = true;
  }
  Serial.println("--- Data to be saved begin ---");
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(client);
  Serial.println(homekit_reset);
  Serial.println("--- Data to be saved end ---");
  SaveUserData();
  message += "Userdata saved.\n";
  Serial.println("Userdata saved.");
  webserver->send(200, "text/plain", message);
  delay(500);
  softAPmode = false;
  ESP.restart();
}

void WiHomeComm::handleClient()
{
  dnsServer->processNextRequest();
  webserver->handleClient();
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
