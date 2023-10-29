#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <SNMP_Agent.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <deque>
#include <TimeLib.h>
#include <ESP_Mail_Client.h>
#include <TFT_eSPI.h>

const char* ssid = "";
const char* password = "";
// Global variables for timer
unsigned long backlightTimer = 0;
const unsigned long backlightDuration = 3000;  // 3 seconds
bool backlightOn = false;
unsigned int rootCounter = 0;
unsigned int chartCounter = 0;
unsigned int tempCounter = 0;
unsigned int humdCounter = 0;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
#define lcd_power 38

static bool wifi_connected = false;

const char* deviceName = "My Room ESP32";
float temperatureThreshold = 80.0;  // The temperature threshold to trigger an email

#define SMTP_HOST "smtp.mail.me.com" // Replace with your email provider SMTP server
#define SMTP_PORT 587 // Replace with your email provider SMTP server port
#define EMAIL_USER "" // Replace with your email address
#define EMAIL_PASSWORD "" // Replace with your email password
#define RECIPIENT_EMAIL "" // Replace with the recipient email address

unsigned long lastEmailTime = millis() - 3420000UL;  // 3420000 milliseconds = 57 minutes

char* oidTemperature = ".1.3.6.1.4.1.850.101.1.2.1.0";
char* oidHumidity = ".1.3.6.1.4.1.850.101.1.1.2.0";
const char* rocommunity = "ou812";  // Read only community string
const char* rwcommunity = "ou812"; // Read Write community string for set commands

#define DHTPIN 13
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);
float temperature = 60;
float humidity = 40;

#define MAX_READINGS 720  // Store data for the last 48 hours (averaged every 2 minutes)

std::deque<float> temperatureData;  // stores averaged temperature readings
std::deque<float> humidityData;  // stores averaged humidity readings
std::deque<String> labelsData;  // stores timestamps

unsigned long lastAveragingTime = 115000;
float temperatureSum = 0;
float humiditySum = 0;
int readingCount = 0;

#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_POWER_PIN 5
#define ETH_TYPE ETH_PHY_LAN8720
#define ETH_ADDR 0
#define ETH_MDC_PIN 23
#define ETH_MDIO_PIN 18

static bool eth_connected = false;
WebServer server(80);


SNMPAgent snmp = SNMPAgent(rocommunity, rwcommunity); // Creates an SNMPAgent instance

WiFiUDP udp;
int snmpSysUpTime = 0;
char* oidSysUpTime = ".1.3.6.1.2.1.1.3.0";
int snmpTemperature = 0;
int snmpHumidity = 0;

// Central Timezone (UTC -6 hours, +1 Daylight Saving)
int timezone = -5;
// Is currently daylight Saving Time in Central Timezone
bool daylightSaving = true;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", timezone * 3600, 60000);

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case SYSTEM_EVENT_STA_START:
        Serial.println("Wi-Fi Started");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        Serial.println("Wi-Fi Connected");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.print("Wi-Fi MAC: ");
        Serial.print(WiFi.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(WiFi.localIP());
        Serial.println();
        wifi_connected = true;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("Wi-Fi Disconnected");
        wifi_connected = false;
        break;
    default:
        break;
    }
}

void sendEmail(float temperature, float humidity) {
  // Declare the SMTPSession object for SMTP transport
  SMTPSession smtp;

  // Set the SMTP Server Email settings
  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = EMAIL_USER;
  config.login.password = EMAIL_PASSWORD;
  

  if (!smtp.connect(&config)) {
    Serial.println("Connection error, Status Code: " + String(smtp.statusCode()) + ", Error Code: " + String(smtp.errorCode()) + ", Reason: " + smtp.errorReason());
  } else {
    Serial.println("Connected to SMTP server.");
  }

  SMTP_Message message;
  message.sender.name = deviceName;
  message.sender.email = EMAIL_USER;
  message.subject = String(deviceName) + " High Temp Alert - " + String(timeClient.getFormattedTime());
  
  String htmlMsg = "<html><body style='background: linear-gradient(45deg, #00303f, #00566e); color: white; padding: 20px; font-family: Arial, sans-serif;'>";
  htmlMsg += "<div style='max-width: 600px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; color: #333;'>";
  htmlMsg += "<h1 style='text-align: center; color: #00566e;'>Current temperature is <span style='color: #00303f;'>" + String(temperature) + "F</span> and humidity is <span style='color: #00303f;'>" + String(humidity) + "%</span>.</h1>";
  htmlMsg += "<p style='text-align: center;'>See the live data at <a style='color: #00566e;' href='http://" + ETH.localIP().toString() + "'>" + ETH.localIP().toString() + "</a></p>";
  htmlMsg += "<hr style='border: none; border-top: 1px solid #ccc;' />";
  htmlMsg += "<h2 style='text-align: center; color: #00566e;'>Device Information</h2>";
  htmlMsg += "<p><strong>ETH MAC:</strong> " + ETH.macAddress() + "</p>";
  htmlMsg += "<p><strong>ETH Link Speed:</strong> " + String(ETH.linkSpeed()) + " Mbps</p>";
  htmlMsg += "<p><strong>Free Heap Memory:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
  htmlMsg += "<p><strong>Device Uptime:</strong> " + String(millis() / 1000 / 60) + " minutes</p>";
  htmlMsg += "</div></body></html>";
  message.html.content = htmlMsg;

  message.addRecipient("ALERT", RECIPIENT_EMAIL);
  message.addRecipient("Kevin", "kevinsnyder@letu.edu");

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Error sending Email, Status Code: " + String(smtp.statusCode()) + ", Error Code: " + String(smtp.errorCode()) + ", Reason: " + smtp.errorReason());
  } else {
    Serial.println("Email sent.");
  }

}

void handleRoot() {
  rootCounter++;
    String message = R"(
<!DOCTYPE html>
<html>
<head>
    <title>)" + String(deviceName) + R"(</title>
        <style>
        body {
            background: linear-gradient(45deg, #021927, #030d17);
            color: #fff;
            font-family: Arial, sans-serif;
            height: 100vh;
            margin: 0;
            background-size: cover;
            background-position: center;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: flex-start; /* Changed this from center to flex-start */
        }
        .info {
            display: flex;
            flex-direction: row;
            flex-wrap: wrap;
            justify-content: center;
            align-items: center;
            text-align: center;
            background: rgba(255, 255, 255, 0.1);
            border-radius: 10px;
            backdrop-filter: blur(10px);
            padding: 15px;
            margin-bottom: 10px;
            flex: 0 1 auto;
        }
        #chart {
            flex: 1 1 auto;
            max-height: 85vh;  // Adjust this value as needed
            width: 95%;
        }
        .info h2, .info h3 {
            margin: 0 10px;
            font-size: 14px;
        }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
    <div class="info">
        <h2>Device Name: )" + String(deviceName) + R"(</h2>
        <h3>Temperature OID: )" + String(oidTemperature) + R"(</h3>
        <h3>Humidity OID: )" + String(oidHumidity) + R"(</h3>
    </div>
    <canvas id="chart"></canvas>
    <script>
        var ctx = document.getElementById('chart').getContext('2d');
        var chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Temperature',
                    data: [],
                    borderColor: 'rgb(255, 99, 132)',
                    yAxisID: 'y',
                }, {
                    label: 'Humidity',
                    data: [],
                    borderColor: 'rgb(75, 192, 192)',
                    yAxisID: 'y1',
                }]
            },
            options: {
                scales: {
                    y: {
                        suggestedMin: 65,
                        suggestedMax: 85,
                        beginAtZero: false,
                        grid: {
                            drawOnChartArea: false,
                        },
                        ticks: {
                            color: 'rgba(255, 255, 255, 0.8)',
                        }
                    },
                    y1: {
                        suggestedMin: 30,
                        suggestedMax: 60,
                        beginAtZero: false,
                        position: 'right',
                        grid: {
                            drawOnChartArea: false,
                        },
                        ticks: {
                            color: 'rgba(255, 255, 255, 0.8)',
                        }
                    },
                    yGrid: {
                        display: false,
                        position: 'left',
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)',
                        },
                    },
                    x: {
                        grid: {
                            color: 'rgba(255, 255, 255, 0.1)',
                        },
                        ticks: {
                            color: 'rgba(255, 255, 255, 0.8)',
                        }
                    }
                },
                animation: true,
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        labels: {
                            color: '#fff',
                        }
                    }
                }
            }
        });

        function fetchData() {
            fetch('/chartdata')
                .then(response => response.json())
                .then(data => {
                    chart.data.labels = data.labels;
                    chart.data.datasets[0].data = data.temperature;
                    chart.data.datasets[1].data = data.humidity;
                    chart.update();
                });
        }

        setInterval(fetchData, 60000);  // Fetch data every minute
        fetchData();  // Fetch data immediately
    </script>
</body>
</html>
    )";

    server.send(200, "text/html", message);
    // Turn on the backlight and set the timer
    // digitalWrite(15, 1);
    digitalWrite(lcd_power, HIGH); 
    backlightTimer = millis();
    backlightOn = true;
}


void updateTFT() {
  // Create a sprite for double buffering (optional)
  sprite.createSprite(170, 320);

  // Clear the sprite by filling it with black (or any other color)
  sprite.fillSprite(TFT_BLACK);

  // Draw text on the sprite
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(2);
  sprite.setCursor(0, 0);
  sprite.print("Endpoint Usage");

  sprite.setTextSize(3);

  sprite.setCursor(0, 30);
  sprite.setTextColor(TFT_LIGHTGREY);
  sprite.print("Root:  ");
  sprite.setTextColor(TFT_WHITE);
  sprite.print(rootCounter);

  sprite.setCursor(0, 70);
  sprite.setTextColor(TFT_LIGHTGREY);
  sprite.print("Chart: ");
  sprite.setTextColor(TFT_WHITE);
  sprite.print(chartCounter);

  sprite.setCursor(0, 110);
  sprite.setTextColor(TFT_LIGHTGREY);
  sprite.print("Temp:  ");
  sprite.setTextColor(TFT_WHITE);
  sprite.print(tempCounter);

  sprite.setCursor(0, 150);
  sprite.setTextColor(TFT_LIGHTGREY);
  sprite.print("Humd:  ");
  sprite.setTextColor(TFT_WHITE);
  sprite.print(humdCounter);

  // Push sprite to TFT screen
  sprite.pushSprite(0, 0);
}

// Handle Temperature request
void handleTemperature() {
    tempCounter++;
    server.send(200, "text/plain", String(temperature));
}

// Handle Humidity request
void handleHumidity() {
    humdCounter++;
    server.send(200, "text/plain", String(humidity));
}

String getChartData() {
    String data = "{";
    data += "\"labels\": [";
    for(int i = 0; i < labelsData.size(); i++) {
        if(i > 0) data += ",";
        data += "\"" + labelsData[i] + "\"";
    }
    data += "], \"temperature\": [";
    for(int i = 0; i < temperatureData.size(); i++) {
        if(i > 0) data += ",";
        data += String(temperatureData[i]);
    }
    data += "], \"humidity\": [";
    for(int i = 0; i < humidityData.size(); i++) {
        if(i > 0) data += ",";
        data += String(humidityData[i]);
    }
    data += "]}";
    return data;
}

void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

// void WiFiEvent(WiFiEvent_t event) {
//     switch (event) {
//     case ARDUINO_EVENT_ETH_START:
//         Serial.println("ETH Started");
//         //set eth hostname here
//         ETH.setHostname("esp32-ethernet");
//         break;
//     case ARDUINO_EVENT_ETH_CONNECTED:
//         Serial.println("ETH Connected");
//         break;
//     case ARDUINO_EVENT_ETH_GOT_IP:
//         Serial.print("ETH MAC: ");
//         Serial.print(ETH.macAddress());
//         Serial.print(", IPv4: ");
//         Serial.print(ETH.localIP());
//         if (ETH.fullDuplex()) {
//             Serial.print(", FULL_DUPLEX");
//         }
//         Serial.print(", ");
//         Serial.print(ETH.linkSpeed());
//         Serial.println("Mbps");
//         eth_connected = true;
//         break;
//     case ARDUINO_EVENT_ETH_DISCONNECTED:
//         Serial.println("ETH Disconnected");
//         eth_connected = false;
//         break;
//     case ARDUINO_EVENT_ETH_STOP:
//         Serial.println("ETH Stopped");
//         eth_connected = false;
//         break;
//     default:
//         break;
//     }
// }

String sanitizeDeviceName(const char* deviceName) {
    String sanitizedDeviceName = "";

    for (int i = 0; deviceName[i] != '\0'; i++) {
        char c = deviceName[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '-')) {
            sanitizedDeviceName += c;
        }
    }

    // Ensure the first character is a letter
    if (!(sanitizedDeviceName[0] >= 'a' && sanitizedDeviceName[0] <= 'z') &&
        !(sanitizedDeviceName[0] >= 'A' && sanitizedDeviceName[0] <= 'Z')) {
        sanitizedDeviceName = 'A' + sanitizedDeviceName;
    }

    return sanitizedDeviceName;
}


void setup() {
    Serial.begin(115200);

    WiFi.onEvent(WiFiEvent);
    // ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN,
    //           ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

    // while (!eth_connected) {
    //     Serial.println("Wait for network connect ..."); delay(500);
    // }

    WiFi.begin(ssid, password);

    while (!wifi_connected) {
        Serial.println("Waiting for Wi-Fi to connect ..."); 
        delay(500);
    }
    // Initialize the TFT and backlight pin
    pinMode(15, OUTPUT);
    tft.init();
    updateTFT();
    pinMode(lcd_power, OUTPUT);          // set arduino pin to output mode
    digitalWrite(lcd_power, LOW); 



    String sanitizedDeviceName = sanitizeDeviceName(deviceName);
    if (MDNS.begin(sanitizedDeviceName.c_str())) {
        Serial.println("MDNS responder started");
    }


    server.on("/", handleRoot);
    // Add the handlers for /temp and /hum
    server.on("/temp", handleTemperature);
    server.on("/humd", handleHumidity);
    server.on("/inline", []() {
        server.send(200, "text/plain", "inline!");
    });
    server.on("/chartdata", []() {
        chartCounter++;
        server.send(200, "application/json", getChartData());
    });

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started");

    dht.begin();

    snmp.setUDP(&udp);
    snmp.begin();
    snmp.addIntegerHandler(oidSysUpTime, &snmpSysUpTime);
    snmp.addIntegerHandler(oidTemperature, &snmpTemperature);
    snmp.addIntegerHandler(oidHumidity, &snmpHumidity);

    // Start NTP client
    timeClient.begin();
}

void loop() {
    server.handleClient();
    snmpSysUpTime = millis() * 10;  // Convert from milliseconds to hundredths of a second

    // NTP client update
    timeClient.update();

    digitalWrite(15, 0);  // Turn off backlight
    // Check if it's time to turn off the backlight
    if (backlightOn && millis() - backlightTimer > backlightDuration) {
      // digitalWrite(15, 0);  // Turn off backlight
      digitalWrite(lcd_power, LOW); 
      backlightOn = false;
      Serial.println("BACKLIGHT OFF");
    }
    // Check if the backlight is on before updating the TFT
    if (backlightOn) {
      updateTFT();
    }
    
    static unsigned long lastReadTime = millis();
    if (millis() - lastReadTime > 1000) { // Read every 1000 ms
        lastReadTime = millis();
        float newTemperature = dht.readTemperature(true);  // For F
        float newHumidity = dht.readHumidity();
        if (!isnan(newTemperature) && !isnan(newHumidity)) {  // only update values if they are valid
            temperatureSum += newTemperature;
            humiditySum += newHumidity;
            readingCount++;

            if (millis() - lastAveragingTime > 120000) {  // average every 2 minutes
                lastAveragingTime = millis();
                temperature = temperatureSum / readingCount;
                snmpTemperature = (int)(temperature);  // SNMP uses integer
                temperatureData.push_back(temperature);
                if(temperatureData.size() > MAX_READINGS) {
                    temperatureData.pop_front();
                }

                humidity = humiditySum / readingCount;
                snmpHumidity = (int)(humidity);  // SNMP uses integer
                humidityData.push_back(humidity);
                if(humidityData.size() > MAX_READINGS) {
                    humidityData.pop_front();
                }
                
                int hours = timeClient.getHours();
                String AM_PM = hours < 12 ? "AM" : "PM";
                if (hours > 12) hours -= 12; // convert to 12-hour format
                if (hours == 0) hours = 12; // handle midnight case

                String now = String(hours) + ":" + (timeClient.getMinutes() < 10 ? "0" : "") + String(timeClient.getMinutes()) + " " + AM_PM;

                labelsData.push_back(now);
                if(labelsData.size() > MAX_READINGS) {
                    labelsData.pop_front();
                }

                temperatureSum = 0;
                humiditySum = 0;
                readingCount = 0;
            }
        }
    }
    
    // Send an email when the temperature exceeds a certain threshold and an hour has passed since the last email.
    if (temperature > temperatureThreshold && millis() - lastEmailTime > 3600000) {
        Serial.println("Email attempt!");
        sendEmail(temperature, humidity);
        lastEmailTime = millis();  // Update the time the last email was sent
    }

    snmp.loop();  // SNMP server loop

    delay(2);  //allow the cpu to switch to other tasks
}
