// sensors.h

#ifndef SENSORS_H
#define SENSORS_H

void setupPins();
void setupSensors();
void handleSensorAndDisplayUpdates();
void forceUploadSensorData(); // trigger2의 새 이름

#endif