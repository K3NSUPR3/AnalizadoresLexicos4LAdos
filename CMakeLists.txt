cmake_minimum_required(VERSION 3.20)
project(AlmaLili VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimización al enlazar: permite integrar en línea las funciones del núcleo
# que el analizador léxico llama por cada token.
include(CheckIPOSupported)
check_ipo_supported(RESULT ALMALILI_IPO OUTPUT ALMALILI_IPO_DETALLE)
if(ALMALILI_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Núcleo del compilador: todas las fases viven en esta biblioteca.
add_library(almalili_nucleo STATIC
    src/diagnostico.cpp
    src/fuente.cpp
    src/lexico.cpp
    src/tabla_simbolos.cpp
    src/token.cpp
    src/unicode.cpp
    src/utf8.cpp
)
target_include_directories(almalili_nucleo PUBLIC include)

# El código fuente y los literales del compilador están en UTF-8.
if(MSVC)
    target_compile_options(almalili_nucleo PUBLIC /utf-8 /W4 /permissive-)
else()
    target_compile_options(almalili_nucleo PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(almalili src/main.cpp)
target_link_libraries(almalili PRIVATE almalili_nucleo)
if(WIN32)
    target_link_libraries(almalili PRIVATE shell32)
endif()

enable_testing()
add_executable(pruebas_lexico pruebas/pruebas_lexico.cpp)
target_link_libraries(pruebas_lexico PRIVATE almalili_nucleo)
add_test(NAME lexico COMMAND pruebas_lexico)
