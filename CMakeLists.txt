cmake_minimum_required(VERSION 3.25)
project(WireQuery)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WireQuery main.cpp)
set_target_properties(WireQuery PROPERTIES OUTPUT_NAME wireq)
target_include_directories(WireQuery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_sources(WireQuery PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/domain/json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/cli.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/infra/posix_resolver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/infra/rawdns_ldns.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/usecases/aggregate.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/usecases/run_queries.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/output_text.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/output_json.cpp
)

# --- ldns detection (optional) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LDNS QUIET ldns)
  if(LDNS_FOUND)
    message(STATUS "Found ldns: ${LDNS_VERSION}")
    target_compile_definitions(WireQuery PRIVATE HAVE_LDNS=1)
    target_link_libraries(WireQuery PRIVATE ${LDNS_LIBRARIES})
    if(LDNS_LIBRARY_DIRS)
      target_link_directories(WireQuery PRIVATE ${LDNS_LIBRARY_DIRS})
    endif()
    target_include_directories(WireQuery PRIVATE ${LDNS_INCLUDE_DIRS})
    if(LDNS_CFLAGS_OTHER)
      target_compile_options(WireQuery PRIVATE ${LDNS_CFLAGS_OTHER})
    endif()
    if(LDNS_LDFLAGS_OTHER)
      target_link_options(WireQuery PRIVATE ${LDNS_LDFLAGS_OTHER})
    endif()
    # ldns headers may include <openssl/ssl.h> (via dane.h), ensure OpenSSL is available
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
      message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
      target_link_libraries(WireQuery PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    else()
      message(STATUS "OpenSSL not found; if you hit openssl/ssl.h not found, install openssl and set PKG_CONFIG_PATH/CMAKE_PREFIX_PATH")
    endif()
  else()
    message(STATUS "ldns not found via pkg-config; building without raw DNS mode")
  endif()
else()
  message(STATUS "pkg-config not available; building without raw DNS mode")
endif()

# ---- Tests (CTest) ----
include(CTest)
enable_testing()

## 0) Help without args should print Usage
add_test(NAME cli_help_noargs
         COMMAND $<TARGET_FILE:WireQuery>)
set_tests_properties(cli_help_noargs PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")

## 1) Resolve localhost (text mode)
add_test(NAME resolve_localhost_text
         COMMAND $<TARGET_FILE:WireQuery> localhost --tries 1)
set_tests_properties(resolve_localhost_text PROPERTIES PASS_REGULAR_EXPRESSION "try 1: [0-9]+\\.[0-9][0-9][0-9] ms - [0-9]+ address\\(es\\)")

## 2) JSON output for localhost
add_test(NAME resolve_localhost_json
         COMMAND $<TARGET_FILE:WireQuery> --json --tries 1 localhost)
set_tests_properties(resolve_localhost_json PROPERTIES PASS_REGULAR_EXPRESSION "\{\"host\":\"localhost\"")

## 3) Reverse (PTR) on 127.0.0.1 (accepts either success or error, just presence)
add_test(NAME reverse_ptr_loopback
         COMMAND $<TARGET_FILE:WireQuery> --reverse --tries 1 127.0.0.1)
set_tests_properties(reverse_ptr_loopback PROPERTIES PASS_REGULAR_EXPRESSION "PTR:")

## 4) Concurrency with multiple tries (check summary line)
add_test(NAME concurrency_text
         COMMAND $<TARGET_FILE:WireQuery> --tries 4 --concurrency 2 localhost)
set_tests_properties(concurrency_text PROPERTIES PASS_REGULAR_EXPRESSION "summary: .+ tries")

## 5) Numeric host (no DNS)
add_test(NAME numeric_host_ipv4
         COMMAND $<TARGET_FILE:WireQuery> --numeric-host --tries 1 127.0.0.1)
set_tests_properties(numeric_host_ipv4 PROPERTIES PASS_REGULAR_EXPRESSION "try 1: [0-9]+\\.[0-9][0-9][0-9] ms - [0-9]+ address\\(es\\)")

## 6) Force IPv4 family with -4
add_test(NAME family_ipv4_only
         COMMAND $<TARGET_FILE:WireQuery> -4 --tries 1 localhost)
set_tests_properties(family_ipv4_only PROPERTIES PASS_REGULAR_EXPRESSION "\\[inet\\]")

## 7) Service/port is reflected in output
add_test(NAME service_port_80
         COMMAND $<TARGET_FILE:WireQuery> --service 80 --tries 1 127.0.0.1)
set_tests_properties(service_port_80 PROPERTIES PASS_REGULAR_EXPRESSION "port=80")

## 8) Error handling for non-existent host (still exits 0, but prints error)
add_test(NAME nonexistent_host_error
         COMMAND $<TARGET_FILE:WireQuery> --tries 1 nonexistent.invalid)
set_tests_properties(nonexistent_host_error PROPERTIES PASS_REGULAR_EXPRESSION "error:")

## 9) Dedup option smoke (ensure summary appears)
add_test(NAME dedup_smoke
         COMMAND $<TARGET_FILE:WireQuery> --dedup --tries 1 localhost)
set_tests_properties(dedup_smoke PROPERTIES PASS_REGULAR_EXPRESSION "summary:")

# ---- Placeholder tests for proposed features (expected to fail until implemented) ----
add_test(NAME ndjson_streaming
         COMMAND $<TARGET_FILE:WireQuery> --ndjson --tries 2 localhost)
set_tests_properties(ndjson_streaming PROPERTIES PASS_REGULAR_EXPRESSION "\\{\\\"try\\\":1(.|\\n)*\\{\\\"try\\\":2")

add_test(NAME percentiles_summary
         COMMAND $<TARGET_FILE:WireQuery> --pctl 50,90 --tries 5 localhost)
set_tests_properties(percentiles_summary PROPERTIES PASS_REGULAR_EXPRESSION "percentiles:.*p50=.*p90=")

# ---- Unit tests: presentation layer ----
add_executable(presentation_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/presentation_tests.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/output_text.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation/output_json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/domain/json.cpp
)
target_include_directories(presentation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME presentation_unit COMMAND $<TARGET_FILE:presentation_tests>)
set_tests_properties(presentation_unit PROPERTIES PASS_REGULAR_EXPRESSION "presentation tests: OK")
