#include "mqtt_client.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>

namespace mqtt_client {

MQTTClient::MQTTClient(const MQTTConfig& config, EventQueue& event_queue)
    : config_(config), event_queue_(event_queue), client_(nullptr) {
    
    if (config_.client_id.empty()) {
        config_.client_id = "mqtt_client_" + 
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }
}

MQTTClient::~MQTTClient() {
    stop();
}

std::string MQTTClient::extract_windows_certificates() {
#ifdef _WIN32
    std::ostringstream pem_stream;
    const char* store_names[] = {"ROOT", "CA"};
    
    for (const auto& store_name : store_names) {
        HCERTSTORE hStore = CertOpenSystemStoreA(0, store_name);
        if (!hStore) {
            std::cerr << "Failed to open certificate store: " << store_name << std::endl;
            continue;
        }

        PCCERT_CONTEXT pContext = nullptr;
        while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
            DWORD pem_size = 0;
            if (CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded,
                                     CRYPT_STRING_BASE64HEADER, nullptr, &pem_size)) {
                std::vector<char> pem_buffer(pem_size);
                if (CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded,
                                         CRYPT_STRING_BASE64HEADER, pem_buffer.data(), &pem_size)) {
                    pem_stream << pem_buffer.data();
                }
            }
        }
        CertCloseStore(hStore, 0);
    }
    return pem_stream.str();
#else
    return "";
#endif
}

std::string MQTTClient::setup_ssl_cert() {
    // 1. 사용자 지정 인증서 파일이 있으면 우선 사용
    if (config_.cert_file_path.has_value() && fs::exists(config_.cert_file_path.value())) {
        std::cout << "[SSL] Using provided certificate file: " << config_.cert_file_path.value() << std::endl;
        return config_.cert_file_path.value();
    }
    
    // 2. Windows 시스템 인증서 추출
    std::cout << "[SSL] Extracting Windows system certificates..." << std::endl;
    std::string pem_certs = extract_windows_certificates();
    
    if (pem_certs.empty()) {
        throw std::runtime_error("Failed to extract Windows certificates and no cert file provided");
    }
    
    // 임시 파일에 저장
    fs::path temp_dir = fs::temp_directory_path();
    temp_cert_file_ = (temp_dir / ("mqtt_certs_" + config_.client_id + ".pem")).string();
    
    std::ofstream cert_file(temp_cert_file_, std::ios::binary);
    if (!cert_file) {
        throw std::runtime_error("Failed to create temporary certificate file");
    }
    cert_file << pem_certs;
    cert_file.close();
    
    std::cout << "[SSL] Temporary certificate file created: " << temp_cert_file_ << std::endl;
    return temp_cert_file_;
}

bool MQTTClient::connect_to_broker() {
    // Server URI 생성
    std::string server_uri;
    if (config_.use_websockets) {
        server_uri = "wss://" + config_.broker_host + ":" + 
                     std::to_string(config_.broker_port) + config_.websocket_path;
    } else {
        server_uri = "ssl://" + config_.broker_host + ":" + std::to_string(config_.broker_port);
    }
    
    std::cout << "[MQTT] Creating client: " << server_uri << std::endl;
    
    // MQTT 클라이언트 생성
    int rc = MQTTAsync_create(&client_, server_uri.c_str(), config_.client_id.c_str(),
                              MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to create MQTT client"));
        return false;
    }
    
    // 콜백 설정
    rc = MQTTAsync_setCallbacks(client_, this, on_connection_lost, 
                                on_message_arrived, on_delivery_complete);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to set callbacks"));
        MQTTAsync_destroy(&client_);
        return false;
    }
    
    // SSL 설정
    std::string cert_file = setup_ssl_cert();
    
    MQTTAsync_SSLOptions ssl_opts = MQTTAsync_SSLOptions_initializer;
    ssl_opts.trustStore = cert_file.c_str();
    ssl_opts.enableServerCertAuth = 1;
    
    // 연결 옵션 설정
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval = config_.keep_alive_seconds;
    conn_opts.cleansession = 1;
    conn_opts.automaticReconnect = 1;  // 자동 재연결 활성화
    conn_opts.minRetryInterval = config_.min_retry_interval;
    conn_opts.maxRetryInterval = config_.max_retry_interval;
    conn_opts.ssl = &ssl_opts;
    conn_opts.onSuccess = on_connect_success;
    conn_opts.onFailure = on_connect_failure;
    conn_opts.context = this;
    
    if (config_.username.has_value()) {
        conn_opts.username = config_.username.value().c_str();
    }
    if (config_.password.has_value()) {
        conn_opts.password = config_.password.value().c_str();
    }
    
    std::cout << "[MQTT] Connecting to broker..." << std::endl;
    rc = MQTTAsync_connect(client_, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to start connect"));
        MQTTAsync_destroy(&client_);
        return false;
    }
    
    return true;
}

void MQTTClient::disconnect_from_broker() {
    if (client_) {
        MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
        disc_opts.timeout = 1000;
        MQTTAsync_disconnect(client_, &disc_opts);
        MQTTAsync_destroy(&client_);
        client_ = nullptr;
    }
    
    // 임시 인증서 파일 삭제
    if (!temp_cert_file_.empty() && fs::exists(temp_cert_file_)) {
        try {
            fs::remove(temp_cert_file_);
            std::cout << "[SSL] Temporary certificate file removed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[SSL] Failed to remove temporary certificate file: " << e.what() << std::endl;
        }
    }
}

void MQTTClient::run() {
    std::cout << "[Thread] MQTT thread started" << std::endl;
    
    if (!connect_to_broker()) {
        std::cerr << "[Thread] Failed to connect to broker" << std::endl;
        return;
    }
    
    // 메인 루프
    while (!should_stop_.load()) {
        process_requests();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[Thread] Disconnecting..." << std::endl;
    disconnect_from_broker();
    connected_.store(false);
    std::cout << "[Thread] MQTT thread stopped" << std::endl;
}

void MQTTClient::stop() {
    std::cout << "[Thread] Stop requested" << std::endl;
    should_stop_.store(true);
}

void MQTTClient::process_requests() {
    std::lock_guard<std::mutex> lock(work_mutex_);
    
    while (!work_queue_.empty() && connected_.load()) {
        auto item = work_queue_.front();
        work_queue_.pop();
        
        switch (item.type) {
            case WorkItem::Type::SUBSCRIBE: {
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.onSuccess = on_subscribe_success;
                opts.onFailure = on_subscribe_failure;
                opts.context = this;
                
                int rc = MQTTAsync_subscribe(client_, item.topic.c_str(), item.qos, &opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_FAILURE, 
                                               "Subscribe request failed: " + item.topic));
                }
                break;
            }
            case WorkItem::Type::PUBLISH: {
                MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
                pubmsg.payload = const_cast<char*>(item.payload.c_str());
                pubmsg.payloadlen = static_cast<int>(item.payload.length());
                pubmsg.qos = item.qos;
                pubmsg.retained = item.retained;
                
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.onSuccess = on_send_success;
                opts.onFailure = on_send_failure;
                opts.context = this;
                
                int rc = MQTTAsync_sendMessage(client_, item.topic.c_str(), &pubmsg, &opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    event_queue_.push(MQTTEvent(EventType::PUBLISH_FAILURE,
                                               "Publish request failed: " + item.topic));
                }
                break;
            }
            case WorkItem::Type::UNSUBSCRIBE: {
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.context = this;
                MQTTAsync_unsubscribe(client_, item.topic.c_str(), &opts);
                break;
            }
        }
    }
}

void MQTTClient::request_subscribe(const std::string& topic, int qos) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::SUBSCRIBE;
    item.topic = topic;
    item.qos = qos;
    work_queue_.push(item);
}

void MQTTClient::request_publish(const std::string& topic, const std::string& payload,
                                 int qos, bool retained) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::PUBLISH;
    item.topic = topic;
    item.payload = payload;
    item.qos = qos;
    item.retained = retained;
    work_queue_.push(item);
}

void MQTTClient::request_unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::UNSUBSCRIBE;
    item.topic = topic;
    work_queue_.push(item);
}

// ============================================================================
// 콜백 함수들
// ============================================================================

void MQTTClient::on_connection_lost(void* context, char* cause) {
    auto* client = static_cast<MQTTClient*>(context);
    client->connected_.store(false);
    std::string cause_str = cause ? std::string(cause) : "Unknown";
    client->event_queue_.push(MQTTEvent(EventType::CONNECTION_LOST, cause_str));
    std::cout << "[Callback] Connection lost: " << cause_str << std::endl;
}

int MQTTClient::on_message_arrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message) {
    auto* client = static_cast<MQTTClient*>(context);
    
    std::string topic(topicName);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    client->event_queue_.push(MQTTEvent(EventType::MESSAGE_ARRIVED, topic, payload, message->qos));
    
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

void MQTTClient::on_delivery_complete(void* context, MQTTAsync_token token) {
    auto* client = static_cast<MQTTClient*>(context);
    MQTTEvent event(EventType::DELIVERY_COMPLETE);
    event.token = token;
    client->event_queue_.push(event);
}

void MQTTClient::on_connect_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->connected_.store(true);
    client->event_queue_.push(MQTTEvent(EventType::CONNECTED, "Connected to broker"));
    std::cout << "[Callback] Connected successfully" << std::endl;
}

void MQTTClient::on_connect_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ? 
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::ERROR, "Connection failed: " + error_msg));
    std::cerr << "[Callback] Connection failed: " << error_msg << std::endl;
}

void MQTTClient::on_subscribe_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_SUCCESS, "Subscription successful"));
}

void MQTTClient::on_subscribe_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ?
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_FAILURE, "Subscribe failed: " + error_msg));
}

void MQTTClient::on_send_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->event_queue_.push(MQTTEvent(EventType::PUBLISH_SUCCESS, "Message published"));
}

void MQTTClient::on_send_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ?
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::PUBLISH_FAILURE, "Publish failed: " + error_msg));
}

} // namespace mqtt_client
