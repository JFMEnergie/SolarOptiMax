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

  #define PZEM_RX_PIN 17
  #define PZEM_TX_PIN 16
  #define PZEM_SERIAL Serial2
  PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
  #define NUM_PZEMS 2
  PZEM004Tv30 pzems[NUM_PZEMS];



  char* ssid ="dlinkHomeMounicou";
  unsigned long timeout = millis() + 10000;

  String FirmwareVer = {
    "S00.00"
  };
  #define URL_fw_Version "https://raw.githubusercontent.com/JFMEnergie/SolarOptiMax/Update/Software/Versions/Test/Releases/fw.ino/bin_version.txt"
  #define URL_fw_Bin "https://raw.githubusercontent.com/JFMEnergie/SolarOptiMax/Update/Software/Versions/Test/Releases/fw.ino/fw.bin"

  void firmwareUpdate();
  int FirmwareVersionCheck();
  unsigned long previousMillis = 0;
  const long interval = 10*1000;

  float acbuy_voltage, acbuy_current, acbuy_power;
  float acp_voltage, acp_current, acp_power;
  float acp_voltage_correction = 1.0;
  float acp_current_correction = 1.0;
  float acbuy_current_correction = 1.0;
  float acbuy_voltage_correction = 1.0;

  float total_acbuy_voltage_correction = 0.0;
  float total_acbuy_current_correction = 0.0;
  float total_acp_voltage_correction = 0.0;
  float total_acp_current_correction = 0.0;

  unsigned long lastCalibrationTime = 0;
  unsigned long calibrationInterval = 3000; 
  int calibration_count = 0;

  AsyncWebServer server(80);
  Preferences preferences;

  void setup() {
    Serial.begin(115200);
    Serial2.begin(9600);
    Serial.println("Bonjour, commencons la configuration :");
    WifiConnect();
    Serial.println("Avez-vous implémenter l'adresse du PZEM - LY (oui/non) :");
    while (!Serial.available()) {
        // Wait for user input
      }
    String input = Serial.readStringUntil('\n');
    if (input.startsWith("non")) {
      String values = input.substring(3);
      static uint8_t addr = 0x10;
      Serial.println("Veuillez débrancher le PZEM - PV et laisser brancher le PZEM - LY. Ecrivez 'OK LY' dans le moniteur série une fois que cela est fait!");
      while (!Serial.available()) {
        // Wait for user input
      }
      input = Serial.readStringUntil('\n');
      if (input.startsWith("OK LY")) {
        Serial.print("Ancienne addresse:   0x");
        Serial.println(pzem.readAddress(), HEX);
        Serial.print("Addresse attendue: 0x");
        Serial.println(addr, HEX);
        if(!pzem.setAddress(addr)) 
        {
          Serial.println("Erreur lors de la configuration de l'adresse");
        } 
        else {
          Serial.print("Nouvelle addresse:    0x");
          Serial.println(pzem.readAddress(), HEX);
          Serial.println();
        }
        Serial.print("L'adresse du PZEM - LY est : 0x");
        Serial.println(pzem.readAddress(), HEX);
      }
      Serial.println("Merci de débrancher le PZEM - LY et de brancher le PZEM - PV. Le SOM va rédemarrer");
      delay(10000);
      ESP.restart();
    }
    Serial.println("Avez-vous implémenter l'adresse du PZEM - PV (oui/non) :");
    while (!Serial.available()) {
        // Wait for user input
    }
    input = Serial.readStringUntil('\n');
    if (input.startsWith("non")) {
      String values = input.substring(3);
      static uint8_t addr = 0x11;
      Serial.println("Veuillez raccorder le PZEM - PV et débrancher le PZEM - LY. Ecrivez 'OK PV' dans le moniteur série une fois que cela est fait!");
      while (!Serial.available()) {
        // Wait for user input
      }
      input = Serial.readStringUntil('\n');
      if (input.startsWith("OK PV")) {
        Serial.print("Ancienne addresse:   0x");
        Serial.println(pzem.readAddress(), HEX);
        Serial.print("Addresse attendue: 0x");
        Serial.println(addr, HEX);
        if(!pzem.setAddress(addr)) 
        {
          Serial.println("Erreur lors de la configuration de l'adresse");
        }
        else {
          // Print out the new custom address
          Serial.print("Nouvelle addresse:    0x");
          Serial.println(pzem.readAddress(), HEX);
          Serial.println();
        }
        Serial.print("L'adresse du PZEM - PV est : 0x");
        Serial.println(pzem.readAddress(), HEX);
      }
      Serial.println("Merci de rebrancher le PZEM - LY. Le SOM va redémarrer.");
      delay(10000);
      ESP.restart();
    }
    preferences.begin("calibration", false);
    preferences.clear();
    acp_voltage_correction = preferences.getFloat("acp_voltage_correction", 1.0);
    acp_current_correction = preferences.getFloat("acp_current_correction", 1.0);
    acbuy_voltage_correction = preferences.getFloat("acbuy_voltage_correction", 1.0);
    acbuy_current_correction = preferences.getFloat("acbuy_current_correction", 1.0);
    preferences.end();
    for (int i = 0; i < NUM_PZEMS; i++) {
      pzems[i] = PZEM004Tv30(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN, 0x10 + i);
      Serial.print("PZEM ");
      Serial.print(i);
      Serial.print(" - Address:");
      Serial.println(pzems[i].getAddress(), HEX);
      Serial.println("===================");
    }
    Serial.println("Merci de renseigner l'usage du SOM (client ou test):");
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
      ArduinoOTA.setPort(3232);
      ArduinoOTA.setHostname("SolarOptiMax");
      ArduinoOTA.setPassword("solaroptimax");
      ArduinoOTA.begin();
      Serial.println("Ready");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
  }

  void WifiConnect(){
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid);
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      delay(1000);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("CONNECTE AU WIFI!");
      delay(1000);
    } 
    else {
      Serial.println("CONNEXION ECHOUEE!");
      delay(1000);
    }
  }

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

  void loop() {
    delay(1000);
    if (calibration_count <=20){
      for (int i = 0; i < NUM_PZEMS; i++) {
        if (i == 0) {
          acbuy_voltage= pzems[i].voltage();
          acbuy_current = pzems[i].current();
          acbuy_power = pzems[i].power();
        } else if (i == 1) {
          acp_voltage= pzems[i].voltage();
          acp_current = pzems[i].current();
          acp_power = pzems[i].power();
        }
      }
      acp_current = acp_current*acp_current_correction;
      acbuy_current = acbuy_current*acbuy_current_correction;
      acp_voltage = acp_voltage*acp_voltage_correction;
      acbuy_voltage = acbuy_voltage*acbuy_voltage_correction;
      Serial.print("Voltage buy: ");     Serial.print(acbuy_voltage); Serial.println(" V");
      Serial.print("Current buy: ");       Serial.print(acbuy_current);     Serial.println(" A");
      Serial.print("Power buy: ");        Serial.print(acbuy_power);        Serial.println(" W");
      Serial.print("Voltage prod: ");     Serial.print(acp_voltage); Serial.println(" V");
      Serial.print("Current prod: ");       Serial.print(acp_current);     Serial.println(" A");
      Serial.print("Power prod: ");       Serial.print(acp_power);     Serial.println(" W");
      Serial.println();
      if (millis() - lastCalibrationTime >= calibrationInterval) {
        autoCalibrate();
        lastCalibrationTime = millis();
      }
    }
    unsigned long currentMillis = millis();
    if ((currentMillis - previousMillis) >= interval && calibration_count > 10) {
      previousMillis = currentMillis;
      if (FirmwareVersionCheck()) {
        firmwareUpdate();
      }
    }
  }

  void autoCalibrate() {
    for (int i = 0; i < NUM_PZEMS; i++) {
      Serial.print("Renseignez les valeurs de tension puis de courant (exemple: 324.5/4.5) pour la ");
      calibration_count++;
      if (i==0){
        Serial.println(" consommation :");
        while (!Serial.available()) {
          // Wait for user input
        }
        String input = Serial.readStringUntil('\n');
        int separatorIndex = input.indexOf('/');
        if (separatorIndex != -1) {
          float realVoltage = input.substring(0, separatorIndex).toFloat();
          float realCurrent = input.substring(separatorIndex + 1).toFloat();
          if (realVoltage > 0 && realCurrent > 0) {
            delay(1000);
            acbuy_voltage_correction = realVoltage / pzems[i].voltage();
            acbuy_current_correction = realCurrent / pzems[i].current();
            Serial.println("Calibration réussit.");

            total_acbuy_voltage_correction += acbuy_voltage_correction;
            total_acbuy_current_correction += acbuy_current_correction;
            float average_acbuy_voltage_correction = total_acbuy_voltage_correction / calibration_count;
            float average_acbuy_current_correction = total_acbuy_current_correction / calibration_count;

            Serial.println("Nouveau facteur de calibration tension: " + String(average_acbuy_voltage_correction));
            Serial.println("Nouveau facteur de calibration courant: " + String(average_acbuy_current_correction));
            Serial.println("pour la consommation.");

            preferences.begin("calibration", false);
            preferences.putFloat("acbuy_voltage_correction", average_acbuy_voltage_correction);
            preferences.putFloat("acbuy_current_correction", average_acbuy_current_correction);
            preferences.end();
          } 
        }
      }
      if (i==1){
        Serial.println(" production :");
        while (!Serial.available()) {
          // Wait for user input
        }
        String input = Serial.readStringUntil('\n');
        int separatorIndex = input.indexOf('/');
        if (separatorIndex != -1) {
          delay(1000);
          float realVoltage = input.substring(0, separatorIndex).toFloat();
          float realCurrent = input.substring(separatorIndex + 1).toFloat();
          if (realVoltage > 0 && realCurrent > 0) {
            acp_voltage_correction = realVoltage / pzems[i].voltage();
            acp_current_correction = realCurrent / pzems[i].current();
            Serial.println("Calibration réussit.");

            total_acp_voltage_correction += acp_voltage_correction;
            total_acp_current_correction += acp_current_correction;
            float average_acp_voltage_correction = total_acp_voltage_correction / calibration_count;
            float average_acp_current_correction = total_acp_current_correction / calibration_count;

            
            Serial.println("Nouveau facteur de calibration tension: " + String(average_acp_voltage_correction));
            Serial.println("Nouveau facteur de calibration courant: " + String(average_acp_current_correction));
            Serial.println("pour la production.");

            preferences.begin("calibration", false);
            preferences.putFloat("acp_voltage_correction", average_acp_voltage_correction);
            preferences.putFloat("acp_current_correction", average_acp_current_correction);
            preferences.end();
          } 
        }
      }
    }
  }