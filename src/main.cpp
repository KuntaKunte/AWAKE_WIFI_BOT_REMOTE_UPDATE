#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Preferences.h>
#include <time.h>

// ====== USER CONFIGURATION ======
// WiFi credentials for network connection
const char* ssid = " ";          // Wi-Fi SSID
const char* password = " ";      // Wi-Fi password

// Telegram bot credentials
#define BOTtoken " "  // Bot token from BotFather
#define CHAT_ID " "  // Your Telegram chat ID for receiving messages

// ====== GLOBAL VARIABLES ======
// Timing intervals (loaded from flash memory)
unsigned long autoPingInterval;     // How often to send keep-alive pings
unsigned long telegramCheckInterval; // How often to check for new Telegram messages

// Timestamps for interval tracking
unsigned long lastPingTime = 0;      // Last time a ping was sent
unsigned long lastTelegramCheck = 0; // Last time Telegram was checked
unsigned long lastHeapCheck = 0;     // Last time memory was checked

// ESP32 preferences and communication objects
Preferences prefs;                   // Flash memory storage for settings
WiFiClientSecure secured_client;     // Secure WiFi client for HTTPS
UniversalTelegramBot bot(BOTtoken, secured_client);  // Telegram bot instance

// Message tracking
unsigned long last_update_id = 0;    // Tracks the latest Telegram message ID

// ====== FUNCTIONS ======

// --- TIME SYNCHRONIZATION ---
// Syncs the ESP32's internal clock with NTP (Network Time Protocol) servers
void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("[TIME] Syncing NTP time...");
  time_t now = time(nullptr);
  // Wait until valid time is received (Unix timestamp > 100000)
  while (now < 100000) {
    delay(200);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" done ✅");
}

// --- SETTINGS MANAGEMENT ---
// Loads user-configured intervals from ESP32 flash memory
// Uses default values if nothing is saved yet
void loadSettings() {
  prefs.begin("keepalive", true);
  autoPingInterval = prefs.getULong("pingInt", 5UL * 60UL * 1000UL);      // Default: 5 min
  telegramCheckInterval = prefs.getULong("checkInt", 10UL * 1000UL);      // Default: 10 sec
  prefs.end();
  Serial.println("[SETTINGS] Loaded:");
  Serial.println("  Auto-ping interval: " + String(autoPingInterval / 60000) + " min");
  Serial.println("  Telegram check interval: " + String(telegramCheckInterval / 1000) + " sec");
}

// Saves the current interval settings to ESP32 flash memory for persistence
void saveSettings() {
  prefs.begin("keepalive", false);
  prefs.putULong("pingInt", autoPingInterval);
  prefs.putULong("checkInt", telegramCheckInterval);
  prefs.end();
  Serial.println("[SETTINGS] Saved to flash memory.");
}

// --- WIFI CONNECTION ---
// Establishes WiFi connection if not already connected
// Times out after 20 seconds if unsuccessful
void connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Connecting to " + String(ssid) + " ...");
    WiFi.mode(WIFI_STA);  // Set to Station mode (client)
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();

    // Wait for connection with 20-second timeout
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

// --- KEEP-ALIVE PING ---
// Sends a lightweight HTTP request to Google to keep the connection alive
// Prevents network timeouts on dormant connections
void sendKeepAlive() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    Serial.println("[PING] Sending keep-alive request...");
    http.begin("http://clients3.google.com/generate_204");  // Lightweight 204 response
    http.setTimeout(5000);  // 5-second timeout
    int httpCode = http.GET();
    http.end();

    // Log the result
    if (httpCode > 0) {
      Serial.println("[PING] Success, code: " + String(httpCode));
    } else {
      Serial.println("[PING] Failed, code: " + String(httpCode));
    }
  } else {
    Serial.println("[PING] Wi-Fi not connected, skipping ping.");
  }
}

// --- TELEGRAM MESSAGE HANDLER ---
// Checks for new Telegram messages and processes commands
// Implements auto-resync safeguard to recover from missed updates
void handleTelegramMessages() {
  Serial.println("[BOT] Checking Telegram...");

  // --- AUTO-RESYNC SAFEGUARD ---
  // If no messages received for ~10 polls, reset offset to prevent falling behind
  static int emptyCount = 0;
  if (emptyCount >= 10) {
    Serial.println("[BOT] No messages for a while → resetting Telegram offset");
    bot.last_message_received = 0;  // Force resync with Telegram server
    emptyCount = 0;
  }

  // Fetch new messages from Telegram API
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  // --- AUTO-RECOVERY FOR MISSED UPDATES ---
  // If no messages but offset is high, reset to 0 and resync
  if (numNewMessages == 0 && bot.last_message_received > 1000) {
    if (numNewMessages == 0)
      emptyCount++;
    else
      emptyCount = 0;

    Serial.println("[BOT] No new messages. Auto-resyncing offset...");
    bot.last_message_received = 0;
    delay(1000);
    numNewMessages = bot.getUpdates(0);  // Fetch from start
  }

  // --- PROCESS RECEIVED MESSAGES ---
  // Iterate through each new message and execute corresponding commands
  for (int i = 0; i < numNewMessages; i++) {
    String text = bot.messages[i].text;
    Serial.println("[BOT] Command: " + text);
    last_update_id = bot.messages[i].update_id;  // Track latest message ID

    // Command: /ping - Send immediate keep-alive
    if (text == "/ping") {
      sendKeepAlive();
      bot.sendMessage(CHAT_ID, "Ping sent ✅", "");
    } 
    // Command: /status - Display system information
    else if (text == "/status") {
      String msg = "ESP32 KeepAlive running\n";
      msg += "WiFi: " + String(ssid) + "\n";
      msg += "IP: " + WiFi.localIP().toString() + "\n";
      msg += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
      msg += "Auto-ping every: " + String(autoPingInterval / 60000) + " min\n";
      msg += "Check Telegram every: " + String(telegramCheckInterval / 1000) + " sec\n";
      msg += "Free Heap: " + String(ESP.getFreeHeap()) + "\n";
      msg += "Uptime: " + String(millis() / 1000 / 60) + " min\n";
      bot.sendMessage(CHAT_ID, msg, "");
    } 
    // Command: /setping - Change auto-ping interval (1-60 minutes)
    else if (text.startsWith("/setping")) {
      int newVal = text.substring(9).toInt();
      if (newVal >= 1 && newVal <= 60) {
        autoPingInterval = newVal * 60UL * 1000UL;
        saveSettings();
        bot.sendMessage(CHAT_ID, "✅ Auto-ping interval set to " + String(newVal) + " minutes (saved).", "");
      } else {
        bot.sendMessage(CHAT_ID, "⚠️ Invalid interval. Use 1–60 minutes.", "");
      }
    }
    // Command: /setcheck - Change Telegram check interval (5-60 seconds)
    else if (text.startsWith("/setcheck")) {
      int newVal = text.substring(10).toInt();
      if (newVal >= 5 && newVal <= 60) {
        telegramCheckInterval = newVal * 1000UL;
        saveSettings();
        bot.sendMessage(CHAT_ID, "✅ Telegram check interval set to " + String(newVal) + " seconds (saved).", "");
      } else {
        bot.sendMessage(CHAT_ID, "⚠️ Invalid interval. Use 5–60 seconds.", "");
      }
    }
    // Command: /help - Display available commands
    else if (text == "/help") {
      bot.sendMessage(CHAT_ID,
        "🤖 Commands:\n"
        "/ping - Send immediate keep-alive\n"
        "/status - Show Wi-Fi & timing info\n"
        "/setping <minutes> - Set auto-ping interval\n"
        "/setcheck <seconds> - Set Telegram check interval\n"
        "/help - Show this list", "");
    } 
    // Unknown command response
    else {
      bot.sendMessage(CHAT_ID, "Unknown command. Type /help", "");
    }
  }

  // Update last processed message ID
  bot.last_message_received = last_update_id;
  Serial.println("[BOT] Updates processed ✅");
}

// ====== SETUP ======
// Runs once on startup - initializes all systems
void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 KeepAlive Bot Starting ---");

  // Initialize secure WiFi client (disable cert check for simplicity)
  secured_client.setInsecure();
  
  // Load stored settings from flash memory
  loadSettings();
  
  // Establish WiFi connection
  connectWiFi();
  
  // Sync internal clock with NTP servers
  syncTime();

  // Send startup notification to Telegram
  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(CHAT_ID, "ESP32 KeepAlive Bot Online ✅\n(Intervals loaded from memory)", "");
  }
}

// ====== LOOP ======
// Main program loop - runs continuously
void loop() {
  // --- WIFI MONITORING ---
  // Reconnect WiFi if connection is lost
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // --- AUTO PING SCHEDULER ---
  // Send keep-alive ping at configured intervals
  if (millis() - lastPingTime >= autoPingInterval) {
    lastPingTime = millis();
    sendKeepAlive();
  }

  // --- TELEGRAM MESSAGE CHECKER ---
  // Check for new Telegram commands at configured intervals
  if (millis() - lastTelegramCheck >= telegramCheckInterval) {
    lastTelegramCheck = millis();
    handleTelegramMessages();
  }

  // --- MEMORY HEALTH MONITOR ---
  // Check free heap memory every 60 seconds
  // Auto-restart if memory drops below 20KB to prevent crashes
  if (millis() - lastHeapCheck >= 60000) {
    lastHeapCheck = millis();
    int freeHeap = ESP.getFreeHeap();
    Serial.println("[SYS] Free heap: " + String(freeHeap));
    if (freeHeap < 20000) {
      Serial.println("[SYS] Low heap detected. Restarting...");
      ESP.restart();  // Perform soft reset
    }
  }

  // Brief delay to prevent overwhelming the MCU
  delay(500);
}
