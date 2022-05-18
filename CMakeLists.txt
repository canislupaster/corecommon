cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 17)

project(corecommon)

find_package(PkgConfig REQUIRED)

file(GLOB SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/database.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/database.hpp)

add_library(corecommon ${SRC})

if (CMAKE_BUILD_TYPE MATCHES Debug)
	add_compile_definitions(BUILD_DEBUG)
endif ()

enable_testing()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(rawstringifier rawstringifier.cpp)

if (graphics)
    add_library(graphics graphics/graphics.cpp graphics/nanosvg.c)
    include_directories(graphics)
    add_dependencies(graphics corecommon)

    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(FreeType2 REQUIRED IMPORTED_TARGET freetype2)

    target_link_libraries(graphics PUBLIC PkgConfig::SDL2 PkgConfig::FreeType2 corecommon)

    if (EMSCRIPTEN)
        target_compile_definitions(graphics PUBLIC GLES)
    else()
#        find_package(OpenGL REQUIRED)
#        target_link_libraries(graphics PUBLIC OpenGL::OpenGL)
    endif()

    file(GLOB SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/graphics/shaders/**)

    set(RAWSTRINGIFIER_PATH "${CMAKE_BINARY_DIR}/../build/rawstringifier")
    if (NOT EXISTS ${RAWSTRINGIFIER_PATH})
        execute_process(COMMAND clang++ ${CMAKE_CURRENT_SOURCE_DIR}/rawstringifier.cpp -o ${RAWSTRINGIFIER_PATH})
    endif()

    foreach(SHADER ${SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        add_custom_target(${SHADER_NAME} COMMAND ${RAWSTRINGIFIER_PATH} ${SHADER} > ${CMAKE_CURRENT_SOURCE_DIR}/graphics/include/${SHADER_NAME} DEPENDS ${SHADER} BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/graphics/include/${SHADER_NAME})
        add_dependencies(graphics ${SHADER_NAME})
    endforeach()
endif()

if (server)
    list(APPEND TESTS tests/server_test.cpp)

    add_library(server ${CMAKE_CURRENT_SOURCE_DIR}/server/server.cpp)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/server)
    add_dependencies(server corecommon)

    find_library(LIBEVENT libevent.a)
    find_library(LIBEVENT_PTHREADS libevent_pthreads.a)
    find_path(LIBEVENT_INCLUDE NAMES event2)

    find_package(OpenSSL)

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    target_link_libraries(server PUBLIC corecommon ${LIBEVENT} ${LIBEVENT_PTHREADS} ${OPENSSL_CRYPTO_LIBRARY} Threads::Threads)
    target_include_directories(server PUBLIC ${OPENSSL_INCLUDE_DIR} ${LIBEVENT_INCLUDE})

    add_executable(fileserver server/fileserver.cpp)
    add_dependencies(fileserver server corecommon)
    target_link_libraries(fileserver PUBLIC corecommon server)
endif()

foreach(TEST ${TESTS})
    get_filename_component(NAME ${TEST} NAME_WLE)
    add_executable(${NAME} ${TEST})

    add_dependencies(${NAME} corecommon)
    target_link_libraries(${NAME} corecommon)

    add_test(${NAME} ${NAME})
endforeach()

if (server)
    add_dependencies(server_test server)
    target_link_libraries(server_test server)
endif()