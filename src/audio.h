#ifndef AUDIO_H
#define AUDIO_H

void setupAudio();
void handleSoundCheck();
void forceRecordAndUpload();
void realtimeRecordAndUpload(); // 기존 recordAudio를 대체할 새로운 함수

#endif