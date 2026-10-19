cmake_minimum_required(VERSION 3.16)
# Fluante's code is this repository's root (a component); TesserAPI,
# TesserUI, TesserKIT, TesserMotion and NowTP are found through
# TESSERAPI_PATH, TESSERUI_PATH, TESSERKIT_PATH, TESSERMOTION_PATH and
# NOWTP_PATH, by default checkouts next to this repository.
get_filename_component(FLUANTE_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${FLUANTE_PATH}")
foreach(dep TesserAPI TesserUI TesserKIT TesserMotion NowTP)
    string(TOUPPER ${dep} DEP)
    if(DEFINED ENV{${DEP}_PATH})
        get_filename_component(${DEP}_PATH "$ENV{${DEP}_PATH}" ABSOLUTE)
    else()
        get_filename_component(${DEP}_PATH "${FLUANTE_PATH}/../${dep}" ABSOLUTE)
    endif()
    if(NOT EXISTS "${${DEP}_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "${dep} not found at ${${DEP}_PATH}: clone it there, or set ${DEP}_PATH")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${${DEP}_PATH}")
endforeach()
# A warning for each checkout that isn't the commit deps.lock pins.
include("${FLUANTE_PATH}/cmake/deps.cmake")
foreach(dep TesserAPI TesserUI TesserKIT TesserMotion NowTP)
    string(TOUPPER ${dep} DEP)
    fluante_check_sibling(${dep} "${${DEP}_PATH}")
endforeach()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fluante)
