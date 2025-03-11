#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <PubSubClient.h>

// ===================== Wi-Fi AP Mode (ใช้ตั้งค่า) =====================
const char* ssid_ap = "ESP32_Config";
const char* pass_ap = "12345678";

// ===================== Web Server =====================
WebServer server(80);

// ===================== MQTT =====================
WiFiClient espClient;
PubSubClient client(espClient);

// ===================== ตัวแปรเก็บค่าคอนฟิก =====================
String wifi_ssid     = "";
String wifi_pass     = "";
String mqtt_server   = "";
String mqtt_user     = "";
String mqtt_password = "";
String device_name   = "";
String mqtt_topic    = "";
String mqtt_sub      = "";

// ===================== สถานะการเชื่อมต่อ & การตั้งค่า =====================
bool mqttConnected = false;
bool shouldConnect = false;  // เมื่อกดปุ่ม "Connect" ในหน้าเว็บ

// ===================== LED & ปุ่ม Reset =====================

#define RESET_BUTTON 0

// หาก LED เป็น Active Low -> LOW = ติด, HIGH = ดับ
unsigned long ledTimer = 0;
int ledState = HIGH;  // เริ่มต้นไฟดับ (หาก Active Low)

// ===================== ฟังก์ชันโหลดค่า config จาก SPIFFS =====================
void loadConfig() {
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
        return;
    }

    File file = SPIFFS.open("/config.txt", "r");
    if (!file) {
        Serial.println("No config found (config.txt not found).");
        return;
    }

    wifi_ssid     = file.readStringUntil('\n');
    wifi_pass     = file.readStringUntil('\n');
    mqtt_server   = file.readStringUntil('\n');
    mqtt_user     = file.readStringUntil('\n');
    mqtt_password = file.readStringUntil('\n');
    device_name   = file.readStringUntil('\n');
    mqtt_topic    = file.readStringUntil('\n');
    mqtt_sub      = file.readStringUntil('\n');

    wifi_ssid.trim(); 
    wifi_pass.trim();
    mqtt_server.trim();
    mqtt_user.trim();
    mqtt_password.trim();
    device_name.trim();
    mqtt_topic.trim();
    mqtt_sub.trim();

    file.close();

    Serial.println("=== Current Config ===");
    Serial.println("WiFi SSID:      " + wifi_ssid);
    Serial.println("WiFi PASS:      " + wifi_pass);
    Serial.println("MQTT Server:    " + mqtt_server);
    Serial.println("MQTT User:      " + mqtt_user);
    Serial.println("MQTT Password:  " + mqtt_password);
    Serial.println("Device Name:    " + device_name);
    Serial.println("Pub Topic:      " + mqtt_topic);
    Serial.println("Sub Topic:      " + mqtt_sub);
    Serial.println("======================");
}

// ===================== ฟังก์ชันบันทึกค่า config =====================
void saveConfig() {
    File file = SPIFFS.open("/config.txt", "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    file.println(wifi_ssid);
    file.println(wifi_pass);
    file.println(mqtt_server);
    file.println(mqtt_user);
    file.println(mqtt_password);
    file.println(device_name);
    file.println(mqtt_topic);
    file.println(mqtt_sub);

    file.close();
    Serial.println("Configuration saved to /config.txt");
}

// ===================== ฟังก์ชันลบ config =====================
void eraseConfig() {
    SPIFFS.remove("/config.txt");
    Serial.println("Configuration erased!");
}

// ===================== ฟังก์ชันรับข้อความจาก MQTT =====================
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message received on topic: ");
    Serial.println(topic);

    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.print("Message: ");
    Serial.println(message);
}

// ===================== ฟังก์ชันเชื่อมต่อ MQTT =====================
void connectToMQTT() {
    client.setServer(mqtt_server.c_str(), 1883);
    client.setCallback(callback);

    while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        // เชื่อมต่อแบบใช้ username/password
        if (client.connect(device_name.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) {
            Serial.println("Connected to MQTT!");
            mqttConnected = true;

            // ถ้ามี topic สำหรับ Subscribe
            if (mqtt_sub.length() > 0) {
                client.subscribe(mqtt_sub.c_str());
                Serial.print("Subscribed: ");
                Serial.println(mqtt_sub);
            }
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds...");
            delay(5000);
        }
    }
}

// ===================== ฟังก์ชันสร้าง Web Portal =====================
void setupWebPortal() {
    WiFi.softAP(ssid_ap, pass_ap);
    Serial.println("AP Mode Started -> 192.168.4.1");

    server.on("/", HTTP_GET, []() {
        String html = R"rawliteral(
        <html>
        <head>
          <meta name="viewport" content="width=device-width, initial-scale=1.0">
          <style>
            body { font-family: Arial, sans-serif; margin: 20px; }
            input[type="text"], input[type="password"] {
              width: 100%; margin-bottom: 8px; padding: 8px;
            }
            input[type="submit"], button {
              width: 100%; padding: 10px; margin-top: 10px;
              background: #4CAF50; color: #fff; border: none; cursor: pointer;
            }
          </style>
        </head>
        <body>
          <h1>ESP32 Configuration</h1>
          <form action='/save' method='POST'>
            SSID: <input type='text' name='ssid'><br>
            Password: <input type='password' name='pass'><br>
            MQTT Server: <input type='text' name='mqtt_server'><br>
            MQTT Username: <input type='text' name='mqtt_user'><br>
            MQTT Password: <input type='password' name='mqtt_password'><br>
            Device Name: <input type='text' name='device_name'><br>
            MQTT Publish Topic: <input type='text' name='mqtt_topic'><br>
            MQTT Subscribe Topic: <input type='text' name='mqtt_sub'><br>
            <input type='submit' value='Save'>
          </form>
          <br>
          <a href='/connect'><button>Connect WiFi & MQTT</button></a>
        </body>
        </html>
        )rawliteral";

        server.send(200, "text/html", html);
    });

    // POST /save -> บันทึกค่า
    server.on("/save", HTTP_POST, []() {
        if (server.hasArg("ssid") && server.hasArg("pass") &&
            server.hasArg("mqtt_server") && server.hasArg("mqtt_user") &&
            server.hasArg("mqtt_password") && server.hasArg("device_name") &&
            server.hasArg("mqtt_topic") && server.hasArg("mqtt_sub")) {
            
            wifi_ssid     = server.arg("ssid");
            wifi_pass     = server.arg("pass");
            mqtt_server   = server.arg("mqtt_server");
            mqtt_user     = server.arg("mqtt_user");
            mqtt_password = server.arg("mqtt_password");
            device_name   = server.arg("device_name");
            mqtt_topic    = server.arg("mqtt_topic");
            mqtt_sub      = server.arg("mqtt_sub");

            saveConfig();
            server.send(200, "text/html", "<h1>Saved! Go back and press Connect.</h1>");
        } else {
            server.send(400, "text/html", "<h1>Error: Missing fields</h1>");
        }
    });

    // GET /connect -> เริ่มเชื่อมต่อ Wi-Fi & MQTT
    server.on("/connect", HTTP_GET, []() {
        shouldConnect = true;
        server.send(200, "text/html", "<h1>Connecting... Please wait.</h1>");
    });

    server.begin();
}

// ===================== ฟังก์ชัน updateLED (แสดงสถานะ) =====================
void updateLED() {
    // เรียงลำดับ: ถ้า MQTT ติด -> ติดค้าง, ถ้าเชื่อม WiFi แล้วยังไม่ติด MQTT -> กระพริบ 500ms
    // กด Connect แล้วยังไม่ต่อ WiFi -> กระพริบ 500ms, ไม่กด Connect -> กระพริบเร็ว 100ms
    
    unsigned long currentMillis = millis();

    if (mqttConnected) {
        // Active Low -> LOW = ติด
        digitalWrite(LED_BUILTIN, LOW); 
        return;
    }

    // เชื่อม Wi-Fi ได้ แต่ MQTT ยังไม่ติด
    if (WiFi.status() == WL_CONNECTED && !mqttConnected) {
        if (currentMillis - ledTimer >= 500) {
            ledTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState);
        }
        return;
    }

    // กด Connect แล้ว แต่ยังไม่ได้เชื่อม Wi-Fi
    if (shouldConnect && WiFi.status() != WL_CONNECTED) {
        if (currentMillis - ledTimer >= 500) {
            ledTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState);
        }
        return;
    }

    // ยังไม่กด Connect -> กระพริบเร็ว
    if (!shouldConnect) {
        if (currentMillis - ledTimer >= 100) {
            ledTimer = currentMillis;
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState);
        }
        return;
    }
}

// ===================== ฟังก์ชัน "สุ่มอุณหภูมิ" และส่งค่าไปยัง MQTT =====================
// โครงสร้าง JSON:
// {
//   "device_name":"...",
//   "topic_name":"...",
//   "value":25.37
// }
void publishRandomTemperature() {
    // สุ่มค่า 20-29 + ทศนิยม 2 ตำแหน่ง
    float sensorValue = random(20, 30) + (random(0, 99) / 100.0);

    // สร้าง Payload ในรูป JSON
    // ( device_name, topic_name, value ) ตามที่ต้องการ
    String payload = "{";
    payload += "\"device_name\":\"" + device_name + "\",";
    payload += "\"topic_name\":\"" + mqtt_topic + "\",";
    payload += "\"value\":" + String(sensorValue);
    payload += "}";

    // ส่งเฉพาะถ้า mqtt_topic ไม่ว่าง
    if (mqtt_topic.length() > 0) {
        client.publish(mqtt_topic.c_str(), payload.c_str());
        Serial.print("Published to ");
        Serial.print(mqtt_topic);
        Serial.print(": ");
        Serial.println(payload);
    }
}

// ===================== Setup =====================
unsigned long lastPublish = 0;
const unsigned long publishInterval = 5000; // ส่งทุก 5 วินาที

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(RESET_BUTTON, INPUT_PULLUP);

    // ลบ config ถ้ากด GPIO0 ตอนบูต (ตามต้องการ)
    if (digitalRead(RESET_BUTTON) == LOW) {
        Serial.println("Reset button pressed => erase config.");
        eraseConfig();
    }

    loadConfig();

    // เปิด AP Mode และสร้าง Web Portal
    WiFi.softAP(ssid_ap, pass_ap);
    setupWebPortal();

    Serial.println("Setup done! Go to 192.168.4.1 -> Save/Connect.");
}

// ===================== Loop =====================
void loop() {
    // ให้ WebServer ทำงาน
    server.handleClient();

    // ถ้าผู้ใช้กดปุ่ม Connect
    if (shouldConnect) {
        // ปิด AP แล้วเริ่ม STA
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);

        // เชื่อมต่อ Wi-Fi
        Serial.println("Connecting to WiFi...");
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
        if (WiFi.waitForConnectResult() == WL_CONNECTED) {
            Serial.println("WiFi connected!");
            Serial.print("Local IP: ");
            Serial.println(WiFi.localIP());
            connectToMQTT();  
        } else {
            Serial.println("WiFi connect failed! Check SSID/PASS.");
        }
        shouldConnect = false; 
    }

    // ถ้า Wi-Fi + MQTT พร้อมแล้ว -> Loop MQTT และส่งข้อมูล
    if (WiFi.status() == WL_CONNECTED && client.connected()) {
        client.loop();

        unsigned long now = millis();
        if (now - lastPublish >= publishInterval) {
            lastPublish = now;
            // เรียกฟังก์ชันสุ่มอุณหภูมิแล้ว Publish
            publishRandomTemperature();
        }
    }

    // อัปเดตสถานะไฟ LED
    updateLED();
}
