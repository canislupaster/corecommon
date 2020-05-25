project(corecommon)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_definitions(BUILD_DEBUG)
endif ()

file(GLOB SRC ./src/*.h ./src/*.c)

add_executable(headergen headergen/headergen.c ./src/hashtable.c ./src/vector.c ./src/util.c ./src/siphash.c ./src/rwlock.c)

add_library(corecommon ${SRC})

enable_testing()
add_executable(hashtable_test tests/hashtable_test.c)
add_test(hashtable_test hashtable_test)
