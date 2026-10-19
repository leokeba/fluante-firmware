# Fluante's code: an ESP-IDF component when built by idf.py (the firmware in
# firmware/ adds this directory), host library + tests otherwise.
set(FLUANTE_SRCS
    src/fluante/timing.cpp
    src/fluante/strobe.cpp
    src/fluante/spinner.cpp
    src/fluante/tree.cpp
    src/fluante/views.cpp
    src/fluante/platform/esp/pwm_esp.cpp)

if(ESP_PLATFORM)
    # TesserAPI, TesserUI and TesserMotion, under whatever component name they
    # were added with (a directory in EXTRA_COMPONENT_DIRS, or the component
    # manager's "<namespace>__<name>"), as TesserKIT finds its own.
    idf_build_get_property(FLUANTE_BUILD_COMPONENTS BUILD_COMPONENTS)
    set(FLUANTE_TESSERAPI "")
    set(FLUANTE_TESSERUI "")
    set(FLUANTE_TESSERMOTION "")
    foreach(c ${FLUANTE_BUILD_COMPONENTS})
        string(TOLOWER "${c}" c_lower)
        if(c_lower MATCHES "(^|__)tesserapi$")
            set(FLUANTE_TESSERAPI ${c})
        elseif(c_lower MATCHES "(^|__)tesserui$")
            set(FLUANTE_TESSERUI ${c})
        elseif(c_lower MATCHES "(^|__)tessermotion$")
            set(FLUANTE_TESSERMOTION ${c})
        endif()
    endforeach()
    if((NOT FLUANTE_TESSERAPI OR NOT FLUANTE_TESSERUI OR NOT FLUANTE_TESSERMOTION)
       AND NOT CMAKE_BUILD_EARLY_EXPANSION)
        message(FATAL_ERROR "Fluante needs TesserAPI, TesserUI and TesserMotion as components")
    endif()
    idf_component_register(SRCS ${FLUANTE_SRCS} INCLUDE_DIRS "src" PRIV_REQUIRES esp_driver_ledc)
    if(FLUANTE_TESSERAPI AND FLUANTE_TESSERUI AND FLUANTE_TESSERMOTION)
        foreach(dep ${FLUANTE_TESSERAPI} ${FLUANTE_TESSERUI} ${FLUANTE_TESSERMOTION})
            idf_component_get_property(lib ${dep} COMPONENT_LIB)
            target_link_libraries(${COMPONENT_LIB} PUBLIC ${lib})
        endforeach()
    endif()
    return()
endif()

cmake_minimum_required(VERSION 3.16)
project(fluante CXX)

option(FLUANTE_SANITIZE "Build with ASan/UBSan" ON)
set(TESSERMOTION_DIR "" CACHE PATH "TesserMotion source tree (default: ../TesserMotion)")
set(TESSERUI_DIR "" CACHE PATH "TesserUI source tree (default: ../TesserUI)")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TesserMotion, which brings TesserAPI (../TesserAPI next to it, or
# TESSERAPI_DIR), and its testing target: the fakes, a TMC2209 among them.
if(NOT TESSERMOTION_DIR)
    get_filename_component(TESSERMOTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TesserMotion" ABSOLUTE)
endif()
if(NOT EXISTS "${TESSERMOTION_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "TesserMotion not found at ${TESSERMOTION_DIR}: clone it there, or set TESSERMOTION_DIR")
endif()
set(TESSERMOTION_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${TESSERMOTION_DIR}" "${CMAKE_BINARY_DIR}/tessermotion")

# TesserUI for the views (tesserui_core); it reuses the TesserAPI already added.
if(NOT TESSERUI_DIR)
    get_filename_component(TESSERUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TesserUI" ABSOLUTE)
endif()
if(NOT EXISTS "${TESSERUI_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "TesserUI not found at ${TESSERUI_DIR}: clone it there, or set TESSERUI_DIR")
endif()
set(TESSERUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${TESSERUI_DIR}" "${CMAKE_BINARY_DIR}/tesserui")

# Each checkout against deps.lock: a warning when it isn't the pinned commit.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake")
get_filename_component(TESSERAPI_CHECKOUT "${CMAKE_CURRENT_SOURCE_DIR}/../TesserAPI" ABSOLUTE)
fluante_check_sibling(TesserAPI "${TESSERAPI_CHECKOUT}")
fluante_check_sibling(TesserUI "${TESSERUI_DIR}")
fluante_check_sibling(TesserMotion "${TESSERMOTION_DIR}")

set(FLUANTE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -fno-exceptions -fno-rtti)

add_library(fluante_core ${FLUANTE_SRCS})
target_include_directories(fluante_core PUBLIC src)
target_link_libraries(fluante_core PUBLIC tessermotion_core tesserui_core)
set_target_properties(fluante_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(fluante_core PRIVATE ${FLUANTE_WARNINGS})

enable_testing()
add_executable(fluante_tests test/test_main.cpp)
target_link_libraries(fluante_tests PRIVATE fluante_core tessermotion_testing)
set_target_properties(fluante_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(fluante_tests PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
add_test(NAME fluante_tests COMMAND fluante_tests)

if(FLUANTE_SANITIZE)
    # Every library in the binary, so it is instrumented alike.
    foreach(t tesser_core tesserui_core tessermotion_core tessermotion_fake_hw tessermotion_testing
              fluante_core fluante_tests)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()
