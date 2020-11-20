// Class to handle Wifi communication
// for WiHome devices
// Author: Gernot Fattinger (2019)

#include "WiHomeComm.h"

WiHomeComm::WiHomeComm() // setup WiHomeComm object
{
  init(true, 0);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol) // setup WiHomeComm object
{
  init(_wihome_protocol, 0);
}

WiHomeComm::WiHomeComm(unsigned int _nvm_offset) // setup WiHomeComm object
{
  init(true, _nvm_offset);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol, unsigned int _nvm_offset) // setup WiHomeComm object
{
  init(_wihome_protocol, _nvm_offset);
}

void WiHomeComm::init(bool _wihome_protocol, unsigned int _nvm_offset)
{
  wihome_protocol = _wihome_protocol;
  NVM_Offset_UserData = _nvm_offset;
  jnvm = new Json_NVM(NVM_Offset_UserData, 512);
  jnvm->dump_NVM();
  LoadUserData();
  strcpy(ssid_softAP, "WiHome_SoftAP");
  hubip = IPAddress(0,0,0,0);
  Serial.printf("WiHomeComm initializing ...\nUser data loaded from NVM:\n");
  Serial.printf("SSID: %s, password: %s, client: %s\n",ssid,password,client);
  etp_Wifi = new EnoughTimePassed(WIHOMECOMM_RECONNECT_INTERVAL);
}

byte WiHomeComm::status()
{
  // 4: SoftAP mode, 3: WiFi not connected, 2: WiFi connected but hub not found,
  // 1: WiFi connected and hub found, 0: unknown problem
  if (WiFi.getMode()==WIFI_STA)
  {
    if (WiFi.status()==WL_CONNECTED)
    {
      if (hub_discovered || !wihome_protocol)
        return 1;
      else
        return 2;
    }
    else
      return 3;
  }
  else if (WiFi.getMode()==WIFI_AP)
    return 4;
  else
    return 0;
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
  if (WiFi.status()!=WL_CONNECTED || WiFi.getMode()!=WIFI_STA)
  {
    if (etp_Wifi->enough_time())
    {
      connect_count++;
      Serial.printf("Connection attempt #%d.\n",connect_count);
      if ((connect_count>WIHOMECOMM_MAX_CONNECT_COUNT) && (WIHOMECOMM_MAX_CONNECT_COUNT>0))
      {
        Serial.printf("Maximum number of connection attempts reached - rebooting!\n");
        delay(1000);
        ESP.restart();
      }
      DestroyConfigWebServer();
      WiFi.softAPdisconnect(true);
      if (WiFi.isConnected())
        WiFi.disconnect();
      if (wihome_protocol)
      {
        Udp.stop();
        if (etp_findhub)
          delete etp_findhub;
        Serial.println("UDP services stopped.");
      }
      while (WiFi.status()==WL_CONNECTED)
        delay(50);
      Serial.printf("Connecting to %s\n",ssid);
      WiFi.begin(ssid,password);
      WiFi.hostname(client);
      needMDNS=true;
    }
  }
  if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA && needMDNS)
  {
    Serial.printf("Connected to station (IP=%s, name=%s).\nSetting up MDNS client:\n",
                  WiFi.localIP().toString().c_str(), WiFi.hostname().c_str());
    if (!MDNS.begin(client))
      Serial.println("Error setting up MDNS responder!");
    else
    {
      Serial.println("mDNS responder started.");
      MDNS.addService("esp", "tcp", 8080); // Announce esp tcp service on port 8080
      needMDNS=false;
      ArduinoOTA.setPort(8266);
      ArduinoOTA.setHostname(client);
      ArduinoOTA.begin();
      Serial.println("OTA service started.");
      if (wihome_protocol)
      {
        Udp.begin(localUdpPort);
        etp_findhub = new EnoughTimePassed(WIHOMECOMM_FINDHUB_INTERVAL);
        Serial.println("UDP services created.");
      }
    }
  }
  if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA && !needMDNS)
  {
    connect_count = 0;
    ArduinoOTA.handle();
    if (wihome_protocol)
      findhub();
    return true;
  }
  hub_discovered = false;
  return false;
}

void WiHomeComm::ConnectSoftAP()
{
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

bool WiHomeComm::LoadUserData()
{
  DynamicJsonDocument doc(1024);
  bool valid = jnvm->readJSON(doc);
  if (valid)
  {
    String s_ssid = doc["ssid"];
    String s_password = doc["password"];
    String s_client = doc["client"];
    strcpy(ssid, s_ssid.c_str());
    strcpy(password, s_password.c_str());
    strcpy(client, s_client.c_str());
  }
  return valid;
}

void WiHomeComm::SaveUserData()
{
  jnvm->writeJSON("ssid", ssid, "password", password, "client", client);
  jnvm->dump_NVM();
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
  }
  if (dnsServer)
  {
    dnsServer->stop();
    delete dnsServer;
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
  }
  Serial.println("--- Data to be saved begin ---");
  Serial.println(ssid);
  Serial.println(password);
  Serial.println(client);
  Serial.println("--- Data to be saved end ---");
  SaveUserData();
  message += "Userdata saved to EEPROM.\n";
  Serial.println("Userdata saved to EEPROM.");
  webserver->send(200, "text/plain", message);
  delay(500);
  softAPmode = false;
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
    // Serial.printf("\nBroadcast findhub message.\n");
    IPAddress ip = WiFi.localIP();
    IPAddress subnetmask = WiFi.subnetMask();
    IPAddress broadcast_ip(0,0,0,0);
    for (int n=0; n<4; n++)
      broadcast_ip[n] = (ip[n] & subnetmask[n]) | ~subnetmask[n];
    // DynamicJsonBuffer jsonBuffer;
    // JsonObject& root = jsonBuffer.createObject();
    DynamicJsonDocument doc(1024);
    doc["cmd"]="findhub";
    doc["client"]=client;
    Udp.beginPacket(broadcast_ip, localUdpPort);
    // root.printTo(Udp);
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

    // JsonObject& root = jsonBuffer->parseObject(incomingPacket);
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
            // root.printTo(Udp);
            serializeJson(doc, Udp);
            Udp.endPacket();
            hubip = Udp.remoteIP();
            hub_discovered = true;
            // return JsonObject::invalid();
          }
        }
        if (doc["cmd"]=="hubid")
        {
          // Serial.printf("DISCOVERED HUB: %s\n",Udp.remoteIP().toString().c_str());
          hubip = Udp.remoteIP();
          hub_discovered = true;
          // return JsonObject::invalid();
        }
      }
    }
    // return root;
  }
  // else
  //   return JsonObject::invalid();
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
