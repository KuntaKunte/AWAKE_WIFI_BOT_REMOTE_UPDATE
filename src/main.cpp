#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Preferences.h>

// ====== USER CONFIGURATION ======
const char* ssid = "Jumbo 53";         // Replace with your SSID 
const char* password = "19831987"; // Replace with your Wi-Fi password

#define BOTtoken "8115605103:AAEcvmpCmvnZIjZdmDjDjTkHITY6InmHWV0"  // Replace with your Bot Token
#define CHAT_ID  "8580089820"                            // Replace with your chat ID

// ====== GLOBAL VARIABLES ======
unsigned long autoPingInterval;      // in ms
unsigned long telegramCheckInterval; // in ms
unsigned long lastPingTime = 0;
unsigned long lastTelegramCheck = 0;

Preferences prefs;
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOTtoken, secured_client);

// ====== FUNCTIONS ======

void loadSettings() {
  prefs.begin("keepalive", true);
  autoPingInterval = prefs.getULong("pingInt", 5UL * 60UL * 1000UL);
  telegramCheckInterval = prefs.getULong("checkInt", 10UL * 1000UL);
  prefs.end();
  Serial.println("[SETTINGS] Loaded:");
  Serial.println("  Auto-ping interval: " + String(autoPingInterval / 60000) + " min");
  Serial.println("  Telegram check interval: " + String(telegramCheckInterval / 1000) + " sec");
}

void saveSettings() {
  prefs.begin("keepalive", false);
  prefs.putULong("pingInt", autoPingInterval);
  prefs.putULong("checkInt", telegramCheckInterval);
  prefs.end();
  Serial.println("[SETTINGS] Saved to flash memory.");
}

void connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connecting to " + String(ssid) + " ...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
    } else {
      Serial.println("\n[WiFi] Connection failed.");
    }
  }
}

void sendKeepAlive() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    Serial.println("[PING] Sending keep-alive request...");
    http.begin("http://clients3.google.com/generate_204");
    int httpCode = http.GET();
    http.end();

    if (httpCode > 0) {
      Serial.println("[PING] Success, code: " + String(httpCode));
    } else {
      Serial.println("[PING] Failed, code: " + String(httpCode));
    }
  } else {
    Serial.println("[PING] Wi-Fi not connected, skipping ping.");
  }
}

void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  while (numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
      String text = bot.messages[i].text;
      Serial.println("[BOT] Command: " + text);

      if (text == "/ping") {
        sendKeepAlive();
        bot.sendMessage(CHAT_ID, "Ping sent ✅", "");
      } 
      else if (text == "/status") {
        String msg = "ESP32 KeepAlive running\n";
        msg += "WiFi: " + String(ssid) + "\n";
        msg += "IP: " + WiFi.localIP().toString() + "\n";
        msg += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
        msg += "Auto-ping every: " + String(autoPingInterval / 60000) + " min\n";
        msg += "Check Telegram every: " + String(telegramCheckInterval / 1000) + " sec\n";
        msg += "Uptime: " + String(millis() / 1000 / 60) + " min\n";
        bot.sendMessage(CHAT_ID, msg, "");
      } 
      else if (text.startsWith("/setping")) {
        int newVal = text.substring(9).toInt();
        if (newVal >= 1 && newVal <= 60) {
          autoPingInterval = newVal * 60UL * 1000UL;
          saveSettings();
          bot.sendMessage(CHAT_ID, "✅ Auto-ping interval set to " + String(newVal) + " minutes (saved).", "");
          Serial.println("[BOT] Auto-ping interval updated and saved.");
        } else {
          bot.sendMessage(CHAT_ID, "⚠️ Invalid interval. Use 1–60 minutes.", "");
        }
      }
      else if (text.startsWith("/setcheck")) {
        int newVal = text.substring(10).toInt();
        if (newVal >= 5 && newVal <= 60) {
          telegramCheckInterval = newVal * 1000UL;
          saveSettings();
          bot.sendMessage(CHAT_ID, "✅ Telegram check interval set to " + String(newVal) + " seconds (saved).", "");
          Serial.println("[BOT] Telegram check interval updated and saved.");
        } else {
          bot.sendMessage(CHAT_ID, "⚠️ Invalid interval. Use 5–60 seconds.", "");
        }
      }
      else if (text == "/help") {
        bot.sendMessage(CHAT_ID,
          "🤖 Commands:\n"
          "/ping - Send immediate keep-alive\n"
          "/status - Show Wi-Fi & timing info\n"
          "/setping <minutes> - Set auto-ping interval\n"
          "/setcheck <seconds> - Set Telegram check interval\n"
          "/help - Show this list", "");
      } 
      else {
        bot.sendMessage(CHAT_ID, "Unknown command. Type /help", "");
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 KeepAlive Bot Starting ---");

  secured_client.setInsecure();
  loadSettings();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(CHAT_ID, "ESP32 KeepAlive Bot Online ✅\n(Intervals loaded from memory)", "");
  }
}

// ====== LOOP ======
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  if (millis() - lastPingTime >= autoPingInterval) {
    lastPingTime = millis();
    sendKeepAlive();
  }

  if (millis() - lastTelegramCheck >= telegramCheckInterval) {
    lastTelegramCheck = millis();
    handleTelegramMessages();
  }

  delay(500);
}
