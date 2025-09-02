#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

#define NULL 0

char* get_line() {
    static char buf[512];
    int n = 0;
    char c;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') break;
        buf[n++] = c;
    }
    if (n == 0) return NULL;

    char *line = malloc(n + 1);
    memmove(line, buf, n);
    line[n] = '\0';
    return line;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: xargs [command] [args...]\n");
        exit(1);
    }

    char *args[MAXARG];
    for (int i = 1; i < argc; i++) {
        args[i - 1] = argv[i];
    }

    char *line;
    while ((line = get_line()) != NULL) {
        args[argc - 1] = line;  
        int pid = fork();
        if (pid == 0) {
            exec(args[0], args);
            fprintf(2, "exec %s failed\n", args[0]);
            exit(1);
        } else {
            wait(0);
        }
        free(line);
    }
    exit(0);
}

