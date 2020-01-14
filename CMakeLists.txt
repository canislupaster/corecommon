project(corecommon)


file(GLOB SRC ./src/*.h ./src/*.c)

add_executable(headergen headergen/headergen.c ./src/hashtable.c ./src/vector.c ./src/util.c ./src/siphash.c)

add_custom_target(gen-headers ALL ${CMAKE_BINARY_DIR}/headergen src headergen tests --pub)
add_dependencies(gen-headers headergen)

add_library(corecommon ${SRC})

enable_testing()
add_executable(hashtable_test tests/hashtable_test.c)
add_test(hashtable_test hashtable_test)
