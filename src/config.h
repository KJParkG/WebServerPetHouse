// config.h

#pragma once // 헤더 파일 중복 포함 방지

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHTesp.h>
#include <MQUnifiedsensor.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <EasyNextionLibrary.h>
#include <driver/i2s.h>

// ------------------ 디버그 출력 설정 -----------------------
#define DEBUG_MODE // 이 줄을 주석 처리하면 모든 Serial 출력이 비활성화됩니다.

#ifdef DEBUG_MODE
  #define D_PRINT(...)    Serial.print(__VA_ARGS__)
  #define D_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define D_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define D_PRINT(...)
  #define D_PRINTLN(...)
  #define D_PRINTF(...)
#endif

// ------------------ 핀 번호 설정 -----------------------
#define DHTPIN            7
#define PIR_PIN           2
#define FAN_CONTROL_PIN   3
#define MOSFET_PIN        5 // 히터 제어용 MOSFET 핀
#define MQ135_PIN         4

// I2S (마이크) 핀 설정
#define I2S_WS_PIN        41
#define I2S_SD_PIN        42
#define I2S_SCK_PIN       40
#define I2S_PORT          I2S_NUM_0

// ------------------ 센서 설정 -----------------------
#define board               "LOLIN S3 PRO"
#define sensortype          "MQ-135"
#define Voltage_Resolution  3.3
#define ADC_Bit_Resolution  12
#define RatioMQ135CleanAir  3.6

// ------------------ 오디오 녹음 설정 -----------------------
const int SAMPLE_RATE       = 16000; // 샘플링 속도 (파일 크기를 위해 16kHz로 조정)
const int BIT_DEPTH         = 16;
const int NUM_CHANNELS      = 1;     // 모노
const int RECORD_SECONDS    = 10;    // 녹음 시간(초)

// 소음 감지 설정
#define SOUND_DETECT_DB           90.0f // 이 데시벨을 넘으면 녹음 시작
#define REFERENCE_RMS             0.0501187f
#define DB_CHECK_BUFFER_SIZE      1024
const int REQUIRED_CONSECUTIVE_HITS = 3; // 연속으로 3번 이상 기준 데시벨 초과 시 녹음

const int WAV_HEADER_SIZE = 44;
const uint32_t AUDIO_DATA_SIZE = RECORD_SECONDS * SAMPLE_RATE * NUM_CHANNELS * (BIT_DEPTH / 8);

// ------------------ 네트워크 및 서버 설정 -----------------
extern const char* upload_server;
extern const int   upload_port;
extern const char* upload_sensor_path;
extern const char* upload_file_path;
extern const char* device_id;
extern String unique_ap_name;

// 시간 서버(NTP) 설정
extern const char* ntpServer;
extern const long  gmtOffset_sec;
extern const int   daylightOffset_sec;

// ------------------ 동작 주기 설정 -----------------------
const long update_interval      = 1000;
const long sound_check_interval = 200;

// ------------------ 전역 객체 선언 -----------------------
extern DHTesp dht;
extern MQUnifiedsensor MQ135;
extern EasyNex myNex;
extern AsyncWebServer server;

// ------------------ 전역 상태 변수 -----------------------
struct DeviceState {
  bool isFanOn = false;
  bool isHeaterOn = false;
  bool isRecording = false;
};
extern DeviceState deviceState;

// ------------------ 전역 버퍼 선언 -----------------------
extern int16_t* audio_buffer_psram;
extern byte wav_header[WAV_HEADER_SIZE];


