cmake_minimum_required(VERSION 3.20)
project(GeSe VERSION 2.4.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/ffmpeg/bin)
  set(FFMPEG_DEFAULT ${CMAKE_SOURCE_DIR}/third_party/ffmpeg)  # tools/fetch_deps.ps1
else()
  set(FFMPEG_DEFAULT "C:/ffmpeg")
endif()
set(FFMPEG_DIR ${FFMPEG_DEFAULT} CACHE PATH "FFmpeg shared build (include/, lib/, bin/)")
file(GLOB FFMPEG_DLLS ${FFMPEG_DIR}/bin/avformat-*.dll ${FFMPEG_DIR}/bin/avcodec-*.dll ${FFMPEG_DIR}/bin/avutil-*.dll
     ${FFMPEG_DIR}/bin/swscale-*.dll ${FFMPEG_DIR}/bin/swresample-*.dll ${FFMPEG_DIR}/bin/avfilter-*.dll)
set(OUT_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR})
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_win32.cpp ${IMGUI}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS UNICODE _UNICODE)
target_compile_options(imgui PRIVATE -O2)

file(GLOB GESE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(GeSe WIN32 ${GESE_SRC} ${CMAKE_SOURCE_DIR}/res/app.rc)
set(ORT_DIR ${CMAKE_SOURCE_DIR}/third_party/onnxruntime)
target_include_directories(GeSe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(GeSe SYSTEM PRIVATE ${FFMPEG_DIR}/include ${ORT_DIR}/include)  # third-party headers: no warnings
target_compile_definitions(GeSe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(GeSe PRIVATE -O2 -Wall -Wno-unused-function -Wno-sign-compare -Wno-unused-variable -Wno-missing-braces
                                    -Wno-unused-but-set-variable -Wno-parentheses -Wno-format-truncation -Wno-class-memaccess)
# FFmpeg is delay-loaded (tools/make_delaylibs.ps1) so its DLLs can live in runtime\ next to the exe
set(DELAY_DIR ${CMAKE_BINARY_DIR}/delay)
target_link_libraries(GeSe PRIVATE imgui
  ${DELAY_DIR}/libavformat_delay.a ${DELAY_DIR}/libavcodec_delay.a ${DELAY_DIR}/libavutil_delay.a
  ${DELAY_DIR}/libswscale_delay.a ${DELAY_DIR}/libswresample_delay.a ${DELAY_DIR}/libavfilter_delay.a
  d3d11 dxgi d3dcompiler_47 d2d1 dwrite dwmapi windowscodecs winhttp ole32 oleaut32 uuid shell32 shlwapi user32 gdi32
  comdlg32 imm32 propsys)
target_link_options(GeSe PRIVATE -static -static-libgcc -static-libstdc++ -municode -mwindows)
set_target_properties(GeSe PROPERTIES OUTPUT_NAME "GeSe")

# install layout (same as the installed program):
#   GeSe.exe   runtime\ (all DLLs)   resources\ (effects, fonts, splash, presets, models)   plugins\   licenses\
add_custom_command(TARGET GeSe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OUT_DIR}/runtime ${OUT_DIR}/resources/Models ${OUT_DIR}/licenses
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} ${OUT_DIR}/runtime
  # built-in background removal: ONNX Runtime + DirectML (any DX12 GPU) + the BiRefNet model (MIT)
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_DIR}/bin/onnxruntime.dll ${ORT_DIR}/bin/DirectML.dll
          ${ORT_DIR}/bin/msvcp140.dll ${ORT_DIR}/bin/msvcp140_1.dll ${ORT_DIR}/bin/vcruntime140.dll ${ORT_DIR}/bin/vcruntime140_1.dll ${OUT_DIR}/runtime
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_DIR}/models/birefnet_lite.onnx ${OUT_DIR}/resources/Models/birefnet_lite.onnx
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Main ${OUT_DIR}/resources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Plugins ${OUT_DIR}/plugins
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/licenses ${OUT_DIR}/licenses)

# installer (single exe with embedded payload, built by tools/build.ps1 after GeSe)
if(EXISTS ${CMAKE_BINARY_DIR}/payload/payload.rc)
  add_executable(GeSe_Setup WIN32 ${CMAKE_SOURCE_DIR}/installer/setup.cpp ${CMAKE_SOURCE_DIR}/src/core.cpp ${CMAKE_SOURCE_DIR}/src/gfx.cpp
                 ${CMAKE_BINARY_DIR}/payload/payload.rc)
  target_include_directories(GeSe_Setup PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_definitions(GeSe_Setup PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_compile_options(GeSe_Setup PRIVATE -O2)
  target_link_libraries(GeSe_Setup PRIVATE imgui d3d11 dxgi d3dcompiler_47 dwmapi windowscodecs ole32 oleaut32 uuid shell32 user32 gdi32 imm32
                        cabinet advapi32)
  target_link_options(GeSe_Setup PRIVATE -static -static-libgcc -static-libstdc++ -municode -mwindows)
endif()
