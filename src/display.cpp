// display.cpp

#include "config.h"
#include "display.h"
#include "network.h" // enterAPMode() 호출을 위해 포함
#include "sensors.h" // forceUploadSensorData() 호출을 위해 포함
#include "audio.h"   // forceRecordAndUpload() 호출을 위해 포함

// 디스플레이 초기화
void setupDisplay() {
  Serial2.begin(115200, SERIAL_8N1, 18, 17);
  myNex.begin(115200);
}

// 디스플레이에 정보 업데이트
void updateDisplay(float temp, float humi, float co2, long rssi, bool occupied) {
  myNex.writeNum("temp.val", temp * 10);
  myNex.writeNum("humi.val", humi * 10);
  myNex.writeNum("co2.val", co2);
  myNex.writeNum("rssi.val", rssi);
  myNex.writeStr("occu.txt", occupied ? "O" : "X");
  myNex.writeStr("ip.txt", WiFi.localIP().toString());
}

// ======== 디스플레이 trigger 함수들 =========

// 팬 On/Off 버튼
void trigger0() {
  deviceState.isFanOn = !deviceState.isFanOn;
  digitalWrite(FAN_CONTROL_PIN, deviceState.isFanOn ? HIGH : LOW);
  D_PRINTLN(deviceState.isFanOn ? "FAN ON" : "FAN OFF");
}

// AP 모드 진입 버튼
void trigger1() {
  enterAPMode();
}

// 센서 데이터 전송 버튼
void trigger2() {
  forceUploadSensorData();
}

// 히터 On/Off 버튼
void trigger3() {
  deviceState.isHeaterOn = !deviceState.isHeaterOn;
  digitalWrite(MOSFET_PIN, deviceState.isHeaterOn ? HIGH : LOW);
  D_PRINTLN(deviceState.isHeaterOn ? "HEATER ON" : "HEATER OFF");
}

// 녹음 및 업로드 버튼
void trigger4() {
  forceRecordAndUpload();
}