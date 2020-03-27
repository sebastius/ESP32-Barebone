// Basically the bare bone structure from the Decennium Badge code, so just the Wifi setup, the MQTT, the portal, the OTA stuff.
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTP_Method.h>
#include <esp_task_wdt.h>

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

const char*  mqtt_server = "revspace.nl";
String       my_hostname = "otadevice-";

const int    button = 0;

WiFiClient   espClient;
PubSubClient mqtt(espClient);

void loop() {
  yield();
}


//////// Over-The-Air update

void setup_ota() {
  String ota = pwgen();
  Serial.printf("OTA-wachtwoord is %s\n", ota.c_str());

  ArduinoOTA.setHostname(my_hostname.c_str());
  ArduinoOTA.setPassword(ota.c_str());
  ArduinoOTA.onStart([]() {
    //doe hier dingen
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    float p = (float) progress / total;
    //doe hier iets mee
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //animatie
  });
  ArduinoOTA.onEnd([]() {
    //animatie
  });

  ArduinoOTA.begin();
}

void check_button() {
  if (digitalRead(button) == HIGH) return;
  unsigned long start = millis();
  while (digitalRead(button) == LOW) {
    if (millis() - start > 1000) setup_wifi_portal();
  }
}

//////// end-user configurable

String read(const char* fn) {
  File f = SPIFFS.open(fn, "r");
  String r = f.readString();
  f.close();
  return r;
}
void store(const char* fn, String content) {
  File f = SPIFFS.open(fn, "w");
  f.print(content);
  f.close();
}

String pwgen() {
  const char* filename   = "/ota-password";
  const char* passchars  = "ABCEFGHJKLMNPRSTUXYZabcdefhkmnorstvxz23456789-#@%^<>";

  String password = read(filename);
  
  if (password.length() == 0) {
    for (int i = 0; i < 16; i++) {
       password.concat( passchars[random(strlen(passchars))] );
    }
    store(filename, password);
  }

  return password;
}



String html_entities(String raw) {
  String r;
  for (int i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c >= '!' && c <= 'z' && c != '&' && c != '<' && c != '>') {
      // printable ascii minus html and {}
      r += c;
    } else {
      r += Sprintf("&#%d;", raw.charAt(i));
    }
  }
  return r;
}

void setup_wifi_portal() {
  static WebServer http(80);
  static DNSServer dns;
  static int num_networks = -1;
  String wpa = read("/wifi-portal-wpa");
  String ota = pwgen();

  WiFi.disconnect();

  if (wpa.length() && ota.length()) {
    WiFi.softAP(my_hostname.c_str(), ota.c_str());
  } else {
    WiFi.softAP(my_hostname.c_str());
  }
  delay(500);
  dns.setTTL(0);
  dns.start(53, "*", WiFi.softAPIP());
  setup_ota();

  Serial.println(WiFi.softAPIP().toString());

  http.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html>\n<meta charset=UTF-8>"
      "<title>{hostname}</title>"
      "<form action=/restart method=post>"
        "Hallo, ik ben {hostname}."
        "<p>Huidig ingestelde SSID: {ssid}<br>"
        "<input type=submit value='Opnieuw starten'>"
      "</form>"
      "<hr>"
      "<h2>Configureren</h2>"
      "<form method=post>"
        "SSID: <select name=ssid onchange=\"document.getElementsByName('password')[0].value=''\">{options}</select> "
        "<a href=/rescan onclick=\"this.innerHTML='scant...';\">opnieuw scannen</a>"
        "</select><br>Wifi WEP/WPA-wachtwoord: <input name=password value='{password}'><br>"
        "<p>Mijn eigen OTA/WPA-wachtwoord: <input name=ota value='{ota}' minlength=8 required> (8+ tekens, en je wilt deze waarschijnlijk *nu* ergens opslaan)<br>"
        "<label><input type=checkbox name=portalpw value=yes{portalwpa}> &uarr;Wachtwoord&uarr; vereisen voor deze wifi-configuratieportaal</label>"
        "<p><label><input type=radio name=retry value=no{retry-no}> Wifi-configuratieportaal starten als verbinding met wifi faalt.</label><br>"
        "<label><input type=radio name=retry value=yes{retry-yes}> Blijven proberen te verbinden met wifi (hou Flash ingedrukt om hier terug te komen)</label><br>"
        "<p><input type=submit value=Opslaan>"
      "</form>";

    String current = read("/wifi-ssid");
    String pw = read("/wifi-password");
    
    html.replace("{hostname}",  my_hostname);
    html.replace("{ssid}",      current.length() ? html_entities(current) : "(not set)");
    html.replace("{portalwpa}", read("/wifi-portal-wpa").length() ? " checked" : "");
    html.replace("{ota}",       html_entities(pwgen()));
    
    bool r = read("/wifi-retry").length();
    html.replace("{retry-yes}", r ? " checked" : "");
    html.replace("{retry-no}",  r ? "" : " checked");
    
    String options;
    if (num_networks < 0) num_networks = WiFi.scanNetworks();
    bool found = false;
    for (int i = 0; i < num_networks; i++) {
      String opt = "<option value='{ssid}'{sel}>{ssid} {lock} {1x}</option>";
      String ssid = WiFi.SSID(i);
      wifi_auth_mode_t mode = WiFi.encryptionType(i);

      opt.replace("{sel}",  ssid == current && !(found++) ? " selected" : "");
      opt.replace("{ssid}", html_entities(ssid));
      opt.replace("{lock}", mode != WIFI_AUTH_OPEN ? "&#x1f512;" : "");
      opt.replace("{1x}",   mode == WIFI_AUTH_WPA2_ENTERPRISE ? "(werkt niet: 802.1x wordt niet ondersteund)" : "");
      options += opt;
    }
    html.replace("{password}", found && pw.length() ? "##**##**##**" : "");
    html.replace("{options}",  options);
    http.send(200, "text/html", html);
  });
  
  http.on("/", HTTP_POST, []() {
    String pw = http.arg("password");
    if (pw != "##**##**##**")
        store("/wifi-password", pw);
    store("/wifi-ssid",         http.arg("ssid"));
    store("/wifi-retry",        http.arg("retry") == "yes" ? "x" : "");
    store("/wifi-portal-wpa",   http.arg("portalpw") == "yes" ? "x" : "");
    store("/ota-password",      http.arg("ota"));
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "ok");
  });
  
  http.on("/restart", HTTP_POST, []() {
    http.send(200, "text/plain", "Doei!");
   //animatie?
    ESP.restart();
  });

  http.on("/rescan", HTTP_GET, []() {
    http.sendHeader("Location", "/");
    http.send(302, "text/plain", "wait for it...");
    num_networks = WiFi.scanNetworks();
  });

  http.onNotFound([]() {
    http.sendHeader("Location", "http://" + my_hostname + "/");
    http.send(302, "text/plain", "hoi");
  });
  
  http.begin();
 
  for (;;) {
 
    http.handleClient();
    dns.processNextRequest();
    ArduinoOTA.handle();
    esp_task_wdt_reset();
    vTaskDelay(1);
  }
}

//////// Wifi + mqtt client

void setup_wifi() {
  String ssid = read("/wifi-ssid");
  String pw = read("/wifi-password");
  if (ssid.length() == 0) {
    Serial.println("First contact!\n");
    setup_wifi_portal();
  }
  Serial.printf("Verbinden met %s...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), pw.c_str());
  setup_ota();
  wait_wifi();
}

void wait_wifi() {
  int attempts = 0;
  String r = read("/wifi-retry");
  bool retry = r.length();
  bool do_yay = false;
  while (WiFi.status() != WL_CONNECTED) {
    if (attempts++ > 2) {
      do_yay = true;
      //yay
      check_button();
      //dingen?
      check_button();
      esp_task_wdt_reset();
      vTaskDelay(1);
    } else {
      delay(1000);
      check_button();
    }
    if (attempts > 30 && !retry) {
      Serial.println("Ik geef het op.");
      setup_wifi_portal();
    }
    Serial.println(attempts);
  }

  if (do_yay) {
    // yay
  }
  randomSeed(micros());  // ?
  Serial.printf("\nIP-adres: %s\n", WiFi.localIP().toString().c_str());
}

void callback(char* topic, byte* message, unsigned int length) {
  String t = topic;
  //t.replace("hoera10jaar/", "");
}

void reconnect_mqtt() {
  // Als de wifi weg is, blijft dit hangen en grijpt de watchdog in.
  // WiFi.status() blijft echter op WL_CONNECTED dus slimmere afhandeling is lastig.

  while (!mqtt.connected()) {
    Serial.print("Verbinden met MQTT-server...");

    if (mqtt.connect(my_hostname.c_str())) {
      Serial.println("verbonden");
      //mqtt.subscribe("hoera10jaar/+");
    } else {
      Serial.printf("mislukt, rc=%d\n", mqtt.state());
      //fail
    }
  }
}

//////// Main

void setup() {
  
  pinMode(button, INPUT);

  Serial.begin(115200);
  Serial.println("o hai");
  my_hostname += Sprintf("%12" PRIx64, ESP.getEfuseMac());
  Serial.println(my_hostname);

  xTaskCreatePinnedToCore(
    network,      /* Function to implement the task */
    "network",    /* Name of the task */
    4096,         /* Stack size in words */
    NULL,         /* Task input parameter */
    3,            /* Priority of the task */
    NULL,         /* Task handle. */
    0             /* Core where the task should run */
  );
}

void network(void * pvParameters) {
  esp_task_wdt_init(30 /* seconds */, true);
  esp_err_t err = esp_task_wdt_add(NULL);
  Serial.println(err == ESP_OK ? "Watchdog ok" : "Watchdog faal");

  SPIFFS.begin(true);
  setup_wifi();
  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(callback);

  while (1) {
    if (!mqtt.connected()) reconnect_mqtt();
    //hier animatie?

    mqtt.loop();
    ArduinoOTA.handle();
    check_button();

    esp_task_wdt_reset();
    vTaskDelay(1);
  }
}
