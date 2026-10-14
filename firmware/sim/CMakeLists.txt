# Desktop simulator for the OpenRouter Meter firmware.
#   cmake -S firmware/sim -B build-sim && cmake --build build-sim -j
#   cd firmware && ../build-sim/meter-sim
# Needs SDL2 development files (Linux: libsdl2-dev, macOS: brew install sdl2,
# Windows: vcpkg install sdl2). LVGL and ArduinoJson are fetched automatically.
cmake_minimum_required(VERSION 3.16)
project(meter_sim C CXX)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.6.0 GIT_SHALLOW TRUE)
FetchContent_Declare(arduinojson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.4.3 GIT_SHALLOW TRUE)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()
FetchContent_GetProperties(arduinojson)
if(NOT arduinojson_POPULATED)
  FetchContent_Populate(arduinojson)
endif()
find_package(SDL2 REQUIRED)

set(LV_DEFS LV_CONF_SKIP LV_COLOR_DEPTH=16 LV_USE_LOG=0 LV_MEM_SIZE=262144
            LV_USE_ARC=1 LV_USE_LABEL=1 LV_USE_LINE=1 LV_USE_IMAGE=1)

file(GLOB_RECURSE LVGL_SRC ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_compile_definitions(lvgl PUBLIC ${LV_DEFS})
target_include_directories(lvgl PUBLIC ${lvgl_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../src)
file(GLOB APP_SRC ${FW}/*.cpp ${FW}/ui/*.cpp ${FW}/fonts/*.c ${FW}/board/*.cpp ${FW}/link/*.cpp
                  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(meter-sim ${APP_SRC})
target_compile_definitions(meter-sim PRIVATE SIMULATOR)
target_include_directories(meter-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${FW}
                           ${arduinojson_SOURCE_DIR}/src ${SDL2_INCLUDE_DIRS})
target_link_libraries(meter-sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
