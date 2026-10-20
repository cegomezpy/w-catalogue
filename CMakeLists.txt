cmake_minimum_required(VERSION 3.20)
project(WCatalog VERSION 0.1.0 LANGUAGES C CXX RC)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilación" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(src/version.hpp.in ${CMAKE_BINARY_DIR}/generated/version.hpp @ONLY)

# SQLite se compila como C aparte para no aplicar las advertencias de C++
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(catalog_core STATIC ${CORE_SOURCES})
target_include_directories(catalog_core PUBLIC
    src
    ${CMAKE_BINARY_DIR}/generated
    third_party/httplib
    third_party/json
    third_party/stb)
target_compile_definitions(catalog_core PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(catalog_core PRIVATE -Wall -Wextra)
target_link_libraries(catalog_core PUBLIC sqlite3 ws2_32 shell32 user32 advapi32)

set_source_files_properties(resources/app.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/resources/app.manifest;${CMAKE_SOURCE_DIR}/resources/app.ico")
add_executable(WCatalog WIN32 src/main.cpp resources/app.rc)
target_link_libraries(WCatalog PRIVATE catalog_core)
target_link_options(WCatalog PRIVATE -static -static-libgcc -static-libstdc++ -mwindows $<$<CONFIG:Release>:-s>)

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(catalog_tests ${TEST_SOURCES})
target_include_directories(catalog_tests PRIVATE third_party/doctest)
target_compile_definitions(catalog_tests PRIVATE WCATALOG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(catalog_tests PRIVATE catalog_core)
add_test(NAME catalog_tests COMMAND catalog_tests)
target_link_options(catalog_tests PRIVATE -static -static-libgcc -static-libstdc++)

# cmake --install build --prefix dist/WCatalog
install(TARGETS WCatalog RUNTIME DESTINATION .)
install(DIRECTORY web/ DESTINATION web)
install(DIRECTORY fonts/ DESTINATION fonts)

# Pruebas de la interfaz que no necesitan navegador (solo si hay Node.js)
find_program(NODE_EXE node)
if(NODE_EXE)
    add_test(NAME web_coords COMMAND ${NODE_EXE} ${CMAKE_SOURCE_DIR}/tests/js/coords.test.js)
endif()
