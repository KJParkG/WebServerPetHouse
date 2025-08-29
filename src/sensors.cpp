// sensors.cpp

#include "config.h"
#include "sensors.h"
#include "display.h" // updateDisplay() 호출을 위해 포함
#include "network.h" // uploadSensorData() 호출을 위해 포함

unsigned long last_update = 0;

void setupPins() {
  pinMode(FAN_CONTROL_PIN, OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);

  digitalWrite(FAN_CONTROL_PIN, LOW);
  digitalWrite(MOSFET_PIN, LOW);
}

void setupSensors() {
  dht.setup(DHTPIN, DHTesp::DHT11);

  // MQ-135 센서 초기화 및 안정화(Calibration)
  MQ135.setRegressionMethod(1);
  MQ135.setA(110.47); MQ135.setB(-2.862);
  MQ135.init();

  D_PRINT("MQ-135 센서 안정화 중...");
  float R0 = 0.0;
  for (int i = 0; i < 10; i++) {
    MQ135.update();
    R0 += MQ135.calibrate(RatioMQ135CleanAir);
    delay(500);
    D_PRINT(".");
  }
  MQ135.setR0(R0 / 10.0);
  D_PRINTLN(" 완료!");
}

// 주기적으로 센서 값을 읽고 디스플레이를 업데이트하는 메인 함수
void handleSensorAndDisplayUpdates() {
  if (millis() - last_update >= update_interval) {
    last_update = millis();

    bool isOccupied = (digitalRead(PIR_PIN) == HIGH);
    float temp = dht.getTemperature();
    float humi = dht.getHumidity();
    float co2 = MQ135.readSensor();

    if (isnan(temp) || isnan(humi) || isnan(co2) || isinf(co2)) {
      D_PRINTLN("센서 값 읽기 실패");
      return;
    }

    // Nextion 디스플레이 업데이트
    updateDisplay(temp, humi, co2 + 400, WiFi.RSSI(), isOccupied);
  }
}

// Nextion 버튼으로 센서 데이터 강제 전송
void forceUploadSensorData() {
  D_PRINTLN("Nextion 요청: 센서 데이터 강제 업로드");
  float temp = dht.getTemperature();
  float humi = dht.getHumidity();
  float co2 = MQ135.readSensor() + 400;

  if (isnan(temp) || isnan(humi)) {
     D_PRINTLN("센서 값 읽기 실패, 업로드 취소");
     return;
  }
  uploadSensorData(temp, humi, co2);
}