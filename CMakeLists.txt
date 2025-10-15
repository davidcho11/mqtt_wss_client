# cmake_minimum_required(VERSION 3.15)
# project(MQTTWSClient CXX)

# # C++17 표준
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)

# # Windows 전용
# # if(NOT WIN32)
# #     message(FATAL_ERROR "This project is designed for Windows only")
# # endif()

# # 컴파일러 옵션
# if(MSVC)
#     add_compile_options(/W4 /utf-8)
#     add_definitions(-D_WIN32_WINNT=0x0601)
# else()
#     add_compile_options(-Wall -Wextra -Wpedantic)
# endif()

# # Paho MQTT C 라이브러리
# find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)

# # OpenSSL
# find_package(OpenSSL REQUIRED)

# # 라이브러리
# add_library(mqtt_wss_client STATIC
#     src/event_queue.h
#     src/mqtt_client.h
#     src/mqtt_client.cpp
# )

# target_include_directories(mqtt_wss_client PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}
# )

# target_link_libraries(mqtt_wss_client PUBLIC
#     eclipse-paho-mqtt-c::paho-mqtt3as-static
#     OpenSSL::SSL
#     OpenSSL::Crypto
#     crypt32
# )

# # 실행 파일
# add_executable(mqtt_client_test src/main.cpp)
# target_link_libraries(mqtt_client_test PRIVATE mqtt_wss_client)
###################################################################
###################################################################
cmake_minimum_required(VERSION 3.15)
project(MQTTWSClient CXX)

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 컴파일러 옵션
if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_definitions(-D_WIN32_WINNT=0x0601)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ----- macOS 전용(OpenSSL 힌트) ---------------------------------------------
# Homebrew의 OpenSSL 경로를 자동 힌트(존재할 때만 적용)
if(APPLE)
    # Apple Silicon
    if(EXISTS "/opt/homebrew/opt/openssl@3")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3" CACHE PATH "OpenSSL root" FORCE)
    endif()
    # Intel Mac
    if(EXISTS "/usr/local/opt/openssl@3")
        list(PREPEND CMAKE_PREFIX_PATH "/usr/local/opt/openssl@3")
        set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@3" CACHE PATH "OpenSSL root" FORCE)
    endif()
endif()

# ----- Dependencies ----------------------------------------------------------
# Paho MQTT C (CONFIG 모드). Homebrew 등에서 설치 시 Config 패키지가 제공됨.
#   - SSL + async 클라이언트: paho-mqtt3as / paho-mqtt3as-static
find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)

# OpenSSL
find_package(OpenSSL REQUIRED)

# POSIX 스레드
find_package(Threads REQUIRED)

# ----- 라이브러리 타깃 선택(정적/공유 자동 감지) ----------------------------
set(PAHO_SSL_TARGET "")
if(TARGET eclipse-paho-mqtt-c::paho-mqtt3as-static)
    set(PAHO_SSL_TARGET eclipse-paho-mqtt-c::paho-mqtt3as-static)
elseif(TARGET eclipse-paho-mqtt-c::paho-mqtt3as)
    set(PAHO_SSL_TARGET eclipse-paho-mqtt-c::paho-mqtt3as)
else()
    message(FATAL_ERROR "Paho MQTT C SSL target not found (expected paho-mqtt3as[-static]).")
endif()

# ----- 라이브러리 -----------------------------------------------------------
add_library(mqtt_wss_client STATIC
    src/event_queue.h
    src/mqtt_client.h
    src/mqtt_client.cpp
)

target_include_directories(mqtt_wss_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mqtt_wss_client
    PUBLIC
        ${PAHO_SSL_TARGET}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

# Windows 전용 라이브러리(crypt32)는 윈도우에서만 링크
if(WIN32)
    target_link_libraries(mqtt_wss_client PUBLIC crypt32)
endif()

# ----- 실행 파일 ------------------------------------------------------------
add_executable(mqtt_client_test src/main.cpp)
target_link_libraries(mqtt_client_test PRIVATE mqtt_wss_client)

# ----- 빌드 출력 정리(선택) -------------------------------------------------
# macOS/Unix에서 정적 라이브러리 PIC 필요 시(대부분 기본값이지만 보장하려면):
set_target_properties(mqtt_wss_client PROPERTIES POSITION_INDEPENDENT_CODE ON)

