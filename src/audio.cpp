// audio.cpp

#include "config.h"
#include "audio.h"
#include "network.h" // getCurrentDateTime(), uploadSensorData() 호출을 위해 포함

unsigned long last_sound_check = 0;
int consecutive_high_count = 0;

void initI2S() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 1024,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

  const i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK_PIN,
      .ws_io_num = I2S_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_SD_PIN
  };
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setupAudio() {
  if (!psramInit()) {
    D_PRINTLN("PSRAM을 찾을 수 없습니다.");
  } else {
    audio_buffer_psram = (int16_t*)ps_malloc(AUDIO_DATA_SIZE);
    if (audio_buffer_psram == NULL) {
      D_PRINTLN("PSRAM 메모리 할당 실패. 프로그램을 중지합니다.");
      while (1);
    }
    D_PRINTLN("PSRAM 오디오 버퍼 할당 성공");
  }

  initI2S();
  i2s_start(I2S_PORT); // 소음 감지를 위해 I2S를 계속 켜 둡니다.
}

void handleSoundCheck() {
  if (millis() - last_sound_check >= sound_check_interval) {
    last_sound_check = millis();
    
    if (deviceState.isRecording) return; // 녹음 중일 때는 소음 감지 안 함

    int16_t samples[DB_CHECK_BUFFER_SIZE];
    size_t bytes_read;
    i2s_read(I2S_PORT, &samples, sizeof(samples), &bytes_read, pdMS_TO_TICKS(100));

    if (bytes_read > 0) {
      double sum_squares = 0;
      int sample_count = bytes_read / sizeof(int16_t);
      for (int i = 0; i < sample_count; i++) {
        sum_squares += pow(samples[i] / 32768.0f, 2);
      }
      float rms = sqrt(sum_squares / sample_count);
      float db_spl = (rms > 0) ? (94.0f + 20.0f * log10(rms / REFERENCE_RMS)) : 0;
      D_PRINTF("현재 소음 -> RMS: %.6f | dB: %.2f\n", rms, db_spl);

      if (digitalRead(PIR_PIN) == HIGH && db_spl > SOUND_DETECT_DB) {
        consecutive_high_count++;
      } else {
        consecutive_high_count = 0;
      }

      if (consecutive_high_count >= REQUIRED_CONSECUTIVE_HITS) {
        D_PRINTF("\n!!! %.1f dB 이상의 지속적인 소음 감지 (%.2f dB) !!!\n", SOUND_DETECT_DB, db_spl);
        deviceState.isRecording = true;
        consecutive_high_count = 0;
        
        recordAudio();
        createWavHeader(wav_header, AUDIO_DATA_SIZE);
        uploadWavFile();

        // 녹음 후 현재 센서 데이터도 함께 전송
        float temp = dht.getTemperature();
        float humi = dht.getHumidity();
        float co2 = MQ135.readSensor();
        uploadSensorData(temp, humi, co2 + 400);

        deviceState.isRecording = false;
      }
    }
  }
}

void createWavHeader(byte* header, uint32_t audioDataSize) {
    uint32_t fileSize = audioDataSize + WAV_HEADER_SIZE - 8;
    uint32_t byteRate = SAMPLE_RATE * NUM_CHANNELS * (BIT_DEPTH / 8);
    uint16_t blockAlign = NUM_CHANNELS * (BIT_DEPTH / 8);

    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    memcpy(&header[4], &fileSize, 4);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1; header[21] = 0;
    header[22] = NUM_CHANNELS; header[23] = 0;
    memcpy(&header[24], &SAMPLE_RATE, 4);
    memcpy(&header[28], &byteRate, 4);
    memcpy(&header[32], &blockAlign, 2);
    header[34] = BIT_DEPTH; header[35] = 0;
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    memcpy(&header[40], &audioDataSize, 4);
    
    D_PRINTLN("WAV 헤더 생성 완료.");
}

void recordAudio() {
    D_PRINTF("\n--- %d초 녹음 시작... ---\n", RECORD_SECONDS);
    
    size_t total_bytes_written = 0;
    const int i2s_read_buffer_size = 4096;
    uint8_t* i2s_read_buffer = (uint8_t*)malloc(i2s_read_buffer_size);

    if (i2s_read_buffer == NULL) {
        D_PRINTLN("I2S 읽기 버퍼 할당 실패!");
        return;
    }

    while (total_bytes_written < AUDIO_DATA_SIZE) {
        size_t bytes_read = 0;
        i2s_read(I2S_PORT, i2s_read_buffer, i2s_read_buffer_size, &bytes_read, pdMS_TO_TICKS(1000));
        
        if (bytes_read > 0) {
            size_t bytes_to_copy = (AUDIO_DATA_SIZE - total_bytes_written < bytes_read) ? 
                                   (AUDIO_DATA_SIZE - total_bytes_written) : bytes_read;
            
            memcpy((uint8_t*)audio_buffer_psram + total_bytes_written, i2s_read_buffer, bytes_to_copy);
            total_bytes_written += bytes_to_copy;
        }
    }
    free(i2s_read_buffer);
    D_PRINTLN("녹음 완료.");
    D_PRINTF("PSRAM에 저장된 총 바이트: %u / %u\n", total_bytes_written, AUDIO_DATA_SIZE);
}

void uploadWavFile() {
    D_PRINTLN("\n--- WAV 파일 업로드 시작 ---");
    WiFiClient client;
    
    if (!client.connect(upload_server, upload_port)) {
        D_PRINTLN("서버 연결 실패!");
        return;
    }
    D_PRINTLN("서버 연결 성공.");

    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String dateTime = getCurrentDateTime();
    String filename = String(device_id) + dateTime + ".wav";

    String head;
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"i\"\r\n\r\n" + String(device_id) + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"d\"\r\n\r\n" + dateTime + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"awfile\"; filename=\"" + filename + "\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";
    
    uint32_t contentLength = head.length() + WAV_HEADER_SIZE + AUDIO_DATA_SIZE + tail.length();

    client.println("POST " + String(upload_file_path) + " HTTP/1.1");
    client.println("Host: " + String(upload_server));
    client.println("Connection: close");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(contentLength));
    client.println();

    D_PRINTLN("페이로드 전송 중...");
    client.print(head);
    client.write((const byte*)wav_header, WAV_HEADER_SIZE);
    client.write((const byte*)audio_buffer_psram, AUDIO_DATA_SIZE);
    client.print(tail);
    D_PRINTLN("페이로드 전송 완료.");

    // 서버 응답 대기
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000) {
        delay(10);
    }
    
    D_PRINTLN("--- 서버 응답 ---");
    while(client.available()){
      String line = client.readStringUntil('\n');
      D_PRINTLN(line);
    }
    D_PRINTLN("-----------------");

    client.stop();
    D_PRINTLN("업로드 과정 종료.");
}

// Nextion 버튼으로 녹음 및 업로드 강제 실행
void forceRecordAndUpload() {
  D_PRINTLN("Nextion 요청: 녹음 및 업로드 실행");
  deviceState.isRecording = true;
  recordAudio();
  createWavHeader(wav_header, AUDIO_DATA_SIZE);
  uploadWavFile();
  deviceState.isRecording = false;
}