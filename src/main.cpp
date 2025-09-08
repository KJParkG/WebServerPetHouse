#include "config.h"
#include "network.h"
#include "sensors.h"
#include "audio.h"
#include "display.h"

// ==========================================================
//      config.h에 선언된 전역 변수들의 실제 값을 여기서 정의합니다.
// ==========================================================
const char* upload_server       = "192.168.219.106"; // 사용하시던 서버 주소로 변경하세요
const int   upload_port         = 8080;
const char* upload_sensor_path  = "/FarmData/api/datainput.do";
const char* upload_file_path    = "/FarmData/fileUpload.do";
const char* device_id           = "TEST_MACHINE";
const char* ntpServer           = "pool.ntp.org";
const long  gmtOffset_sec       = 9 * 3600;
const int   daylightOffset_sec  = 0;
// ==========================================================

String unique_ap_name;

// 라이브러리 객체 생성
DHTesp dht;
MQUnifiedsensor MQ135(board, Voltage_Resolution, ADC_Bit_Resolution, MQ135_PIN, sensortype);
EasyNex myNex(Serial2);
AsyncWebServer server(80); // server 객체 생성 80번
// 전역 상태 변수
DeviceState deviceState;

// FreeRTOS 핸들 정의
SemaphoreHandle_t audioSemaphore;
TaskHandle_t audioTaskHandle;

// 오디오 버퍼
int16_t* audio_buffer_psram = NULL;
uint8_t* mp3_buffer_psram = NULL;

// 오디오 Task 함수 (audio.cpp에 구현됨)
void audio_task_function(void *pvParameters);

void setup() {
  Serial.begin(115200);
  delay(1000);
  D_PRINTLN("--- 펫 드라이룸 시스템 시작 (Web Server Mode) ---");

  setupPins();
  setupDisplay();
  setupSensors();
  
  // 오디오 Task 생성
  audioSemaphore = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(
      audio_task_function,    // Task 함수
      "Audio Task",           // Task 이름
      10000,                  // Stack 크기
      NULL,                   // Task 파라미터
      1,                      // 우선순위
      &audioTaskHandle,       // Task 핸들
      1);                     // Core 1에서 실행

  setupWiFi();
  setupWebServer();
  setupAudio();

  D_PRINTLN("--- 모든 설정 완료. 오디오 안정화 대기 중... ---");
  delay(1000);

  myNex.writeStr("page main");
}

void loop() {
  myNex.NextionListen();
  handleSensorAndDisplayUpdates();
  handleSoundCheck();
}
