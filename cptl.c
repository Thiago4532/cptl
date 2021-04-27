#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT_SIZE 255
#define MAX_LIST_SIZE 255
#define MAX_COMMAND_LENGTH 32768

typedef struct {
    char* name;
    char* path;
} TEMPLATE;

TEMPLATE t_list[MAX_LIST_SIZE];
int t_id[MAX_LIST_SIZE];

int t_len, t_id_len;
char input[MAX_INPUT_SIZE];

char preview_command[MAX_COMMAND_LENGTH] = "cat";

char* fgetl(FILE* stream) {
    int len = 0;
    for (int c = getc(stream); c != '\n'; c = getc(stream)) {
        if (c == EOF) return NULL;
        input[len++] = c;
    }

    input[len++] = '\0';

    return input;
}

void strcpyl(char* dst, const char* src) {
    int i;
    for (i = 0; src[i] != '\0'; i++)
        dst[i] = tolower(src[i]);
    dst[i] = src[i];
}

char* getcwd_pointer() {
    int n = pathconf(".", _PC_PATH_MAX);
    if (n == -1) return NULL;

    char* buff = malloc(n * sizeof *buff);
    if (getcwd(buff, n) != NULL)
        return buff;

    free(buff);
    return NULL;
}

void add_element(char* token) {
    if (token == NULL) {
        fprintf(stderr, "add: Missing template's name!\n");
        exit(EPERM);
    }

    int len = 0;
    while (token) {
        if (strcmp(token, "|") == 0)
            break;

        if (len > 0) input[len++] = ' ';

        while (*token) {
            input[len++] = *token;
            token++;
        }
        token = strtok(NULL, " \t");
    }

    input[len++] = '\0';

    t_list[t_len].name = (char*) malloc(len);
    strcpy(t_list[t_len].name, input);

    if (token != NULL)
        token = strtok(NULL, " \t");

    if (token == NULL) {
        fprintf(stderr, "add: Missing template's path!\n");
        exit(EPERM);
    }

    len = 0;
    while (token) {
        if (len > 0) input[len++] = ' ';

        while (*token) {
            input[len++] = *token;
            token++;
        }
        token = strtok(NULL, " \t");
    }
    
    input[len++] = '\0';

    char* path = realpath(input, NULL);
    if (path == NULL) {
        fprintf(stderr, "path: %s\n", strerror(errno));
        fprintf(stderr, "template: %s | %s\n", t_list[t_len].name, input);

        free(path);
        exit(errno);
    }

    t_list[t_len].path = (char*) malloc( strlen(path) + 1 );
    strcpy(t_list[t_len].path, path);

    free(path);
    t_len++;
}

FILE* parse_line(char* line) {
    char* token = strtok(line, " \t");

    if (token == NULL) return NULL;

    if (strcmp(token, "add") == 0) {
        token = strtok(NULL, " \t");
        add_element(token);
    } else if(strcmp(token, "chdir") == 0) {
        token = strtok(NULL, " \t");
        
        if (token == NULL) {
            fprintf(stderr, "chdir: Missing directory!\n");
            exit(ENOENT);
        }

        if (chdir(token) == -1) {
            fprintf(stderr, "chdir: %s\n", strerror(errno));
            exit(errno);
        }

        FILE* file = fopen(".cptl", "r");
        if (file == NULL) {
            fprintf(stderr, "chdir: %s\n", strerror(errno));
            fprintf(stderr, "Maybe you are missing .cptl file.\n");
            int error = errno;

            char* buff = getcwd_pointer();
            if (buff)
                fprintf(stderr, "getcwd(): %s\n", buff);
            free(buff);

            exit(error);
        }
        return file;
    } else if (strcmp(token, "preview_cmd") == 0) {
        token = strtok(NULL, " \t");
        
        if (token == NULL) {
            fprintf(stderr, "preview_cmd: Missing path!\n");
            exit(ENOENT);
        }

        char* path = realpath(token, NULL);
        if (path == NULL || access(path, X_OK) != 0) {
            fprintf(stderr, "path: %s\n", strerror(errno));
            fprintf(stderr, "token: %s\n", token);
            exit(errno);
        }

        printf("%ld\n", strlen(path));
        strcpy(preview_command, path);
    }else {
        fprintf(stderr, "bad input: %s\n", token);
        exit(EPERM);
    }
    return NULL;
}

int check() {
    for (int i = 0; i < t_len; i++) {
        printf("Name: %s\n", t_list[i].name);
        printf("Path: %s\n", t_list[i].path);
        printf("Status: %s\033[0m\n",
                (access(t_list[i].path, R_OK) == 0 ? "\033[1;32mOK!"
                        : "\033[1;31mInvalid!"));
        if (i + 1 < t_len)
            puts("");
    }
    return 0;
}

int simple_menu() {
    for (int i = 0; i < t_id_len; i++)
        printf("%d) %s\n", i+1, t_list[t_id[i]].name);

    int id = 0;

    printf("Valor: ");
    scanf("%d", &id);
    id--;

    if (id < 0 || id >= t_id_len) {
        fprintf(stderr, "input: Invalid ID!\n");
        exit(EXIT_FAILURE);
    }
    return id;
}

int fzf_menu() { 
    char fzf_height[] = "80%";

    int fd[2], fd_in[2];

    if (pipe(fd) == -1) {
        fprintf(stderr, "pipe out: %s\n", strerror(errno));
        exit(errno);
    }

    if (pipe(fd_in) == -1) {
        fprintf(stderr, "pipe in: %s\n", strerror(errno));
        exit(errno);
    }

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        exit(errno);
    }

    if (pid > 0) {
        close(fd[1]);
        close(fd_in[0]); close(fd_in[1]);

        wait(NULL);

        FILE* file = fdopen(fd[0], "r");

        int id;
        if (fscanf(file, "%d:", &id) == EOF)
            exit(1);

        fclose(file);
        close(fd[0]);

        return id;
    } else {
        close(fd[0]);

        for (int i = 0; i < t_id_len; i++)
            dprintf(fd_in[1], "%d:%s:%s\n", i, t_list[t_id[i]].path,
                        t_list[t_id[i]].name);

        dup2(fd[1], STDOUT_FILENO);
        dup2(fd_in[0], STDIN_FILENO);

        strcat(preview_command, " {2}");
        char* args[] = {"fzf", "--height", fzf_height, "--layout=reverse",
            "--prompt", "Template> ", "--preview", preview_command,
            "-d:", "--with-nth=3", NULL};

        execv("/usr/bin/fzf", args);
        fprintf(stderr, "execv: %s\n", strerror(errno));
        fprintf(stderr, "You are probably missing fzf!\n");
        exit(errno);
    }

    fprintf(stderr, "Unknown error!\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }
    chdir(homedir);

    FILE* file = fopen(".cptl", "r"); // TODO: Absolute/relative path.

    if (file == NULL) {
        fprintf(stderr, ".cptl: %s\n", strerror(errno));
        return errno;
    }
    char* line = fgetl(file);

    while (line) {
        FILE* f = parse_line(line);

        if (f != NULL) {
            fclose(file);
            file = f;
        }

        line = fgetl(file);
    }

    input[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0)
            return check();

        strcat(input, argv[i]);
        if (i + 1 < argc) strcat(input, " ");
    }

    strcpyl(input, input);

    for (int i = 0; i < t_len; i++) {
        static char lower[MAX_INPUT_SIZE];
        strcpyl(lower, t_list[i].name);

        if (strstr(lower, input) != NULL)
            t_id[t_id_len++] = i;
    }
    if (t_id_len == 0) {
        fprintf(stderr, "Couldn't find %s!\n", input);
        return ENOENT;
    }

    int id = 0;

    if (t_id_len == 1) {
        printf("%s\n", t_list[t_id[id]].name);
    } else if (access("/usr/bin/fz", X_OK) == 0) id = fzf_menu();
    else id = simple_menu();

    if (id < 0 || id >= t_id_len) {
        fprintf(stderr, "input: Invalid ID!\n");
        return EXIT_FAILURE;
    }

    if (freopen(t_list[t_id[id]].path, "r", stdin) == NULL) {
        fprintf(stderr, "freopen: %s\n", strerror(errno));
        fprintf(stderr, "file(): %s\n", t_list[t_id[id]].path);
        return errno;
    }

    char* args[] = {"xclip", "-i", "-selection", "clipboard", NULL};
    execv("/usr/bin/xclip", args);
    fprintf(stderr, "execv: %s\n", strerror(errno));
    fprintf(stderr, "You are probably missing xclip!\n");

    return errno;
}
