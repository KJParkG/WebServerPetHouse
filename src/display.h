// display.h

#ifndef DISPLAY_H
#define DISPLAY_H

// Nextion 디스플레이 시리얼 포트 및 라이브러리 초기화
void setupDisplay();

// Nextion 디스플레이에 센서 값들을 업데이트하는 함수
void updateDisplay(float temp, float humi, float co2, long rssi, bool occupied);

#endif