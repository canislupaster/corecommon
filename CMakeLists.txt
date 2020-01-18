project(corecommon)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_compile_definitions(BUILD_DEBUG)
endif ()

file(GLOB SRC ./src/*.h ./src/*.c)

add_executable(headergen headergen/headergen.c ./src/hashtable.c ./src/vector.c ./src/util.c ./src/siphash.c)

add_custom_target(core-headers ALL ${CMAKE_BINARY_DIR}/headergen src headergen tests --pub)
add_dependencies(core-headers headergen)

add_library(corecommon ${SRC})
add_dependencies(corecommon core-headers)

enable_testing()
add_executable(hashtable_test tests/hashtable_test.c)
add_test(hashtable_test hashtable_test)
