cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)

project(corecommon)

file(GLOB SRC ./src/*.h ./src/*.c)

add_library(corecommon ${SRC})

add_executable(headergen headergen/headergen.c ./src/hashtable.c ./src/vector.c ./src/util.c ./src/siphash.c)
target_link_libraries(headergen corecommon)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_definitions(BUILD_DEBUG)
else ()
		target_compile_options(headergen PUBLIC -O0)
endif ()

#enable_testing()
#add_executable(hashtable_test tests/hashtable_test.c)
#add_test(hashtable_test hashtable_test)
