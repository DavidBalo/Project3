#include <stdio.h>
#include <stdlib.h>
#include <errno.h>           // for errno and strerror()
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define N 12
#define MAX_ARGS 21

extern char **environ;

// Allowed commands list
static const char *allowed[N] = {
    "cp", "touch", "mkdir", "ls", "pwd", "cat",
    "grep", "chmod", "diff", "cd", "exit", "help"
};

// Check if a command is in the allowed list
static int isAllowed(const char *cmd) {
    for (int i = 0; i < N; i++) {
        if (strcmp(cmd, allowed[i]) == 0)
            return 1;
    }
    return 0;
}

// Built-in function signatures
static int bi_cd(int argc, char *argv[]);
static int bi_exit(int argc, char *argv[]);
static int bi_help(int argc, char *argv[]);

// Table of built-ins
typedef int (*builtin_fn)(int, char**);
static const struct {
    const char  *name;
    builtin_fn   fn;
} builtins[] = {
    { "cd",   bi_cd   },
    { "exit", bi_exit },
    { "help", bi_help },
};
static const size_t n_builtins = sizeof(builtins)/sizeof(builtins[0]);

// Retrieve built-in handler for a command (or NULL)
static builtin_fn get_builtin(const char *cmd) {
    for (size_t i = 0; i < n_builtins; i++) {
        if (strcmp(cmd, builtins[i].name) == 0)
            return builtins[i].fn;
    }
    return NULL;
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    char *argv[MAX_ARGS];

    // Ignore Ctrl-C in the shell
    struct sigaction sa = { .sa_handler = SIG_IGN };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    while (1) {
        // Print prompt
        fprintf(stderr, "rsh>");
        fflush(stderr);

        // Read a line of input
        nread = getline(&line, &len, stdin);
        if (nread <= 0)  // EOF or error
            break;

        // Strip newline
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0')  // empty line
            continue;

        // Tokenize into argv[]
        int argc = 0;
        char *saveptr = NULL;
        char *token = strtok_r(line, " \t", &saveptr);
        while (token && argc < MAX_ARGS-1) {
            if (*token != '\0')
                argv[argc++] = token;
            token = strtok_r(NULL, " \t", &saveptr);
        }
        argv[argc] = NULL;
        if (argc == 0)
            continue;

        // Disallow any command not in our list
        if (!isAllowed(argv[0])) {
            printf("NOT ALLOWED!\n");
            continue;
        }

        // Built-in?
        builtin_fn bf = get_builtin(argv[0]);
        if (bf) {
            int rc = bf(argc, argv);
            if (bf == bi_exit) {
                free(line);
                return 0;
            }
            continue;
        }

        // External command: spawn a child
        pid_t pid;
        posix_spawnattr_t attr;
        if (posix_spawnattr_init(&attr) != 0) {
            perror("rsh: posix_spawnattr_init");
            continue;
        }
        // Reset SIGINT in the child
        sigset_t dfl;
        sigemptyset(&dfl);
        sigaddset(&dfl, SIGINT);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);
        posix_spawnattr_setsigdefault(&attr, &dfl);

        int rc = posix_spawnp(&pid, argv[0], NULL, &attr, argv, environ);
        posix_spawnattr_destroy(&attr);
        if (rc != 0) {
            errno = rc;
            perror("rsh: spawn");
            continue;
        }
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("rsh: waitpid");
        }
    }

    free(line);
    return 0;
}

// ----- Built-in implementations -----

// cd [dir]
static int bi_cd(int argc, char *argv[]) {
    if (argc > 2) {
        printf("-rsh: cd: too many arguments\n");
        return 1;
    }
    const char *target = (argc == 2 ? argv[1] : getenv("HOME"));
    if (!target) target = "/";
    if (chdir(target) != 0) {
        perror("cd failed");
    }
    return 0;
}

// exit
static int bi_exit(int argc, char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}

// help
static int bi_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("The allowed commands are:\n");
    for (int i = 0; i < N; i++) {
        printf("%d: %s\n", i+1, allowed[i]);
    }
    return 0;
}
