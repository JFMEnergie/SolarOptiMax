#include <ArduinoOTA.h>
#include <ArduinoHttpClient.h>
#include <Update.h>

const char* githubUpdateURL = "https://raw.githubusercontent.com/VOTRE_NOM_UTILISATEUR/VOTRE_REPO/NOM_DE_LA_BRANCHE/main.ino";
const char* ssid = "dlinkHomeMounicou";

void setup() {
  Serial.begin(9600);
  WiFi.begin(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  performUpdate();
}

void performUpdate() {
  WiFiClient client;

  Serial.print("Téléchargement du nouveau code...");
  if (client.connect("raw.githubusercontent.com", 443)) {
    client.print(String("GET ") + githubUpdateURL + " HTTP/1.1\r\n" +
                 "Host: raw.githubusercontent.com\r\n" +
                 "User-Agent: ESP8266\r\n" +
                 "Connection: close\r\n\r\n");
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") {
        break;
      }
    }
    uint8_t buffer[256];
    int firmwareSize = 0;
    while (client.available()) {
      int size = client.read(buffer, sizeof(buffer));
      if (size > 0) {
        Update.write(buffer, size);
        firmwareSize += size;
      }
    }
    client.stop();
    Serial.println("OK.");
    Serial.print("Taille du firmware : ");
    Serial.println(firmwareSize);

    Serial.println("Démarrage de la mise à jour...");
    if (Update.end(true)) {
      Serial.println("Succès ! Redémarrage...");
      ESP.restart();
    } else {
      Serial.println("Échec !");
    }
  } else {
    Serial.println("Échec de la connexion au serveur.");
  }
}

void checkForUpdates() {
  Serial.println("Vérification des mises à jour...");
  HttpClient client;
  client.get(githubUpdateURL);
  int statusCode = client.responseStatusCode();

  if (statusCode == 200) {
    Serial.println("Mise à jour disponible !");
    performUpdate();
  } else {
    Serial.println("Pas de mise à jour disponible.");
  }
}
