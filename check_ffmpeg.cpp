#include <stdio.h>
extern "C" {
#include <libavformat/avformat.h>
}
int main() {
    void *opaque = NULL;
    const char *name;
    printf("Protocols:\n");
    while ((name = avio_enum_protocols(&opaque, 0))) {
        printf("Out: %s\n", name);
    }
    opaque = NULL;
    while ((name = avio_enum_protocols(&opaque, 1))) {
        printf("In: %s\n", name);
    }
    return 0;
}
