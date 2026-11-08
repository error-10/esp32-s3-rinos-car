cmake_minimum_required(VERSION 3.20)
project(RinOSCarSim C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SIM_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")
set(ROOT_DIR "${SIM_DIR}/..")
set(LGFX_DIR "${ROOT_DIR}/.pio/libdeps/esp32-s3-devkitc-1/LovyanGFX")
set(SDL2_DIR "${ROOT_DIR}/.pio/sdl2")

if(NOT EXISTS "${LGFX_DIR}/src/lgfx/v1/lgfx_v1.cpp")
    message(FATAL_ERROR "LovyanGFX not found at ${LGFX_DIR}")
endif()
if(NOT EXISTS "${SDL2_DIR}/build/Release/SDL2-static.lib")
    message(FATAL_ERROR "SDL2-static.lib not found at ${SDL2_DIR}/build/Release")
endif()

add_executable(rin_sim
    "${SIM_DIR}/sim_main.cpp"
    "${SIM_DIR}/lgfx_sdl.cpp"
    "${LGFX_DIR}/src/lgfx/Fonts/efont/lgfx_efont_cn.c"
    "${LGFX_DIR}/src/lgfx/Fonts/efont/lgfx_efont_ja.c"
    "${LGFX_DIR}/src/lgfx/Fonts/efont/lgfx_efont_kr.c"
    "${LGFX_DIR}/src/lgfx/Fonts/efont/lgfx_efont_tw.c"
    "${LGFX_DIR}/src/lgfx/Fonts/IPA/lgfx_font_japan.c"
    "${LGFX_DIR}/src/lgfx/utility/lgfx_miniz.c"
    "${LGFX_DIR}/src/lgfx/utility/lgfx_pngle.c"
    "${LGFX_DIR}/src/lgfx/utility/lgfx_qoi.c"
    "${LGFX_DIR}/src/lgfx/utility/lgfx_qrcode.c"
    "${LGFX_DIR}/src/lgfx/utility/lgfx_tjpgd.c"
)

# sim_main.cpp includes ../src/main.cpp rather than compiling it as a TU.
# Make MSVC/CMake rebuild sim_main.obj whenever the firmware source changes.
set_source_files_properties("${SIM_DIR}/sim_main.cpp" PROPERTIES
    OBJECT_DEPENDS "${ROOT_DIR}/src/main.cpp"
)

target_include_directories(rin_sim PRIVATE
    "${SIM_DIR}/include"
    "${LGFX_DIR}/src"
    "${SDL2_DIR}/build/include"
    "${SDL2_DIR}/build/include/SDL2"
    "${SDL2_DIR}/build/include-config-release"
    "${SDL2_DIR}/build/include-config-release/SDL2"
)

target_compile_definitions(rin_sim PRIVATE
    SIM_BUILD
    LGFX_SDL
    LGFX_USE_V1
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(rin_sim PRIVATE
        /utf-8 /W3 /EHsc
        /FI"${SIM_DIR}/include/msvc_compat.h"
    )
endif()

target_link_libraries(rin_sim PRIVATE
    "${SDL2_DIR}/build/Release/SDL2-static.lib"
    winmm
    imm32
    version
    setupapi
    ole32
    oleaut32
    user32
    gdi32
    advapi32
    shell32
    uuid
    ws2_32
)
