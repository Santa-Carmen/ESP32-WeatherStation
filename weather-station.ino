#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

#define WIFI_SSID     "your_ssid"
#define WIFI_PASSWORD "your_password"
#define DHTPIN 15
#define DHTTYPE DHT22
#define HISTORY_SIZE 300
DHT dht(DHTPIN, DHTTYPE);

WebServer server(80);

float temperature = 0;
float humidity = 0;
unsigned long lastUpdate = 0;
unsigned long measureInterval = 2000;
int readingCount = 0;

struct Measurement {
    float temperature;
    float humidity;
    unsigned long timestamp;
};

Measurement history[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

void addMeasurement() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(t) || isnan(h)) {
        Serial.println("Error: sensor returned NaN");
        return;
    }
    if (t < -40.0 || t > 80.0 || h < 0.0 || h > 100.0) {
        Serial.println("Error: reading out of range");
        return;
    }

    temperature = t;
    humidity = h;

    history[historyIndex].temperature = temperature;
    history[historyIndex].humidity = humidity;
    history[historyIndex].timestamp = millis();
    readingCount ++;

    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;
}

void handleHistory() {
    String json = "[";
    
    for (int i = 0; i < historyCount; i++) {
        int index = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
        
        if (i > 0) json += ",";
        
        json += "{";
        json += "\"temperature\":" + String(history[index].temperature) + ",";
        json += "\"humidity\":" + String(history[index].humidity) + ",";
        json += "\"timestamp\":" + String(history[index].timestamp);
        json += "}";
    }
    
    json += "]";
    
    server.send(200, "application/json", json);
}

void handleExport() {
    String csv = "timestamp_s,temperature_C,humidity_percent,heat_index_C\n";

    for (int i = 0; i < historyCount; i++) {
        int index = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;

        float t = history[index].temperature;
        float h = history[index].humidity;
        float hi = dht.computeHeatIndex(t, h, false);

        csv += String(history[index].timestamp / 1000);
        csv += ",";
        csv += String(t, 1);
        csv += ",";
        csv += String(h, 1);
        csv += ",";
        csv += String(hi, 1);
        csv += "\n";
    }

    server.sendHeader("Content-Disposition", "attachment; filename=\"weather.csv\"");
    server.send(200, "text/csv", csv);
}

void handleSetInterval() {
    if (server.hasArg("ms")) {
        unsigned long ms = server.arg("ms").toInt();
        if (ms >= 2000 && ms <= 60000) {
            measureInterval = ms;
            server.send(200, "text/plain", "OK");
            return;
        }
    }
    server.send(400, "text/plain", "Bad interval");
}

void handleRoot() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>
        <title>Weather Station Pro</title>
        <script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0'></script>
        <style>
            body {
                font-family: Arial, sans-serif;
                max-width: 1200px;
                margin: 0 auto;
                padding: 20px;
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            }
            .card {
                background: white;
                border-radius: 15px;
                padding: 20px;
                margin: 20px 0;
                box-shadow: 0 5px 15px rgba(0,0,0,0.2);
            }
            .stats {
                display: flex;
                justify-content: space-around;
                flex-wrap: wrap;
            }
            .stat-box {
                text-align: center;
                padding: 20px;
                min-width: 200px;
            }
            .value {
                font-size: 3em;
                font-weight: bold;
                color: #667eea;
            }
            button {
                background: #667eea;
                color: white;
                border: none;
                padding: 15px 30px;
                border-radius: 10px;
                font-size: 1em;
                cursor: pointer;
                transition: 0.3s;
            }
            button:hover {
                background: #764ba2;
                transform: scale(1.05);
            }
            @media (max-width: 768px) {
                .stats { flex-direction: column; }
            }
        </style>
    </head>
    <body>
        <div class='card'>
            <h1 style='text-align: center; color: #667eea;'>🌡️ ESP32 Weather Station Pro</h1>
        </div>

        <div class='card stats'>
            <div class='stat-box'>
                <div style='font-size: 2em;'>🌡️</div>
                <div>Temperature</div>
                <div class='value'><span id='temperature'>--</span>°C</div>
            </div>
            <div class='stat-box'>
                <div style='font-size: 2em;'>💧</div>
                <div>Humidity</div>
                <div class='value'><span id='humidity'>--</span>%</div>
            </div>
            <div class='stat-box'>
                <div style='font-size: 2em;'>🔥</div>
                <div>Heat Index</div>
                <div class='value'><span id='heatIndex'>--</span>°C</div>
            </div>
        </div>

        <div class='card'>
            <button onclick='updateData()'>🔄 Manual Update</button>
            <button onclick='exportData()'>💾 Export CSV</button>
            <label style='margin-left: 10px;'>Update every:
                <select id='intervalSelect' onchange='changeInterval()'>
                    <option value='2000' selected>2 s</option>
                    <option value='5000'>5 s</option>
                    <option value='10000'>10 s</option>
                    <option value='30000'>30 s</option>
                    <option value='60000'>60 s</option>
                </select>
            </label>
            <p style='text-align: center; margin-top: 10px;'>
                Last update: <span id='lastUpdate'>--</span> | 
                Readings: <span id='readings'>0</span>
            </p>
        </div>

        <div class='card'>
            <h2>📊 Temperature & Humidity History (Chart)</h2>
            
            <div class='chart-container'>
                <canvas id='tempChart'></canvas>
            </div>

            <div class='chart-container'>
                <canvas id='humChart'></canvas>
            </div>
        </div>

        <div class='card'>
            <h2>📋 Recent Measurements</h2>
            <div id='history' style='max-height: 300px; overflow-y: auto;'></div>
        </div>

        <script>
            const maxDataPoints = 10;
            const tempData = [];
            const humData = [];
            const labels = [];
            
            const tempCtx = document.getElementById('tempChart').getContext('2d');
            const tempChart = new Chart(tempCtx, {
                type: 'line',
                data: {
                    labels: labels,
                    datasets: [{
                    label: 'Temperature (°C)',
                    data: tempData,
                    borderColor: 'rgb(255, 99, 132)',
                    backgroundColor: 'rgba(255, 99, 132, 0.1)',
                    tension: 0.4,
                    fill: true
                    }]
                },
                options: {
                    responsive: true,
                    scales: {
                    y: {
                        beginAtZero: false
                    }
                    },
                    animation: {
                    duration: 0
                    }
                }
            });

            const humCtx = document.getElementById('humChart').getContext('2d');
            const humChart = new Chart(humCtx, {
                type: 'line',
                data: {
                    labels: labels,
                    datasets: [{
                    label: 'Humidity (%)',
                    data: humData,
                    borderColor: 'rgb(54, 162, 235)',
                    backgroundColor: 'rgba(54, 162, 235, 0.1)',
                    tension: 0.4,
                    fill: true
                    }]
                },
                options: {
                    responsive: true,
                    scales: {
                    y: {
                        beginAtZero: false,
                        max: 100
                    }
                    },
                    animation: {
                    duration: 0
                    }
                }
            });

            function updateCharts() {
                fetch('/data')
                .then(response => response.json())
                .then(data => {
                    const now = new Date().toLocaleTimeString();
                    labels.push(now);
                    tempData.push(data.temperature);
                    humData.push(data.humidity);

                    if (labels.length > maxDataPoints) {
                        labels.shift();
                        tempData.shift();
                        humData.shift();
                    }

                    tempChart.update();
                    humChart.update();
                });
            }

            function updateData() {
                fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('temperature').textContent = data.temperature.toFixed(1);
                    document.getElementById('humidity').textContent = data.humidity.toFixed(1);
                    document.getElementById('heatIndex').textContent = data.heatIndex.toFixed(1);
                    document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
                    document.getElementById('readings').textContent = data.readings;
                })
                .catch(error => console.error('Error:', error));
            }

            function exportData() {
                window.location.href = '/export';
            }

            function loadHistory() {
                fetch('/history')
                .then(response => response.json())
                .then(list => {
                    if (list.length === 0) return;

                    const newest = list[list.length - 1].timestamp;

                    let html = '<table style="width:100%; text-align:center;">';
                    html += '<tr><th>Time</th><th>Temp, °C</th><th>Humidity, %</th></tr>';

                    for (let i = list.length - 1; i >= 0; i--) {
                        const m = list[i];
                        const age = newest - m.timestamp;
                        if (age > 3600000) break;

                        const time = new Date(Date.now() - age).toLocaleTimeString();
                        html += '<tr><td>' + time + '</td><td>' + m.temperature + '</td><td>' + m.humidity + '</td></tr>';
                    }

                    html += '</table>';
                    document.getElementById('history').innerHTML = html;
                })
                .catch(error => console.error('Error:', error));
            }
            
            function changeInterval() {
                const ms = parseInt(document.getElementById('intervalSelect').value);

                fetch('/setInterval?ms=' + ms)
                .catch(error => console.error('Error:', error));

                clearInterval(updateTimerData);
                clearInterval(updateTimerHist);
                clearInterval(updateTimerCharts);
                updateTimerData = setInterval(updateData, ms);
                updateTimerHist = setInterval(loadHistory, ms);
                updateTimerCharts = setInterval(updateCharts, ms);
            }

            let updateTimerData = null;
            let updateTimerHist = null;
            let updateTimerCharts = null;
            updateCharts();
            updateData();
            loadHistory();
            changeInterval();

        </script>
    </body>
    </html>
    )rawliteral";
    
        server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"temperature\":" + String(temperature) + ",";
  json += "\"humidity\":" + String(humidity) + ",";
  json += "\"heatIndex\":" + String(dht.computeHeatIndex(temperature, humidity, false)) + ",";
  json += "\"readings\":" + String(readingCount) + ",";
  json += "\"uptime\":" + String(millis() / 1000);
  json += "}";

  server.send(200, "application/json", json);
}

void setup() {
    Serial.begin(115200);
    dht.begin();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/history", handleHistory);
    server.on("/data", handleData);
    server.on("/export", handleExport);
    server.on("/setInterval", handleSetInterval);
    
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
    if (millis() - lastUpdate > measureInterval) {
        addMeasurement(); 
        lastUpdate = millis();
    }
}