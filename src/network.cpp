// network.cpp

#include "config.h"
#include "display.h" // Nextion 화면 제어를 위해 포함
#include "network.h"
#include <Preferences.h>

Preferences preferences;
// WiFiManager가 AP 모드 진입 시 호출할 콜백 함수
void configModeCallback(WiFiManager *myWiFiManager) {
  D_PRINTLN("AP 모드 진입.");
  D_PRINTLN(myWiFiManager->getConfigPortalSSID());
  myNex.writeStr("page apmode");
}

void setupWiFi() {
  preferences.begin("pet-dryroom", false); // "pet-dryroom" 네임스페이스로 NVS 시작
  
  // 부팅 시 AP 모드로 진입하라는 플래그가 있는지 확인
  bool startAPMode = preferences.getBool("start_ap", false);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String unique_id = mac.substring(mac.length() - 6);
  unique_ap_name = "Pet-" + unique_id;

  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(180);

  if (startAPMode) {
    D_PRINTLN("AP 모드 플래그 감지. 설정 포털을 시작합니다.");
    preferences.putBool("start_ap", false); // 플래그를 사용했으니 바로 삭제
    wm.resetSettings(); // 기존 WiFi 정보 삭제
    if (!wm.startConfigPortal(unique_ap_name.c_str())) {
      D_PRINTLN("Config portal timed out. Restarting.");
      delay(1000);
      ESP.restart();
    }
  } else {
    if (!wm.autoConnect(unique_ap_name.c_str())) {
      D_PRINTLN("WiFi 연결 실패, 재부팅합니다.");
      delay(1000);
      ESP.restart();
    }
  }
  
  preferences.end(); // NVS 종료

  D_PRINTLN("WiFi 연결 성공!");
  D_PRINT("IP 주소: ");
  D_PRINTLN(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void setupWebServer() {
  // 현재 팬/히터 상태를 JSON으로 반환
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    D_PRINTLN("--- GET /status 요청 수신 ---");
    JsonDocument doc;
    doc["fan_on"] = deviceState.isFanOn;
    doc["heater_on"] = deviceState.isHeaterOn;
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  // JSON 요청을 받아 팬/히터 상태 변경
  server.on(
    "/status", HTTP_POST,
    [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      D_PRINTLN("--- POST /status 요청 수신 ---");
      JsonDocument doc;
      if (deserializeJson(doc, (const char *)data, len)) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
      }

      if (doc["fan_on"].is<bool>()) {
        deviceState.isFanOn = doc["fan_on"].as<bool>();
        digitalWrite(FAN_CONTROL_PIN, deviceState.isFanOn ? HIGH : LOW);
        myNex.writeNum("bt0.val", deviceState.isFanOn ? 1 : 0);
        D_PRINTF("팬 상태 변경: %s\n", deviceState.isFanOn ? "ON" : "OFF");
      }

      if (doc["heater_on"].is<bool>()) {
        deviceState.isHeaterOn = doc["heater_on"].as<bool>();
        digitalWrite(MOSFET_PIN, deviceState.isHeaterOn ? HIGH : LOW);
        myNex.writeNum("bt1.val", deviceState.isHeaterOn ? 1 : 0);
        D_PRINTF("히터 상태 변경: %s\n", deviceState.isHeaterOn ? "ON" : "OFF");
      }

      // 변경된 상태를 다시 JSON으로 응답
      String jsonResponse;
      serializeJson(doc, jsonResponse);
      request->send(200, "application/json", jsonResponse);
    }
  );

  server.on("/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    D_PRINTLN("--- GET /sensors 요청 수신 ---");
    float temp = dht.getTemperature();
    float humi = dht.getHumidity();
    float co2 = MQ135.readSensor() + 400;

        // 2. 센서 읽기 실패 시 에러 응답을 보냄
    if (isnan(temp) || isnan(humi) || isnan(co2)) {
        D_PRINTLN("센서 값 읽기 실패");
        request->send(503, "application/json", "{\"error\":\"Sensor read failure\"}");
        return;
        }

        // 3. JSON 문서를 만들어 센서 값을 담습니다.
        JsonDocument doc;
        doc["temperature"] = String(temp, 1); // 소수점 한 자리까지
        doc["humidity"] = String(humi, 1);
        doc["co2"] = String(co2, 0); // 소수점 없이

        // 4. JSON을 문자열로 변환하여 응답으로 보냅니다.
        String jsonResponse;
        serializeJson(doc, jsonResponse);
        request->send(200, "application/json", jsonResponse);
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "ESP32 Pet Care System is running!");
  });

  server.begin();
  D_PRINTLN("웹 서버 시작.");
}

void uploadSensorData(float temp, float humi, float co2) {
  if (WiFi.status() != WL_CONNECTED) {
    D_PRINTLN("WiFi 연결이 끊겨 센서 데이터를 업로드할 수 없습니다.");
    return;
  }

  JsonDocument doc;
  doc["t"] = String(temp, 1);
  doc["h"] = String(humi, 1);
  doc["c"] = String(co2, 0);
  doc["d"] = getCurrentDateTime();
  doc["i"] = device_id;
  String jsonData;
  serializeJson(doc, jsonData);

  D_PRINTLN("센서 데이터 업로드 시도...");
  D_PRINTLN(jsonData);

  HTTPClient http;
  String serverPath = "http://" + String(upload_server) + ":" + String(upload_port) + String(upload_sensor_path);
  http.begin(serverPath);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(jsonData);
  D_PRINTF("HTTP 응답 코드: %d\n", httpResponseCode);
  http.end();
}

String getCurrentDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    D_PRINTLN("시간 정보를 가져오는 데 실패했습니다.");
    return "20250101000000";
  }
  char timeString[15];
  strftime(timeString, sizeof(timeString), "%Y%m%d%H%M%S", &timeinfo);
  return String(timeString);
}

void enterAPMode() {
  D_PRINTLN("AP 모드 진입을 위해 재부팅을 예약합니다.");
  preferences.begin("pet-dryroom", false);
  preferences.putBool("start_ap", true); // "다음 부팅 시 AP 모드로 진입하라" 플래그 설정
  preferences.end();
  
  delay(500);
  ESP.restart();
}