cmake_minimum_required(VERSION 3.16)
project(c3_ai_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(test_app_mode
    test_app_mode.c
    ${REPO_ROOT}/components/app_logic/app_mode.c)
target_include_directories(test_app_mode PRIVATE
    ${REPO_ROOT}/components/app_logic/include)
add_test(NAME app_mode COMMAND test_app_mode)

add_executable(test_sensor_mock
    test_sensor_mock.c
    ${REPO_ROOT}/components/sensor/sensor_mock.c
    ${REPO_ROOT}/components/sensor/sensor_status.c)
target_include_directories(test_sensor_mock PRIVATE
    ${REPO_ROOT}/components/sensor/include)
add_test(NAME sensor_mock COMMAND test_sensor_mock)

add_executable(test_button_fsm
    test_button_fsm.c
    ${REPO_ROOT}/components/app_logic/app_button.c)
target_include_directories(test_button_fsm PRIVATE
    ${REPO_ROOT}/components/app_logic/include)
add_test(NAME button_fsm COMMAND test_button_fsm)
