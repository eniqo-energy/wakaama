cmake_minimum_required(VERSION 3.9)
cmake_policy(SET CMP0042 NEW)
set (CMAKE_CXX_STANDARD 11)
set(CURRENT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")

set( includes ${VAR_PARENT}/include/liblwm2m.h )
project (build-node-addon-api-with-cmake)
include_directories(${CMAKE_JS_INC})

include(../../wakaama.cmake)

file(GLOB SOURCE_FILES "src/func.c" "src/func.h" "../server/lwm2mserver.c" "src/interface.c")
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
target_compile_definitions(${PROJECT_NAME} PRIVATE LWM2M_SERVER_MODE)

target_sources_wakaama(${PROJECT_NAME})
target_sources_shared(${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB})

# Include Node-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REGEX REPLACE "[\r\n\"]" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
# Enforce a certain level of hygiene
add_compile_options(
    -Waggregate-return
    -Wall
    -Wcast-align
    -Wextra
    -Wfloat-equal
    -Wpointer-arith
    -Wshadow
    -Wswitch-default
    -Wwrite-strings
    -pedantic
    # Reduce noise: Unused parameters are common in this ifdef-littered code-base, but of no danger
    -Wno-unused-parameter
    # Reduce noise: Too many false positives
    -Wno-uninitialized
    # Turn (most) warnings into errors
    -Werror
    # Disabled because of existing, non-trivially fixable code
    -Wno-error=cast-align
)
# define NAPI_VERSION
add_definitions(-DNAPI_VERSION=3)