// audio.cpp

#include "config.h"
#include "audio.h"
#include "network.h" // getCurrentDateTime(), uploadSensorData() 호출을 위해 포함

// Shine MP3 인코더 라이브러리 (C언어) 포함
extern "C" {
  #include "layer3.h"
}

unsigned long last_sound_check = 0;
int consecutive_high_count = 0;

// 새로운 실시간 녹음 및 업로드 함수
void realtimeRecordAndUpload();

// 오디오 Task 함수
void audio_task_function(void *pvParameters) {
  for (;;) {
    // 세마포를 받을 때까지 무한정 대기
    if (xSemaphoreTake(audioSemaphore, portMAX_DELAY) == pdTRUE) {
      D_PRINTLN("[Audio Task] 세마포 수신. 실시간 녹음 및 업로드 시작.");
      deviceState.isRecording = true;

      realtimeRecordAndUpload(); // 새로운 실시간 함수 호출

      // 녹음 후 현재 센서 데이터도 함께 전송
      float temp = dht.getTemperature();
      float humi = dht.getHumidity();
      float co2 = MQ135.readSensor();
      uploadSensorData(temp, humi, co2 + 400);

      deviceState.isRecording = false;
      D_PRINTLN("[Audio Task] 작업 완료.");
    }
  }
}

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
  // PSRAM 버퍼 할당 코드 제거. 더 이상 큰 버퍼가 필요 없음.
  initI2S();
  i2s_start(I2S_PORT); // 소음 감지를 위해 I2S를 계속 켜 둡니다.
  D_PRINTLN("실시간 오디오 처리를 위해 I2S 초기화 완료.");
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
        consecutive_high_count = 0;
        
        xSemaphoreGive(audioSemaphore);
      }
    }
  }
}

void realtimeRecordAndUpload() {
    D_PRINTLN("\n--- 실시간 MP3 인코딩 및 업로드 시작 ---");

    // 1. Shine MP3 인코더 초기화
    shine_config_t config;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.wave.samplerate = SAMPLE_RATE;
    config.wave.channels = PCM_MONO;
    config.mpeg.bitr = MP3_BITRATE;
    config.mpeg.mode = MONO;
    
    if (shine_check_config(config.wave.samplerate, config.mpeg.bitr) < 0) {
        D_PRINTLN("지원되지 않는 샘플레이트/비트레이트 설정입니다.");
        return;
    }
    shine_t s = shine_initialise(&config);
    if (!s) {
        D_PRINTLN("Shine 인코더 초기화 실패.");
        return;
    }

    // 2. 네트워크 클라이언트 연결
    WiFiClient client;
    if (!client.connect(upload_server, upload_port)) {
        D_PRINTLN("서버 연결 실패!");
        shine_close(s);
        return;
    }
    D_PRINTLN("서버 연결 성공.");

    // 3. HTTP 헤더 전송 (Chunked-Encoding은 서버 지원이 필요하므로, 예상 길이를 보내는 방식으로 우선 구현)
    // 예상 MP3 크기 계산 (정확하지 않을 수 있음, 헤더 전송을 위해 대략적으로 계산)
    // (샘플레이트 * 시간 * 비트레이트) / 8 / 압축률(대략 11)
    uint32_t estimated_mp3_size = (SAMPLE_RATE * RECORD_SECONDS * MP3_BITRATE) / 8 / 11;

    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String dateTime = getCurrentDateTime();
    String filename = String(device_id) + dateTime + ".mp3";

    String head;
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"i\"\r\n\r\n" + String(device_id) + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"d\"\r\n\r\n" + dateTime + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"awfile\"; filename=\"" + filename + "\"\r\n";
    head += "Content-Type: audio/mpeg\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    
    // Content-Length를 지금 알 수 없으므로, 스트리밍을 위해 헤더를 나중에 보내거나 Chunked-Encoding을 사용해야 함.
    // 여기서는 먼저 연결하고, 데이터를 모은 뒤 길이를 계산해서 보내는 방식을 유지하되, 메모리 버퍼 대신 직접 전송 로직으로 변경.
    // 하지만 HTTP/1.1은 Content-Length 없이 Chunked transfer를 지원하지만, 구현이 복잡해짐.
    // 여기서는 우선 MP3 데이터를 임시 버퍼에 모았다가 한번에 보내는 방식으로 다시 구현합니다.
    // 실시간 스트리밍의 진정한 이점을 얻으려면 서버 측에서 스트림을 직접 받을 수 있어야 합니다.
    
    uint8_t* mp3_buffer = (uint8_t*)malloc(MP3_BUFFER_SIZE);
    if (!mp3_buffer) {
        D_PRINTLN("MP3 버퍼를 위한 메모리 할당 실패.");
        client.stop();
        shine_close(s);
        return;
    }

    int samples_per_pass = shine_samples_per_pass(s);
    int16_t pcm_buffer[samples_per_pass];
    size_t total_samples_read = 0;
    size_t total_samples_to_read = SAMPLE_RATE * RECORD_SECONDS;
    int mp3_bytes_written = 0;

    D_PRINTF("%d초 동안 녹음 및 인코딩 진행...\n", RECORD_SECONDS);

    while (total_samples_read < total_samples_to_read) {
        size_t bytes_read = 0;
        i2s_read(I2S_PORT, (char*)pcm_buffer, sizeof(pcm_buffer), &bytes_read, portMAX_DELAY);

        if (bytes_read > 0) {
            int samples_read = bytes_read / sizeof(int16_t);
            total_samples_read += samples_read;

            unsigned char *data;
            int len;
            int16_t* pcm_ptr = pcm_buffer;
            data = shine_encode_buffer(s, &pcm_ptr, &len);

            if (len > 0) {
                if (mp3_bytes_written + len > MP3_BUFFER_SIZE) {
                    D_PRINTLN("MP3 버퍼 오버플로우!");
                    break;
                }
                memcpy(mp3_buffer + mp3_bytes_written, data, len);
                mp3_bytes_written += len;
            }
        }
    }

    // 마지막 남은 데이터 플러시
    unsigned char *flushed_data;
    int flushed_len;
    flushed_data = shine_flush(s, &flushed_len);
    if (flushed_len > 0) {
        if (mp3_bytes_written + flushed_len <= MP3_BUFFER_SIZE) {
            memcpy(mp3_buffer + mp3_bytes_written, flushed_data, flushed_len);
            mp3_bytes_written += flushed_len;
        }
    }
    
    D_PRINTF("인코딩 완료. 총 MP3 크기: %d bytes\n", mp3_bytes_written);

    // 이제 전체 크기를 알았으므로 헤더와 함께 전송
    uint32_t contentLength = head.length() + mp3_bytes_written + tail.length();

    client.println("POST " + String(upload_file_path) + " HTTP/1.1");
    client.println("Host: " + String(upload_server));
    client.println("Connection: close");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(contentLength));
    client.println();

    D_PRINTLN("페이로드 전송 중...");
    client.print(head);
    client.write((const byte*)mp3_buffer, mp3_bytes_written);
    client.print(tail);
    D_PRINTLN("페이로드 전송 완료.");

    // 서버 응답 대기 및 출력
    unsigned long timeout = millis();
    while (!client.available() && millis() - timeout < 5000) { delay(10); }
    D_PRINTLN("--- 서버 응답 ---");
    while(client.available()){
      String line = client.readStringUntil('\n');
      D_PRINTLN(line);
    }
    D_PRINTLN("-----------------");

    // 리소스 정리
    free(mp3_buffer);
    client.stop();
    shine_close(s);
    D_PRINTLN("업로드 과정 종료.");
}

// Nextion 버튼으로 녹음 및 업로드 강제 실행
void forceRecordAndUpload() {
  D_PRINTLN("Nextion 요청: 녹음 및 업로드 신호 전송");
  xSemaphoreGive(audioSemaphore);
}