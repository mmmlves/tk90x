# Parte nativa do app Android do TK90X: SDL2 (compilado do codigo-fonte, vira libSDL2.so)
# e o emulador (libmain.so, que exporta SDL_main e as funcoes JNI de android_bridge.c).
#
# SDL2_SRC_DIR e TK_SRC_DIR vem do app/build.gradle; os padroes abaixo valem para
# quem roda o CMake direto a partir deste diretorio.
cmake_minimum_required(VERSION 3.6)
project(TK90X C)

if(NOT SDL2_SRC_DIR)
  set(SDL2_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/android/SDL2-2.32.10)
endif()
if(NOT TK_SRC_DIR)
  set(TK_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
endif()

# SDL fora da arvore: o segundo argumento da o diretorio de build dele.
add_subdirectory(${SDL2_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/SDL2)

set(TK_SOURCES
  ${TK_SRC_DIR}/z80.c
  ${TK_SRC_DIR}/tape.c
  ${TK_SRC_DIR}/snapshot.c
  ${TK_SRC_DIR}/machine.c
  ${TK_SRC_DIR}/profiles.c
  ${TK_SRC_DIR}/roms_embedded.c
  ${TK_SRC_DIR}/png.c
  ${TK_SRC_DIR}/platform.c
  ${TK_SRC_DIR}/osd.c
  ${TK_SRC_DIR}/touch.c
  ${TK_SRC_DIR}/main.c
  ${TK_SRC_DIR}/android_bridge.c
)

# SHARED (nunca add_executable): o SDLActivity carrega libmain.so e chama SDL_main.
add_library(main SHARED ${TK_SOURCES})
set_target_properties(main PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(main PRIVATE _DEFAULT_SOURCE)
target_include_directories(main PRIVATE ${TK_SRC_DIR})
target_link_libraries(main SDL2 SDL2main android log m)
