// audio.h

#ifndef AUDIO_H
#define AUDIO_H

void setupAudio();
void handleSoundCheck();
void recordAudio();
void createWavHeader(byte* header, uint32_t audioDataSize);
void uploadWavFile();
void forceRecordAndUpload();

#endif