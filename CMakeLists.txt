# Emulador TK90X / TK95 - build com CMake (Linux, Windows nativo com MSYS2/MinGW ou MSVC)
#
#   Linux:        sudo apt install cmake libsdl2-dev
#                 cmake -B build-cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-cmake
#   MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2}
#                 cmake -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-cmake
#   MSVC:         cmake -B build-cmake -DSDL2_DIR=C:/SDL2-2.x/cmake && cmake --build build-cmake --config Release
#
# Opcoes:
#   -DTK_STATIC_SDL=ON   liga o SDL2 estatico (SDL2::SDL2-static), um .exe sem DLL
#   -DTK_BUILD_TESTS=ON  compila tambem os testes (ctest)
#
# As ROMs ja vao embutidas no executavel (src/roms_embedded.c); a pasta roms/
# e copiada ao lado do executavel apenas para quem quiser troca-las.

cmake_minimum_required(VERSION 3.10)
project(tk90x VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

option(TK_STATIC_SDL "Ligar o SDL2 estaticamente" OFF)
option(TK_BUILD_TESTS "Compilar os testes" OFF)

find_package(SDL2 REQUIRED)

set(TK_CORE_SRC
  src/z80.c
  src/tape.c
  src/snapshot.c
  src/machine.c
  src/roms_embedded.c
  src/png.c
)
set(TK_APP_SRC
  ${TK_CORE_SRC}
  src/platform.c
  src/osd.c
  src/touch.c
  src/main.c
)

# Avisos e definicoes comuns
function(tk_setup_target tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W3)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
  if(NOT WIN32)
    target_compile_definitions(${tgt} PRIVATE _DEFAULT_SOURCE)
  endif()
  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(UNIX)
    target_link_libraries(${tgt} PRIVATE m)
  endif()
endfunction()

# WIN32 = subsistema de janela (sem console); SDL2main fornece o WinMain
add_executable(tk90x WIN32 ${TK_APP_SRC})
tk_setup_target(tk90x)

# SDL2: alvos importados (SDL2 >= 2.0.12) ou variaveis antigas
if(TARGET SDL2::SDL2main)
  target_link_libraries(tk90x PRIVATE SDL2::SDL2main)
endif()
if(TK_STATIC_SDL AND TARGET SDL2::SDL2-static)
  target_link_libraries(tk90x PRIVATE SDL2::SDL2-static)
  if(MINGW)
    target_link_options(tk90x PRIVATE -static)
  endif()
elseif(TARGET SDL2::SDL2)
  target_link_libraries(tk90x PRIVATE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
  target_link_libraries(tk90x PRIVATE SDL2::SDL2-static)
else()
  target_include_directories(tk90x PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(tk90x PRIVATE ${SDL2_LIBRARIES})
endif()

# O codigo usa #include <SDL.h>: garante o diretorio include/SDL2
if(SDL2_INCLUDE_DIRS)
  target_include_directories(tk90x PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

# Copia roms/ (e o SDL2.dll, se dinamico no Windows) para junto do executavel
add_custom_command(TARGET tk90x POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/roms $<TARGET_FILE_DIR:tk90x>/roms)
if(WIN32 AND TARGET SDL2::SDL2 AND NOT TK_STATIC_SDL)
  get_target_property(_sdl_type SDL2::SDL2 TYPE)
  if(_sdl_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET tk90x POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:tk90x>)
  endif()
endif()

install(TARGETS tk90x RUNTIME DESTINATION bin)
install(DIRECTORY roms DESTINATION bin)

# ---------------- Testes (opcional) ----------------
if(TK_BUILD_TESTS)
  enable_testing()
  set(TK_DATA ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)

  add_executable(z80_fusetest tests/z80_fusetest.c src/z80.c)
  tk_setup_target(z80_fusetest)
  add_test(NAME z80_fuse COMMAND z80_fusetest ${TK_DATA}/tests.in ${TK_DATA}/tests.expected)

  add_executable(tape_test tests/tape_test.c src/tape.c)
  tk_setup_target(tape_test)
  add_test(NAME tape COMMAND tape_test ${TK_DATA})

  # snapshot_test inclui src/snapshot.c (para testar a compressao interna)
  add_executable(snapshot_test tests/snapshot_test.c src/tape.c)
  tk_setup_target(snapshot_test)
  add_test(NAME snapshot COMMAND snapshot_test ${TK_DATA})

  add_executable(machine_test tests/machine_test.c ${TK_CORE_SRC})
  tk_setup_target(machine_test)
  add_test(NAME machine COMMAND machine_test ${TK_DATA} ${CMAKE_CURRENT_BINARY_DIR}/test-out)

  add_executable(zextest tests/zextest.c src/z80.c)
  tk_setup_target(zextest)
  add_test(NAME zexdoc COMMAND zextest ${TK_DATA}/zexdoc.com)
  add_test(NAME zexall COMMAND zextest ${TK_DATA}/zexall.com)
  set_tests_properties(zexdoc zexall PROPERTIES LABELS slow)

  # Boot e carga pelo front-end, sem janela
  add_test(NAME headless_boot COMMAND tk90x --headless --frames 300
           --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boot.png)
  add_test(NAME headless_load COMMAND tk90x --headless --until-loaded ${TK_DATA}/dogmole.tap)
endif()
