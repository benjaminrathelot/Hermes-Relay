cmake_minimum_required(VERSION 3.20)
project(hermes-relay C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HERMES_BUILD_TESTS "Build Hermes Relay unit tests" ON)
option(HERMES_BUILD_FUZZ "Build Hermes Relay fuzz harnesses" OFF)
option(HERMES_ENABLE_HARDENING "Enable compiler hardening flags" ON)
option(HERMES_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(OpenSSL REQUIRED)

set(HERMES_PLATFORM_SOURCE src/platform_posix.c)
if(WIN32)
    set(HERMES_PLATFORM_SOURCE src/platform_win32.c)
endif()

set(HERMES_COMMON_CFLAGS -Wall -Wextra -Wpedantic)
if(HERMES_ENABLE_HARDENING)
    list(APPEND HERMES_COMMON_CFLAGS -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fno-omit-frame-pointer)
endif()
if(HERMES_ENABLE_SANITIZERS)
    list(APPEND HERMES_COMMON_CFLAGS -fsanitize=address,undefined)
endif()

set(HERMES_LIBRARY_SOURCES
    ${HERMES_PLATFORM_SOURCE}
    src/util.c
    src/api.c
    src/log.c
    src/identity.c
    src/crypto_openssl.c
    src/envelope.c
    src/store_fs.c
    src/bundle.c
    src/relay.c
    src/sync.c
    src/transport_tcp.c
)

add_library(hermesrelay STATIC ${HERMES_LIBRARY_SOURCES})
add_library(hermesrelay_shared SHARED ${HERMES_LIBRARY_SOURCES})
set_target_properties(hermesrelay_shared PROPERTIES OUTPUT_NAME hermesrelay)

target_include_directories(hermesrelay PUBLIC include)
target_link_libraries(hermesrelay PUBLIC OpenSSL::Crypto)
target_compile_options(hermesrelay PRIVATE ${HERMES_COMMON_CFLAGS})
target_include_directories(hermesrelay_shared PUBLIC include)
target_link_libraries(hermesrelay_shared PUBLIC OpenSSL::Crypto)
target_compile_options(hermesrelay_shared PRIVATE ${HERMES_COMMON_CFLAGS})
target_compile_definitions(hermesrelay_shared PRIVATE HERMES_API_BUILD)
if(HERMES_ENABLE_SANITIZERS)
    target_link_options(hermesrelay PUBLIC -fsanitize=address,undefined)
    target_link_options(hermesrelay_shared PUBLIC -fsanitize=address,undefined)
endif()

add_executable(hermes-cli src/cli.c)
target_link_libraries(hermes-cli PRIVATE hermesrelay)
target_compile_options(hermes-cli PRIVATE ${HERMES_COMMON_CFLAGS})
if(HERMES_ENABLE_SANITIZERS)
    target_link_options(hermes-cli PRIVATE -fsanitize=address,undefined)
endif()

add_executable(hermes-sim src/sim.c)
target_link_libraries(hermes-sim PRIVATE hermesrelay)
target_compile_options(hermes-sim PRIVATE ${HERMES_COMMON_CFLAGS})
if(HERMES_ENABLE_SANITIZERS)
    target_link_options(hermes-sim PRIVATE -fsanitize=address,undefined)
endif()

if(HERMES_BUILD_TESTS)
    enable_testing()
    add_executable(hermes-test tests/test_main.c)
    target_link_libraries(hermes-test PRIVATE hermesrelay)
    target_compile_options(hermes-test PRIVATE ${HERMES_COMMON_CFLAGS})
    if(HERMES_ENABLE_SANITIZERS)
        target_link_options(hermes-test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME hermes-test COMMAND hermes-test)
    if(UNIX)
        add_test(NAME hermes-relay-service COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/tests/test_relay_service.sh)
    endif()
endif()

if(HERMES_BUILD_FUZZ)
    add_executable(fuzz-envelope fuzz/fuzz_envelope.c)
    target_link_libraries(fuzz-envelope PRIVATE hermesrelay)
    target_compile_options(fuzz-envelope PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz-envelope PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
