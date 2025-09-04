
#ifndef NETWORK_H
#define NETWORK_H

void setupWiFi();
void setupWebServer();
void uploadSensorData(float temp, float humi, float co2);
String getCurrentDateTime();
void enterAPMode(); // trigger1

#endif