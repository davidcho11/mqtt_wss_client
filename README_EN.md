# MQTT WebSocket/SSL Client

A cross-platform MQTT client library supporting WebSocket and SSL/TLS protocols.

## Key Features

- **Multi-protocol Support**: WebSocket (WS/WSS), TCP (MQTT/MQTTS)
- **SSL/TLS Security**: Automatic certificate management and platform-specific certificate extraction
- **Cross-platform**: Windows, macOS, Linux support
- **Asynchronous Processing**: Event-based architecture
- **Auto-reconnection**: Automatic recovery on connection loss
- **Thread-safe**: Safe to use in multi-threaded environments

## Platform Differences Summary
| Feature | Windows | macOS | Linux |
|---------|------|---------|------|
| Certificate Store | Certificate Store | Keychain | /etc/ssl/certs |
| API | WinCrypt | Security Framework | File System |
| Extraction Method | API → PEM | API → PEM | Direct Path Usage |
| Link Libraries | crypt32.lib | -framework Security | None |

## Supported Protocols

| Protocol | Description | Command Options | Port | Security | Use Case |
|---------|------|---------|------|------|----------|
| WSS | WebSocket Secure |--ws --ssl | 8883, 443 | ✅ | Production (Recommended) |
| WS | WebSocket | --ws --no-ssl | 8080, 8083 | ❌ | Local Development |
| MQTTS | MQTT over SSL | --tcp --ssl | 8883 | ✅ | IoT Devices |
| MQTT | Plain TCP | --tcp --no-ssl | 1883 | ❌ | Internal Network |

## Build Requirements

### Dependencies
- **CMake** 3.15 or higher
- **C++17** compatible compiler
- **Eclipse Paho MQTT C** library
- **OpenSSL** 1.1 or higher

### Platform-specific Installation

#### macOS (Homebrew)
```bash
# Paho MQTT C library
brew install paho-mqtt-c

# OpenSSL
brew install openssl@3
```

#### Ubuntu/Debian
```bash
# Paho MQTT C library
sudo apt-get install libpaho-mqtt-dev

# OpenSSL
sudo apt-get install libssl-dev
```

#### Windows (vcpkg)
```bash
# Paho MQTT C library
vcpkg install paho-mqtt-c

# OpenSSL
vcpkg install openssl
```

## Build Instructions

```bash
# Navigate to project directory
cd mqtt_wss_client

# Create build directory
mkdir build && cd build

# Configure CMake
cmake ..

# Build
cmake --build .
```

## Usage

### Basic Usage Example

```cpp
#include "mqtt_client.h"
#include <iostream>

using namespace mqtt_client;

int main() {
    // Configuration
    MQTTConfig config;
    config.broker_host = "test.mosquitto.org";
    config.broker_port = 8883;
    config.client_id = "my_client";
    config.use_websockets = true;
    config.use_ssl = true;
    
    // Create event queue
    EventQueue event_queue;
    
    // Create MQTT client
    MQTTClient client(config, event_queue);
    
    // Run in separate thread
    std::thread mqtt_thread([&client]() {
        client.run();
    });
    
    // Process events
    while (true) {
        auto event = event_queue.pop(std::chrono::milliseconds(100));
        if (event.has_value()) {
            switch (event->type) {
                case EventType::CONNECTED:
                    std::cout << "Connected!" << std::endl;
                    client.request_subscribe("test/topic", 1);
                    break;
                case EventType::MESSAGE_ARRIVED:
                    std::cout << "Message received: " << event->payload << std::endl;
                    break;
                default:
                    break;
            }
        }
    }
    
    return 0;
}
```

### Command Line Test Program

After building, you can use the `mqtt_client_test` executable to test various protocols.

```bash
# WSS (WebSocket Secure) - Default
./mqtt_client_test test.mosquitto.org 8883

# WS (WebSocket without SSL) - Insecure
./mqtt_client_test --no-ssl test.mosquitto.org 8080

# MQTTS (MQTT over SSL)
./mqtt_client_test --tcp --ssl broker.hivemq.com 8883

# MQTT (Plain TCP) - Insecure
./mqtt_client_test --tcp --no-ssl test.mosquitto.org 1883

# Use custom certificate
./mqtt_client_test --cert ca.crt broker.example.com 8883
```

## API Reference

### MQTTConfig Structure

```cpp
struct MQTTConfig {
    std::string broker_host;                    // Broker host
    int broker_port = 8883;                     // Broker port
    std::string client_id;                      // Client ID
    std::optional<std::string> username;        // Username (optional)
    std::optional<std::string> password;        // Password (optional)
    std::string websocket_path = "/mqtt";       // WebSocket path
    int keep_alive_seconds = 20;                // Keep-alive interval
    int qos = 1;                                // Default QoS
    bool use_websockets = true;                 // Use WebSocket
    bool use_ssl = true;                        // Use SSL/TLS
    std::optional<std::string> cert_file_path;  // Certificate file path
};
```

### MQTTClient Class

```cpp
class MQTTClient {
public:
    // Constructor
    explicit MQTTClient(const MQTTConfig& config, EventQueue& event_queue);
    
    // Main function to run in thread
    void run();
    
    // Request thread stop
    void stop();
    
    // Check connection status
    bool is_connected() const;
    
    // MQTT operation requests (thread-safe)
    void request_subscribe(const std::string& topic, int qos = 1);
    void request_publish(const std::string& topic, const std::string& payload, 
                        int qos = 1, bool retained = false);
    void request_unsubscribe(const std::string& topic);
};
```

### EventQueue Class

```cpp
class EventQueue {
public:
    // Add event
    void push(MQTTEvent event);
    
    // Wait for event (with timeout)
    std::optional<MQTTEvent> pop(std::chrono::milliseconds timeout);
    
    // Get event immediately
    std::optional<MQTTEvent> try_pop();
    
    // Check queue status
    bool empty() const;
    size_t size() const;
};
```

## Event Types

```cpp
enum class EventType {
    CONNECTED,           // Connection successful
    CONNECTION_LOST,     // Connection lost
    MESSAGE_ARRIVED,     // Message received
    DELIVERY_COMPLETE,   // Message delivery complete
    SUBSCRIBE_SUCCESS,   // Subscribe successful
    SUBSCRIBE_FAILURE,   // Subscribe failed
    PUBLISH_SUCCESS,     // Publish successful
    PUBLISH_FAILURE,     // Publish failed
    ERROR                // Error occurred
};
```

## SSL/TLS Certificate Management

This library automatically extracts system certificates on a per-platform basis:

- **Windows**: Extracts ROOT, CA certificates from Windows Certificate Store
- **macOS**: Extracts anchor certificates from Keychain
- **Linux**: Uses system certificate path (`/etc/ssl/certs/`)

To use custom certificates, set the PEM format certificate file path in `MQTTConfig::cert_file_path`.

## Important Notes

1. **Thread Safety**: The `request_*` methods of `MQTTClient` are thread-safe, but the `run()` method must be executed in a separate thread.

2. **Memory Management**: The `MQTTClient` object must remain valid until the `run()` method completes.

3. **SSL Disable**: Setting `use_ssl = false` creates an insecure connection. Do not use in production environments.

4. **Auto-reconnection**: Automatically attempts to reconnect when the connection is lost. Subscriptions are not automatically restored on reconnection, so you may need to resubscribe if necessary.

## License

This project is distributed under the MIT License.

## Contributing

Bug reports, feature requests, and pull requests are welcome. Please create an issue before contributing.

## Troubleshooting

### Common Issues

1. **Build Failure**: Check if dependency libraries are properly installed.
2. **SSL Connection Failure**: Verify certificate file path and format.
3. **WebSocket Connection Failure**: Check if the broker supports WebSocket.

### Debugging

To enable debug information during build:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

You can check connection status and errors through log output.