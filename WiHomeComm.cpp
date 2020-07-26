// Class to handle Wifi communication
// for WiHome devices
// Author: Gernot Fattinger (2019)

#include "WiHomeComm.h"

WiHomeComm::WiHomeComm() // setup WiHomeComm object
{
  init(true);
}

WiHomeComm::WiHomeComm(bool _wihome_protocol) // setup WiHomeComm object
{
  init(_wihome_protocol);
}

void WiHomeComm::init(bool _wihome_protocol)
{
  wihome_protocol = _wihome_protocol;
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

void WiHomeComm::check()
{
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = check(&jsonBuffer);
}

JsonObject& WiHomeComm::check(DynamicJsonBuffer* jsonBuffer)
{
  check_status_led();
  if (softAPmode==false)
  {
    if (ConnectStation() && wihome_protocol)
    {
      JsonObject& root = serve_packet(jsonBuffer);
      return root;
    }
  }
  else
    ConnectSoftAP();
  return JsonObject::invalid();
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
  EEPROM.begin(97);
  strcpy(ssid, "");
  strcpy(password, "");
  strcpy(client, "");
  EEPROM.get(NVM_Offset_UserData, ud_id);
  if (ud_id != valid_ud_id)
    return false;
  EEPROM.get(NVM_Offset_UserData+1,ssid);
  EEPROM.get(NVM_Offset_UserData+33,password);
  EEPROM.get(NVM_Offset_UserData+65,client);
  EEPROM.end();
  return true;
}

void WiHomeComm::SaveUserData()
{
  EEPROM.begin(97);
  EEPROM.put(NVM_Offset_UserData+1,ssid);
  EEPROM.put(NVM_Offset_UserData+33,password);
  EEPROM.put(NVM_Offset_UserData+65,client);
  ud_id = valid_ud_id;
  EEPROM.write(NVM_Offset_UserData, ud_id);
  EEPROM.end();
  delay(100);
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
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["cmd"]="findhub";
    root["client"]=client;
    Udp.beginPacket(broadcast_ip, localUdpPort);
    root.printTo(Udp);
    Udp.endPacket();
  }
}

JsonObject& WiHomeComm::serve_packet(DynamicJsonBuffer* jsonBuffer)
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

    JsonObject& root = jsonBuffer->parseObject(incomingPacket);
    // Test if parsing succeeds.
    if (!root.success())
    {
      Serial.println("parseObject() failed");
    }
    else
    {
      if (root.containsKey("cmd"))
      {
        if (root["cmd"]=="findclient" && root.containsKey("client"))
        {
          if (strcmp(root["client"],client)==0)
          {
            root["cmd"] = "clientid";
            Udp.beginPacket(Udp.remoteIP(), localUdpPort);
            root.printTo(Udp);
            Udp.endPacket();
            hubip = Udp.remoteIP();
            hub_discovered = true;
            return JsonObject::invalid();
          }
        }
        if (root["cmd"]=="hubid")
        {
          // Serial.printf("DISCOVERED HUB: %s\n",Udp.remoteIP().toString().c_str());
          hubip = Udp.remoteIP();
          hub_discovered = true;
          return JsonObject::invalid();
        }
      }
    }
    return root;
  }
  else
    return JsonObject::invalid();
}

void WiHomeComm::send(JsonObject& root)
{
  if (WiFi.status() == WL_CONNECTED && WiFi.getMode() == WIFI_STA && !needMDNS && wihome_protocol)
  {
    root["client"]=client;
    Udp.beginPacket(hubip, localUdpPort);
    root.printTo(Udp);
    Udp.endPacket();
  }
}
