#include "../src/hashtable.h"

#include <stdio.h>
#include <time.h>

static char *rand_string(char *str, unsigned size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJK...";
    if (size) {
        --size;
        for (unsigned n = 0; n < size; n++) {
            int key = rand() % (int) (strlen(charset) - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
    return str;
}

int main(int argc, char** argv) {
    float start_time = (float)clock() / CLOCKS_PER_SEC;

    map_t x = map_new();
    map_configure_string_key(&x, sizeof(char*));

    srand(743);

    for (unsigned i=0; i<1000; i++) {
        char* key = malloc(31);
        rand_string(key, 30);

        char* v = malloc(31);
        rand_string(v, 30);

        map_insertcpy(&x, &key, &v);
        if (map_find(&x, &key)==NULL) return 1;
    }

    float end_time = (float)clock() / CLOCKS_PER_SEC;

    float elapsed = end_time - start_time;
    printf("%f", elapsed * 1000);

    return 0;
}
