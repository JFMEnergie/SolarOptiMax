// Code by Matthieu CHEUTIN for Copyright © 2023 JFM ENERGIE SARL

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <cstdlib>
#include "cert.h"
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <PZEM004Tv30.h>
#include "time.h"
#include <TimeLib.h>
#include <vector>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define PZEM_SERIAL Serial2
PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
#define NUM_PZEMS 2
PZEM004Tv30 pzems[NUM_PZEMS];
#define LY_ADDRESS 0x10
#define PV_ADDRESS 0x11

LiquidCrystal_I2C lcd(0x27, 20, 4);

String FirmwareVer = {
  "S01.02"
};
#define URL_fw_Version "https://raw.githubusercontent.com/JFMEnergie/SolarOptiMax/Update/Software/Versions/Download/Releases/fw.ino/bin_version.txt"
#define URL_fw_Bin "https://raw.githubusercontent.com/JFMEnergie/SolarOptiMax/Update/Software/Versions/Download/Releases/fw.ino/fw.bin"

void firmwareUpdate();
int FirmwareVersionCheck();

unsigned long previousMillis = 0;
const long interval = 15*60*1000;

const char* defaultSsid = "SolarOptiMax";
const char* defaultPassword = "ConfigClient";
String ssid;
String password;
String automation_id;
// int maxpower = 0;
// String VAH;
// String VAS;
// String installation_automate;
// String installation_pv;


const int numRelays = 4;
const int relayPin[numRelays] = {32, 26 , 33, 2};
const char* relayNames[numRelays] = {"Relai 1", "Relai 2", "Relai 3", "Relai 4"};
const char* relayPowerDefaults[numRelays] = {"0", "0", "0", "0"};
int SSR1 = 0;
int SSR2 = 0;
int SSR3 = 0;
int SSR4 = 0;
int VAR1 = 0;

unsigned long relayActivatedTime[numRelays] = {0};
const unsigned long relayOffDelay = 15*60;

enum RelayState {
  Inactif = 0,
  ActifDiscontinu = 1,
  ActifContinu = 2
};

struct RelayConfig {
  RelayState etat;
  String power;
  String duration;
};

RelayConfig relayConfigs[numRelays];

int restartcount = 0;
int loops = 0;
int TrueWifiStatus = 1;
int WifiStatus = 0;
int lastbuypower = 0;
int lastbuypf  = 0;
int lastlastbuypf = 0;

const char* serverUrl = "https://solar.frb.io/api/addprodsource";
const char* serverBaseUrl = "https://solar.frb.io/api/getpanoprofil/";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
int lastHour = 0;
unsigned long timeout = millis() + 10000;
const int dataInterval = 60;

int acbuy_power = 0;

struct EnergyData {
  time_t timestamp;
  signed int acbuy_power;
  float acbuy_energy;
  signed int acp_power;
  float acp_energy;
  int SSR1;
  int SSR2;
  int SSR3;
  int SSR4;
  int VAR1;
};
std::vector<EnergyData> energyData;

AsyncWebServer server(80);
Preferences preferences;


void setup() {
  Serial.begin(115200);
  Serial2.begin(9600);
  Serial.println("WELCOME");
  Serial.print("Active firmware version:");
  Serial.println(FirmwareVer);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("SolarOptiMax");
  delay(1000);
  lcd.setCursor(6, 2);
  lcd.print("WELCOME");
  delay(1000);
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("SolarOptiMax");

  for (int i = 0; i < numRelays; i++) {
    relayConfigs[i].etat = Inactif;
    relayActivatedTime[i] = 0;
    pinMode(relayPin[i], OUTPUT);
    digitalWrite(relayPin[i], LOW);
  }

  preferences.begin("config", false);
  ssid = preferences.getString("ssid", ssid);
  password = preferences.getString("password", password);
  automation_id = preferences.getString("automation_id", "0");
  // preferences.putUInt("automation_id", 0);
  // preferences.putUInt("building_id", 0);
  // preferences.putUInt("puissance", 0);
  // preferences.putUInt("nombre_pano", 0);
  // maxpower = preferences.getInt("maxpower", 0);
  // VAH = preferences.getString("VAH", VAH);
  // VAS = preferences.getString("VAS", VAS);
  // installation_automate = preferences.getString("installation_automate", installation_automate);
  // installation_pv = preferences.getString("installation_pv", installation_pv);
  restartcount = preferences.getInt("restartcount", 0);
  bool configured = preferences.getBool("configured", false);
  if (!configured) {
    lcd.setCursor(2, 2);
    lcd.print("CONFIGURATION...");
    delay(1000);  
    startConfigPortal();
    while (!configured) {
      delay(1000);
    }
  }
  else {
    for (int i = 0; i < numRelays; i++) {
      relayConfigs[i].etat = static_cast<RelayState>(preferences.getInt((String("relay") + String(i + 1) + "Etat").c_str(), Inactif));
      relayConfigs[i].power = preferences.getString((String("relay") + String(i + 1) + "Power").c_str(), relayPowerDefaults[i]);
      relayConfigs[i].duration = preferences.getString((String("relay") + String(i + 1) + "Duration").c_str(), "0"); 
    }
    if (ssid.equals(defaultSsid) && password.equals(defaultPassword)) {
      TrueWifiStatus = 0;
      Serial.println("TrueWifiStatus = 0");
    }
    else if (TrueWifiStatus == 1) {
      WifiConnect(ssid, password);
      if (WifiStatus == 1) {
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      }
    }
    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else 
          type = "filesystem";

        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });
    if(WifiStatus == 1){
      ArduinoOTA.setPort(3232);
      ArduinoOTA.setHostname("SolarOptiMax");
      ArduinoOTA.setPassword("solaroptimax");
      ArduinoOTA.begin();
      Serial.println("Ready");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      printConfigData();
    }
    for (int i = 0; i < NUM_PZEMS; i++) {
      pzems[i] = PZEM004Tv30(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN, 0x10 + i);
    }
    delay(1000);
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("SolarOptiMax");
    delay(1000);
    lcd.setCursor(0, 2);
    lcd.print("ADRESSE IP :" + String(WiFi.localIP())); 
  }
}

void startConfigPortal() {
  WiFi.softAP(defaultSsid, defaultPassword);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Point d'accès WiFi démarré à l'adresse IP : ");
  Serial.println(apIP);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getConfigPage());
  });
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    String ssid = request->arg("ssid");
    String password = request->arg("password");
    String automation_id = request->arg("automation_id");
    preferences.putString("ssid", ssid.isEmpty() ? defaultSsid : ssid);
    preferences.putString("password", password.isEmpty() ? defaultPassword : password);
    preferences.putString("automation_id", automation_id);
    for (int i = 0; i < numRelays; i++) {
      String etatArg = "relay" + String(i + 1) + "Etat";
      String powerArg = "relay" + String(i + 1) + "Power";
      String durationArg = "relay" + String(i + 1) + "Duration";
      relayConfigs[i].etat = static_cast<RelayState>(request->arg(etatArg.c_str()).toInt());
      relayConfigs[i].power = request->arg(powerArg.c_str());
      relayConfigs[i].duration = request->arg(durationArg.c_str());
      preferences.putInt(etatArg.c_str(), static_cast<int>(relayConfigs[i].etat));
      preferences.putString(powerArg.c_str(), relayConfigs[i].power);
      preferences.putString(durationArg.c_str(), relayConfigs[i].duration);
    }
    preferences.putBool("configured", true);
    ESP.restart();
  });
  server.begin();
}

String getConfigPage() {
  String page = "<html><head>";
  page += "<title>Configuration SolarOptiMax by JFMEnergie</title>";
  page += "<style>";
  page += "body { font-family: Arial, sans-serif; margin: 0; padding: 0; background-color: #f5f5f5; }";
  page += ".container { max-width: 600px; margin: 0 auto; padding: 20px; background-color: #fff; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); }";
  page += "h1 { text-align: center; margin: 30px 0; }";
  page += "h2 { margin-top: 30px; }";
  page += "label { display: block; margin: 10px 0; }";
  page += "input[type='text'], input[type='password'], select { width: 100%; padding: 10px; font-size: 16px; border: 1px solid #ccc; border-radius: 4px; }";
  page += "select { cursor: pointer; }";
  page += "input[type='submit'] { width: 100%; padding: 12px; font-size: 16px; color: #fff; background-color: #4CAF50; border: none; border-radius: 4px; cursor: pointer; }";
  page += "input[type='submit']:hover { background-color: #45a049; }";
  page += ".logo { max-width: 100px; display: block; margin: 0 auto; }";
  page += "</style>";
  page += "</head><body>";
  page += "<div class='container'>";
  page += "<img class='logo' src='logo.png' alt='Logo de marque'>";
  page += "<h1>Configuration SolarOptiMax</h1>";
  page += "<form action='/save' method='post'>";
  page += "<label>Numero de l'automate :</label>";
  page += "<input type='text' name='automation_id' value='" + String(preferences.getString("automation_id")) + "'>";
  page += "<h2>Configuration WiFi</h2>";
  page += "<label>Nom du WiFi :</label>";
  page += "<input type='text' name='ssid' value='" + String(preferences.getString("ssid")) + "'>";
  page += "<label>Mot de passe WiFi :</label>";
  page += "<input type='password' name='password' value='" + String(preferences.getString("password")) + "'>";
  page += "<h2>Configuration des relais</h2>";
  for (int i = 0; i < numRelays; i++) {
    page += "<h3>" + String(relayNames[i]) + "</h3>";
    page += "<label>Etat :</label>";
    page += "<select name='relay" + String(i + 1) + "Etat'>";
    page += "<option value='0' " + String(relayConfigs[i].etat == Inactif ? "selected" : "") + ">Inactif</option>";
    page += "<option value='1' " + String(relayConfigs[i].etat == ActifDiscontinu ? "selected" : "") + ">Actif et discontinu</option>";
    page += "<option value='2' " + String(relayConfigs[i].etat == ActifContinu ? "selected" : "") + ">Actif et continu</option>";
    page += "</select>";
    page += "<label>Duree de continuite (en minutes) :</label>";
    page += "<input type='text' name='relay" + String(i + 1) + "Duration' value='" + String(relayConfigs[i].duration) + "'>";
    page += "<label>Puissance :</label>";
    page += "<input type='text' name='relay" + String(i + 1) + "Power' value='" + relayConfigs[i].power + "'>";
  }
  page += "<input type='submit' value='Enregistrer'>";
  page += "</form></div></body></html>";
  return page;
}

void printConfigData() {
  Serial.println("===== Configuration de l'automate =====");
  Serial.println("Numéro de l'automate : " + String(automation_id)); 
  Serial.println("===== Configuration WiFi =====");
  Serial.println("SSID : " + preferences.getString("ssid"));
  Serial.println("Password : " + preferences.getString("password"));
  Serial.print("addresse IP de l'automate: ");
  Serial.println(WiFi.localIP());
  Serial.println("===== Configuration des relais =====");
  for (int i = 0; i < numRelays; i++) {
    Serial.println("Relay " + String(i + 1) + " - État : " + relayConfigs[i].etat + " - Puissance : " + relayConfigs[i].power + " - Durée de continuité : " + relayConfigs[i].duration + " minutes");
  }
}

void WifiConnect(const String& ssid, const String& password){
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("SolarOptiMax");
  lcd.setCursor(1, 2);
  lcd.print("CONNEXION AU WIFI");
  WiFi.mode(WIFI_STA);
  if(!password.equals(defaultPassword)){
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  else{
    WiFi.begin(ssid.c_str());
  }
  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
    delay(1000);
  }
  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("SolarOptiMax");
    lcd.setCursor(2, 2);
    lcd.print("CONNECTE AU WIFI!");
    delay(1000);
    Serial.print("CONNECTE AU WIFI!");
    WifiStatus = 1;
    restartcount = 0;
    preferences.putInt("restartcount", restartcount);
  } 
  else {
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("SolarOptiMax");
    lcd.setCursor(2, 2);
    lcd.print("CONNEXION ECHOUEE!");
    Serial.print("CONNEXION ECHOUEE!");
    delay(1000);
    restartcount+=1;
    preferences.putInt("restartcount", restartcount);
    if(restartcount==1 && loops==0){
      ESP.restart();
    }
    else{
      WifiStatus=0;
      restartcount=0;
      preferences.putInt("restartcount", restartcount);
    }
  }
}

// bool checkServer() {
//   HTTPClient http;
//   String serverUrl = serverBaseUrl + String(automation_id);

//   http.begin(serverUrl);

//   int httpCode = http.GET();
//   if (httpCode == HTTP_CODE_OK) {
//     String payload = http.getString();
//     http.end();

//     StaticJsonDocument<1024> doc;
//     DeserializationError error = deserializeJson(doc, payload);

//     if (!error) {
//       int automationId = doc["id"];
//       preferences.putInt("automation_id", automationId);

//       int buildingId = doc["building_id"];
//       preferences.putInt("building_id", buildingId);

//       int puissance = doc["puissance"];
//       preferences.putInt("puissance", puissance);

//       int nombrePano = doc["nombre_pano"];
//       preferences.putInt("nombre_pano", nombrePano);

//       const char* wifiId = doc["wifi_id"];
//       preferences.putString("ssid", wifiId);

//       const char* wifiPassword = doc["wifi_password"];
//       preferences.putString("password", wifiPassword);

//       preferences.end();

//       return true;
//     } else {
//       Serial.println("Erreur lors du parsing JSON.");
//     }
//   } else {
//     Serial.print("Erreur HTTP : ");
//     Serial.println(httpCode);
//   }

//   http.end();
//   return false;
// }


void firmwareUpdate(void) {
  WiFiClientSecure client;
  client.setCACert(rootCACertificate);
  t_httpUpdate_return ret = httpUpdate.update(client, URL_fw_Bin);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}
int FirmwareVersionCheck(void) {
  String payload;
  int httpCode;
  String fwurl = "";
  fwurl += URL_fw_Version;
  fwurl += "?";
  fwurl += String(rand());
  Serial.println(fwurl);
  WiFiClientSecure * client = new WiFiClientSecure;

  if (client) {
    client -> setCACert(rootCACertificate);
    HTTPClient https;
    if (https.begin( * client, fwurl)) { 
      Serial.print("[HTTPS] GET...\n");
      delay(100);
      httpCode = https.GET();
      delay(100);
      if (httpCode == HTTP_CODE_OK){
        payload = https.getString();
      } 
      else {
        Serial.print("error in downloading version file:");
        Serial.println(httpCode);
      }
      https.end();
    }
    delete client;
  } 
  if (httpCode == HTTP_CODE_OK)
  {
    payload.trim();
    if (payload.equals(FirmwareVer)) {
      Serial.printf("\nDevice already on latest firmware version:%s\n", FirmwareVer);
      return 0;
    } 
    else {
      Serial.println(payload);
      Serial.println("New firmware detected");
      return 1;
    }
  } 
  return 0;  
}

void sendEnergyData() {
  DynamicJsonDocument jsonDoc(20480);
  JsonArray dataArray = jsonDoc.to<JsonArray>();

  for (const auto& data : energyData) {
    JsonObject dataObj = dataArray.createNestedObject();
    struct tm* timeinfo = localtime(&data.timestamp);
    dataObj["pano_id"] = preferences.getString("automation_id").toInt();
    dataObj["tyear"] = timeinfo->tm_year + 1900;
    dataObj["tmonth"] = timeinfo->tm_mon + 1;
    dataObj["tday"] = timeinfo->tm_mday;
    dataObj["thour"] = timeinfo->tm_hour;
    dataObj["tmin"] = timeinfo->tm_min;
    dataObj["tsec"] = timeinfo->tm_sec;
    dataObj["acbuy_power"] = data.acbuy_power;
    dataObj["acbuy_energy"] = data.acbuy_energy;
    dataObj["acp_power"] = data.acp_power;
    dataObj["acp_energy"] = data.acp_energy;
    dataObj["ssr1"] = data.SSR1;
    dataObj["ssr2"] = data.SSR2;
    dataObj["ssr3"] = data.SSR3;
    dataObj["ssr4"] = data.SSR4;
    dataObj["var1"] = data.VAR1;
  }

  String jsonString;
  serializeJson(dataArray, jsonString);

  HTTPClient http;

  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.PUT(jsonString);

  if (httpResponseCode > 0) {
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error sending data. HTTP error code: ");
    Serial.println(httpResponseCode);
  }

  energyData.clear();
  http.end();
}

void loop() {
  ArduinoOTA.handle();
  // Serial.print("Let's GO");
  // printConfigData();
  // delay(1000);
  // ConfigClient();
  // printConfigClient();
  Serial.println(TrueWifiStatus);
  Serial.println(WifiStatus);
  if (WiFi.status() != WL_CONNECTED) {
    WifiStatus=0;
  }
  struct tm timeinfo;
  loops = 1;

  if(WifiStatus==1){
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }
    if (timeinfo.tm_hour != lastHour && TrueWifiStatus == 1 && WifiStatus==0 ) {
      WifiConnect(ssid, password);
    }
    unsigned long currentMillis = millis();
    if ((currentMillis - previousMillis) >= interval) {
      previousMillis = currentMillis;
      if (FirmwareVersionCheck()) {
        firmwareUpdate();
      }
    }
    // if (millis() % 10000 == 0) {
    //   if (checkServer()) {
    //     Serial.println("Mise à jour réussie !");
    //   } 
    //   else {
    //     Serial.println("Echec de la mise à jour...");
    //   }
    // }

    Serial.println(&timeinfo, "%H:%M:%S");

    if (timeinfo.tm_hour != lastHour) {
      pzems[0].resetEnergy();
      pzems[1].resetEnergy();
      lastHour = timeinfo.tm_hour;
    }
  }
  
  float acbuy_voltage = 0;
  float acbuy_current = 0;
  signed int acbuy_power = 0;    
  float acbuy_energy = 0;
  float acbuy_pf = 0;
  float acp_voltage = 0;
  float acp_current = 0;  
  signed int acp_power = 0;
  float acp_energy = 0;
  float acp_pf = 0;
  float acp_voltage_correction = 1.002;
  float acp_current_correction = 0.73;
  float acbuy_current_correction = 0.76;
  float acbuy_voltage_correction = 1.005;
  for (int i = 0; i < NUM_PZEMS; i++) {
    Serial.print("PZEM ");
    Serial.print(i);
    Serial.print(" - Address:");
    Serial.println(pzems[i].getAddress(), HEX);
    Serial.println("===================");

    if (i == 0) {
      acbuy_voltage= pzems[i].voltage();
      acbuy_current = pzems[i].current();
      acbuy_power = pzems[i].power();  
      acbuy_energy = pzems[i].energy();
      acbuy_pf = pzems[i].pf();
    } else if (i == 1) {
      acp_voltage= pzems[i].voltage();
      acp_current = pzems[i].current();
      acp_power = pzems[i].power();
      acp_energy = pzems[i].energy();
      acp_pf = pzems[i].pf();
    }
  }

  // //Coorection factor
  acp_current = acp_current*acp_current_correction;
  acbuy_current = acbuy_current*acbuy_current_correction;
  acp_voltage = acp_voltage*acp_voltage_correction;
  acbuy_voltage = acbuy_voltage*acbuy_voltage_correction;

  if (acp_current == acbuy_current - 0,01) {
    acbuy_current = acbuy_current - 0,01;
  }
  if (acp_power == acbuy_power - 1) {
    acbuy_power = acbuy_power - 1;
  } 
  Serial.println("-------------------");
  if ((acp_voltage > acbuy_voltage) && (acp_current > acbuy_current) && (acp_power > acbuy_power)) {
    if (((acp_pf >= acbuy_pf) || ((acbuy_power >= lastbuypower - 2) && (acbuy_power <= lastbuypower + 2))) &&
        ((((3.8 * acbuy_current) / acp_current) > acbuy_pf)) || (((lastlastbuypf >= 0.9 * acbuy_pf) && (lastlastbuypf <= 0.9 * acbuy_pf)) && (acbuy_pf >= 3 * lastbuypf))) {
        acbuy_power = -1 * acbuy_power;
        Serial.println("Injection");
    }
}
  lastbuypower = (int) acbuy_power;
  lastlastbuypf = lastbuypf;
  lastbuypf = 1,5*acbuy_pf;

  Serial.println("-------------------");

  time_t now = time(nullptr);
  for (int i = 0; i < numRelays; i++) {
    if (relayActivatedTime[i] != 0){
      Serial.println(now - relayActivatedTime[i]);
    }
    else{
      Serial.println("SSR " + String(i + 1) + " : OFF");
    }
    if (relayConfigs[i].etat == Inactif) {
      continue;
    } 
    else if (relayConfigs[i].etat == ActifDiscontinu) {
      if (acbuy_power < -700) {
        if (i == 0 && SSR1 == 0) {
          digitalWrite(relayPin[i], HIGH);
          SSR1 = 1;
          relayActivatedTime[i] = now;
        }
        if (i == 2 && acbuy_power < -1100 && SSR3 == 0) {
          digitalWrite(relayPin[i], HIGH);
          SSR3 = 1;
          relayActivatedTime[i] = now;
        }
      } else if ((acbuy_power < -400 && SSR1 == 0 ) || (acbuy_power < -800 && SSR1 == 1)) {
        if (i == 2 && SSR3 == 0) {
          digitalWrite(relayPin[i], HIGH);
          SSR3 = 1;
          relayActivatedTime[i] = now;
        }
      }
      
      if (SSR1 == 1 || SSR3 == 1) {
        if (now - relayActivatedTime[i] >= relayOffDelay) {
          if (acbuy_power > -200) {
            digitalWrite(relayPin[i], LOW);
            if (i == 0) {
              SSR1 = 0;
              relayActivatedTime[i] = 0;
            } 
            else if (i == 2) {
              SSR3 = 0;
              relayActivatedTime[i] = 0;
            }
          } 
          else if (acbuy_power > -400) {
            digitalWrite(relayPin[i], LOW);
            if (i == 0) {
              SSR1 = 0;
              relayActivatedTime[i] = 0;
            }
          }
        }
      }
    }
  }

  if (isnan(acbuy_power) || isnan(acp_power)){
    acbuy_power = -1;    
    acbuy_energy = -1;  
    acp_power = -1;
    acp_energy = -1;
  }
  else if (acbuy_power == 2147483647 || acp_power == 2147483647){
    acbuy_power = -2;    
    acbuy_energy = -2;  
    acp_power = -2;
    acp_energy = -2;
  }
  
  int acc_power = acp_power + acbuy_power;
  if (acc_power <= 0){
    acc_power = 0;
  }
  int state = 0;


  if (acc_power != 0 && acbuy_power < 0){
    state = (100*acbuy_power*(-1))/acp_power;
  }
  else if (acbuy_power>=0){
    state = (100*acp_power)/acc_power;
  }
  else{
    state = 100;
  }

  if(WifiStatus==1){
    EnergyData data = {
      mktime(&timeinfo),
      acbuy_power,
      acbuy_energy,
      acp_power,
      acp_energy,
      SSR1,
      SSR2,
      SSR3,
      SSR4,
      VAR1
    };
    energyData.push_back(data);

    static unsigned long lastSendTime = 0;
    unsigned long currentTime = millis();
    if (currentTime - lastSendTime >= dataInterval * 1000) {
      sendEnergyData();
      lastSendTime = currentTime;
    }
  }

  lcd.clear();
  if(acbuy_power == -2 || acp_power == -2 || acbuy_power == -1 || acp_power == -1){
    lcd.setCursor(4, 0);
    lcd.print("SolarOptiMax");
    lcd.setCursor(3, 2);
    if ((acbuy_power == -2 || acbuy_power == -1) && (acp_power == -2 || acp_power == -1)) {
      lcd.setCursor(1, 2);
      lcd.print("ERREUR: " + String(acbuy_power) + "/B" + String(acp_power) + "/P");
    }
    else if(acp_power == -2 || acp_power == -1){
      lcd.print("ERREUR :" + String((signed int)acp_power) + "/P");
    }
    else if(acbuy_power == -2 || acbuy_power == -1){
      lcd.print("ERREUR :" + String((signed int)acbuy_power) + "/B");
    }
    lcd.setCursor(1, 3);
    lcd.print("CONTACTEZ LE SAV!");
  }
  else{
    lcd.setCursor(0, 0);
    if (acbuy_power <0){
      lcd.print("P export: " + String((int)acbuy_power) + " W");
    }
    else{
      lcd.print("P import: " + String((int)acbuy_power) + " W");
    }
    lcd.setCursor(19,0);
    lcd.print(WifiStatus);
    lcd.setCursor(0, 1);
    lcd.print("P prod: " + String((int)acp_power) + " W");
    lcd.setCursor(0, 2);
    if (acbuy_power < 0){
      lcd.print("Surproduction: " + String((int)state) + " %");  
    }
    else{
      lcd.print("Auto-conso: " + String((int)state) + " %");
    }
    lcd.setCursor(0, 3);
    lcd.print("Conso: " + String((int)acc_power) + " W");
  }
  Serial.print("Voltage buy: ");     Serial.print(acbuy_voltage); Serial.println(" V");
  Serial.print("Voltage prod: ");     Serial.print(acp_voltage); Serial.println(" V");
  Serial.print("Current buy: ");       Serial.print(acbuy_current);     Serial.println(" A");
  Serial.print("pf buy: ");       Serial.print(acbuy_pf);     Serial.println(" ");
  Serial.print("Power buy: ");        Serial.print(acbuy_power);        Serial.println(" W");
  Serial.print("Current prod: ");       Serial.print(acp_current);     Serial.println(" A");
  Serial.print("pf prod: ");       Serial.print(acp_pf);     Serial.println("");
  Serial.print("Power prod: ");       Serial.print(acp_power);     Serial.println(" W");
  Serial.print("SSR1 :"); Serial.println(SSR1);
  Serial.println();
  delay(1000);
}

