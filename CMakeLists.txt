cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)

project(corecommon)

file(GLOB SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

add_library(corecommon ${SRC})

add_executable(headergen ${CMAKE_CURRENT_SOURCE_DIR}/headergen/headergen.c ${CMAKE_CURRENT_SOURCE_DIR}/src/hashtable.c ${CMAKE_CURRENT_SOURCE_DIR}/src/vector.c ${CMAKE_CURRENT_SOURCE_DIR}/src/util.c ${CMAKE_CURRENT_SOURCE_DIR}/src/siphash.c ${CMAKE_CURRENT_SOURCE_DIR}/src/rwlock.c)
target_link_libraries(headergen corecommon)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

if (CMAKE_BUILD_TYPE MATCHES Debug)
	add_compile_definitions(BUILD_DEBUG)
else ()
	target_compile_options(headergen PUBLIC -O0)
endif ()

#enable_testing()
#add_executable(hashtable_test tests/hashtable_test.c)
#add_test(hashtable_test hashtable_test)
