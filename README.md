# MQTT WebSocket/SSL 클라이언트

크로스 플랫폼 MQTT 클라이언트 라이브러리로, WebSocket과 SSL/TLS를 지원합니다.

## 주요 기능

- **다중 프로토콜 지원**: WebSocket (WS/WSS), TCP (MQTT/MQTTS)
- **SSL/TLS 보안**: 자동 인증서 관리 및 플랫폼별 인증서 추출
- **크로스 플랫폼**: Windows, macOS, Linux 지원
- **비동기 처리**: 이벤트 기반 아키텍처
- **자동 재연결**: 연결 끊김 시 자동 복구
- **스레드 안전**: 멀티스레드 환경에서 안전한 사용

## 지원 프로토콜

| 프로토콜 | 설명 | 명령옵션 | 포트 | 보안 | 사용 사례 |
|---------|------|---------|------|------|----------|
| WSS | WebSocket Secure |--ws --ssl | 8883, 443 | ✅ | 프로덕션 (권장) |
| WS | WebSocket | --ws --no-ssl | 8080, 8083 | ❌ | 로컬 개발 |
| MQTTS | MQTT over SSL | --tcp --ssl | 8883 | ✅ | IoT 디바이스 |
| MQTT | Plain TCP | --tcp --no-ssl | 1883 | ❌ | 내부 네트워크 |

## 빌드 요구사항

### 의존성
- **CMake** 3.15 이상
- **C++17** 지원 컴파일러
- **Eclipse Paho MQTT C** 라이브러리
- **OpenSSL** 1.1 이상

### 플랫폼별 설치

#### macOS (Homebrew)
```bash
# Paho MQTT C 라이브러리
brew install paho-mqtt-c

# OpenSSL
brew install openssl@3
```

#### Ubuntu/Debian
```bash
# Paho MQTT C 라이브러리
sudo apt-get install libpaho-mqtt-dev

# OpenSSL
sudo apt-get install libssl-dev
```

#### Windows (vcpkg)
```bash
# Paho MQTT C 라이브러리
vcpkg install paho-mqtt-c

# OpenSSL
vcpkg install openssl
```

## 빌드 방법

```bash
# 프로젝트 디렉토리로 이동
cd mqtt_wss_client

# 빌드 디렉토리 생성
mkdir build && cd build

# CMake 설정
cmake ..

# 빌드
cmake --build .
```

## 사용법

### 기본 사용 예제

```cpp
#include "mqtt_client.h"
#include <iostream>

using namespace mqtt_client;

int main() {
    // 설정
    MQTTConfig config;
    config.broker_host = "test.mosquitto.org";
    config.broker_port = 8883;
    config.client_id = "my_client";
    config.use_websockets = true;
    config.use_ssl = true;
    
    // 이벤트 큐 생성
    EventQueue event_queue;
    
    // MQTT 클라이언트 생성
    MQTTClient client(config, event_queue);
    
    // 별도 스레드에서 실행
    std::thread mqtt_thread([&client]() {
        client.run();
    });
    
    // 이벤트 처리
    while (true) {
        auto event = event_queue.pop(std::chrono::milliseconds(100));
        if (event.has_value()) {
            switch (event->type) {
                case EventType::CONNECTED:
                    std::cout << "연결됨!" << std::endl;
                    client.request_subscribe("test/topic", 1);
                    break;
                case EventType::MESSAGE_ARRIVED:
                    std::cout << "메시지 수신: " << event->payload << std::endl;
                    break;
                default:
                    break;
            }
        }
    }
    
    return 0;
}
```

### 명령행 테스트 프로그램

빌드 후 `mqtt_client_test` 실행 파일을 사용하여 다양한 프로토콜을 테스트할 수 있습니다.

```bash
# WSS (WebSocket Secure) - 기본
./mqtt_client_test test.mosquitto.org 8883

# WS (WebSocket without SSL) - 비보안
./mqtt_client_test --no-ssl test.mosquitto.org 8080

# MQTTS (MQTT over SSL)
./mqtt_client_test --tcp --ssl broker.hivemq.com 8883

# MQTT (Plain TCP) - 비보안
./mqtt_client_test --tcp --no-ssl test.mosquitto.org 1883

# 사용자 정의 인증서 사용
./mqtt_client_test --cert ca.crt broker.example.com 8883
```

## API 참조

### MQTTConfig 구조체

```cpp
struct MQTTConfig {
    std::string broker_host;                    // 브로커 호스트
    int broker_port = 8883;                     // 브로커 포트
    std::string client_id;                      // 클라이언트 ID
    std::optional<std::string> username;        // 사용자명 (선택)
    std::optional<std::string> password;        // 비밀번호 (선택)
    std::string websocket_path = "/mqtt";       // WebSocket 경로
    int keep_alive_seconds = 20;                // Keep-alive 간격
    int qos = 1;                                // 기본 QoS
    bool use_websockets = true;                 // WebSocket 사용 여부
    bool use_ssl = true;                        // SSL/TLS 사용 여부
    std::optional<std::string> cert_file_path;  // 인증서 파일 경로
};
```

### MQTTClient 클래스

```cpp
class MQTTClient {
public:
    // 생성자
    explicit MQTTClient(const MQTTConfig& config, EventQueue& event_queue);
    
    // 스레드에서 실행될 메인 함수
    void run();
    
    // 스레드 중지 요청
    void stop();
    
    // 연결 상태 확인
    bool is_connected() const;
    
    // MQTT 작업 요청 (스레드 안전)
    void request_subscribe(const std::string& topic, int qos = 1);
    void request_publish(const std::string& topic, const std::string& payload, 
                        int qos = 1, bool retained = false);
    void request_unsubscribe(const std::string& topic);
};
```

### EventQueue 클래스

```cpp
class EventQueue {
public:
    // 이벤트 추가
    void push(MQTTEvent event);
    
    // 이벤트 대기 (타임아웃 지원)
    std::optional<MQTTEvent> pop(std::chrono::milliseconds timeout);
    
    // 이벤트 즉시 가져오기
    std::optional<MQTTEvent> try_pop();
    
    // 큐 상태 확인
    bool empty() const;
    size_t size() const;
};
```

## 이벤트 타입

```cpp
enum class EventType {
    CONNECTED,           // 연결 성공
    CONNECTION_LOST,     // 연결 끊김
    MESSAGE_ARRIVED,     // 메시지 수신
    DELIVERY_COMPLETE,   // 메시지 전송 완료
    SUBSCRIBE_SUCCESS,   // 구독 성공
    SUBSCRIBE_FAILURE,   // 구독 실패
    PUBLISH_SUCCESS,     // 발행 성공
    PUBLISH_FAILURE,     // 발행 실패
    ERROR                // 오류 발생
};
```

## SSL/TLS 인증서 관리

이 라이브러리는 플랫폼별로 자동으로 시스템 인증서를 추출합니다:

- **Windows**: Windows Certificate Store에서 ROOT, CA 인증서 추출
- **macOS**: Keychain에서 anchor 인증서 추출
- **Linux**: 시스템 인증서 경로 사용 (`/etc/ssl/certs/`)

사용자 정의 인증서를 사용하려면 `MQTTConfig::cert_file_path`에 PEM 형식 인증서 파일 경로를 설정하세요.

## 주의사항

1. **스레드 안전성**: `MQTTClient`의 `request_*` 메서드는 스레드 안전하지만, `run()` 메서드는 별도 스레드에서 실행해야 합니다.

2. **메모리 관리**: `MQTTClient` 객체는 `run()` 메서드가 완료될 때까지 유효해야 합니다.

3. **SSL 비활성화**: `use_ssl = false`로 설정하면 보안되지 않은 연결이 생성됩니다. 프로덕션 환경에서는 사용하지 마세요.

4. **자동 재연결**: 연결이 끊어지면 자동으로 재연결을 시도합니다. 재연결 시 구독은 자동으로 복원되지 않으므로 필요시 다시 구독해야 합니다.

## 라이선스

이 프로젝트는 MIT 라이선스 하에 배포됩니다.

## 기여하기

버그 리포트, 기능 요청, 풀 리퀘스트를 환영합니다. 기여하기 전에 이슈를 먼저 생성해 주세요.

## 문제 해결

### 일반적인 문제

1. **빌드 실패**: 의존성 라이브러리가 올바르게 설치되었는지 확인하세요.
2. **SSL 연결 실패**: 인증서 파일 경로와 형식을 확인하세요.
3. **WebSocket 연결 실패**: 브로커가 WebSocket을 지원하는지 확인하세요.

### 디버깅

빌드 시 디버그 정보를 활성화하려면:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

로그 출력을 통해 연결 상태와 오류를 확인할 수 있습니다.
