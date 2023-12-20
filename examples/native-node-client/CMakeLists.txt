cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0042 NEW)
set (CMAKE_CXX_STANDARD 11)

project (build-node-addon-api-with-cmake)
include_directories(${CMAKE_JS_INC})
file(GLOB SOURCE_FILES "src/hello.c")
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")
target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB})
include(../../wakaama.cmake)
add_executable(lwm2mserver src/lwm2mserver.c)
target_compile_definitions(lwm2mserver PRIVATE LWM2M_SERVER_MODE)
target_sources_shared(lwm2mserver)
target_sources_wakaama(lwm2mserver)

# Include Node-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REGEX REPLACE "[\r\n\"]" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})

# define NAPI_VERSION
add_definitions(-DNAPI_VERSION=3)