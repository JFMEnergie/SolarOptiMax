#include <Update.h>
#include <ESP32httpUpdate.h>

#define GITHUB_USER "YourGitHubUsername"
#define GITHUB_REPO "YourRepositoryName"
#define GITHUB_RELEASE_TAG "test" // Replace with the latest release tag on GitHub

void setup() {
  Serial.begin(9600);

  Serial.println("HELLO WORLD");
  if (WiFiStatus == 1) {
    if (AutoUpdate.updateFromGitHub(GITHUB_USER, GITHUB_REPO, GITHUB_RELEASE_TAG)) {
      ESP.restart();
    } else {
      Serial.println("Update failed.");
    }
  }
}
