#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_COMMANDS 16
#define MAX_ARGS 64

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} TokenList;

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
    int append_output;
} Command;

typedef struct {
    Command commands[MAX_COMMANDS];
    int command_count;
} Pipeline;

static void free_tokens(TokenList *tokens)
{
    for (size_t i = 0; i < tokens->count; i++) {
        free(tokens->items[i]);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

static int push_token(TokenList *tokens, const char *text, size_t length)
{
    char *copy;
    char **new_items;
    size_t new_capacity;

    if (tokens->count == tokens->capacity) {
        new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
        new_items = realloc(tokens->items, new_capacity * sizeof(tokens->items[0]));
        if (new_items == NULL) {
            perror("realloc");
            return -1;
        }
        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }

    copy = malloc(length + 1);
    if (copy == NULL) {
        perror("malloc");
        return -1;
    }

    if (length > 0) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    tokens->items[tokens->count++] = copy;
    return 0;
}

static int append_char(char **buffer, size_t *length, size_t *capacity, char ch)
{
    char *new_buffer;
    size_t new_capacity;

    if (*length + 1 >= *capacity) {
        new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        new_buffer = realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            perror("realloc");
            return -1;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    (*buffer)[(*length)++] = ch;
    return 0;
}

static int finish_current_token(TokenList *tokens, char *buffer, size_t *length, int *started)
{
    if (!*started) {
        return 0;
    }

    if (push_token(tokens, buffer, *length) == -1) {
        return -1;
    }

    *length = 0;
    *started = 0;
    return 0;
}

static int tokenize_line(const char *line, TokenList *tokens)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int started = 0;
    char quote = '\0';

    for (size_t i = 0; line[i] != '\0'; i++) {
        char ch = line[i];

        if (quote == '\0' && isspace((unsigned char)ch)) {
            if (finish_current_token(tokens, buffer, &length, &started) == -1) {
                free(buffer);
                return -1;
            }
            continue;
        }

        if (quote == '\0' && (ch == '|' || ch == '<' || ch == '>')) {
            if (finish_current_token(tokens, buffer, &length, &started) == -1) {
                free(buffer);
                return -1;
            }

            if (ch == '>' && line[i + 1] == '>') {
                if (push_token(tokens, ">>", 2) == -1) {
                    free(buffer);
                    return -1;
                }
                i++;
            } else if (push_token(tokens, &ch, 1) == -1) {
                free(buffer);
                return -1;
            }
            continue;
        }

        if (ch == '\'' || ch == '"') {
            if (quote == '\0') {
                quote = ch;
                started = 1;
                continue;
            }
            if (quote == ch) {
                quote = '\0';
                continue;
            }
        }

        if (ch == '\\' && quote != '\'' && line[i + 1] != '\0') {
            i++;
            ch = line[i];
        }

        started = 1;
        if (append_char(&buffer, &length, &capacity, ch) == -1) {
            free(buffer);
            return -1;
        }
    }

    if (quote != '\0') {
        fprintf(stderr, "mini-shell: missing closing quote\n");
        free(buffer);
        return -1;
    }

    if (finish_current_token(tokens, buffer, &length, &started) == -1) {
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
}

static int is_operator(const char *token)
{
    return strcmp(token, "|") == 0 ||
           strcmp(token, "<") == 0 ||
           strcmp(token, ">") == 0 ||
           strcmp(token, ">>") == 0;
}

static void init_pipeline(Pipeline *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->command_count = 1;
}

static int add_argument(Command *command, char *arg)
{
    if (command->argc >= MAX_ARGS - 1) {
        fprintf(stderr, "mini-shell: too many arguments in command\n");
        return -1;
    }

    command->argv[command->argc++] = arg;
    command->argv[command->argc] = NULL;
    return 0;
}

static int parse_tokens(const TokenList *tokens, Pipeline *pipeline)
{
    Command *command;

    init_pipeline(pipeline);
    command = &pipeline->commands[0];

    for (size_t i = 0; i < tokens->count; i++) {
        char *token = tokens->items[i];

        if (strcmp(token, "|") == 0) {
            if (command->argc == 0) {
                fprintf(stderr, "mini-shell: empty command near '|'\n");
                return -1;
            }
            if (pipeline->command_count >= MAX_COMMANDS) {
                fprintf(stderr, "mini-shell: too many pipeline commands\n");
                return -1;
            }
            command = &pipeline->commands[pipeline->command_count++];
            continue;
        }

        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
            int is_input = strcmp(token, "<") == 0;
            int is_append = strcmp(token, ">>") == 0;

            if (i + 1 >= tokens->count || is_operator(tokens->items[i + 1])) {
                fprintf(stderr, "mini-shell: missing file name after '%s'\n", token);
                return -1;
            }

            i++;
            if (is_input) {
                command->input_file = tokens->items[i];
            } else {
                command->output_file = tokens->items[i];
                command->append_output = is_append;
            }
            continue;
        }

        if (add_argument(command, token) == -1) {
            return -1;
        }
    }

    if (command->argc == 0) {
        fprintf(stderr, "mini-shell: empty command at end of pipeline\n");
        return -1;
    }

    return 0;
}

static void close_pipes(int pipes[][2], int pipe_count)
{
    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i][0] != -1) {
            close(pipes[i][0]);
        }
        if (pipes[i][1] != -1) {
            close(pipes[i][1]);
        }
    }
}

static void redirect_input(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror(path);
        _exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDIN_FILENO) == -1) {
        perror("dup2");
        close(fd);
        _exit(EXIT_FAILURE);
    }

    close(fd);
}

static void redirect_output(const char *path, int append)
{
    int flags = O_WRONLY | O_CREAT;
    int fd;

    flags |= append ? O_APPEND : O_TRUNC;
    fd = open(path, flags, 0644);
    if (fd == -1) {
        perror(path);
        _exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        close(fd);
        _exit(EXIT_FAILURE);
    }

    close(fd);
}

static void run_child(const Pipeline *pipeline, int index, int pipes[][2], int pipe_count)
{
    const Command *command = &pipeline->commands[index];

    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    if (index > 0 && dup2(pipes[index - 1][0], STDIN_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }

    if (index < pipeline->command_count - 1 && dup2(pipes[index][1], STDOUT_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }

    close_pipes(pipes, pipe_count);

    if (command->input_file != NULL) {
        redirect_input(command->input_file);
    }
    if (command->output_file != NULL) {
        redirect_output(command->output_file, command->append_output);
    }

    execvp(command->argv[0], command->argv);
    perror(command->argv[0]);
    _exit(127);
}

static int execute_pipeline(const Pipeline *pipeline)
{
    int pipes[MAX_COMMANDS - 1][2];
    pid_t pids[MAX_COMMANDS];
    int pipe_count = pipeline->command_count - 1;
    int last_status = 0;

    for (int i = 0; i < MAX_COMMANDS - 1; i++) {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }

    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            close_pipes(pipes, pipe_count);
            return 1;
        }
    }

    for (int i = 0; i < pipeline->command_count; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork");
            close_pipes(pipes, pipe_count);
            for (int j = 0; j < i; j++) {
                waitpid(pids[j], NULL, 0);
            }
            return 1;
        }

        if (pids[i] == 0) {
            run_child(pipeline, i, pipes, pipe_count);
        }
    }

    close_pipes(pipes, pipe_count);

    for (int i = 0; i < pipeline->command_count; i++) {
        int status;

        while (waitpid(pids[i], &status, 0) == -1) {
            if (errno != EINTR) {
                perror("waitpid");
                return 1;
            }
        }

        if (i == pipeline->command_count - 1) {
            if (WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                last_status = 128 + WTERMSIG(status);
                fprintf(stderr, "mini-shell: process terminated by signal %d\n", WTERMSIG(status));
            }
        }
    }

    return last_status;
}

static int handle_builtin(const Command *command, int *should_exit)
{
    if (strcmp(command->argv[0], "exit") == 0 || strcmp(command->argv[0], "quit") == 0) {
        *should_exit = 1;
        return 1;
    }

    if (strcmp(command->argv[0], "cd") == 0) {
        const char *target;

        if (command->argc > 2) {
            fprintf(stderr, "mini-shell: cd: too many arguments\n");
            return 1;
        }

        target = command->argc == 2 ? command->argv[1] : getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "mini-shell: cd: HOME is not set\n");
            return 1;
        }

        if (chdir(target) == -1) {
            perror("cd");
        }
        return 1;
    }

    return 0;
}

static void install_shell_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
}

int main(void)
{
    char *line = NULL;
    size_t line_capacity = 0;
    int should_exit = 0;

    install_shell_signal_handlers();

    while (!should_exit) {
        ssize_t bytes_read;
        TokenList tokens = {0};
        Pipeline pipeline;

        printf("mini-shell$ ");
        fflush(stdout);

        bytes_read = getline(&line, &line_capacity, stdin);
        if (bytes_read == -1) {
            if (errno == EINTR) {
                clearerr(stdin);
                putchar('\n');
                continue;
            }
            putchar('\n');
            break;
        }

        if (bytes_read > 0 && line[bytes_read - 1] == '\n') {
            line[bytes_read - 1] = '\0';
        }

        if (tokenize_line(line, &tokens) == -1) {
            free_tokens(&tokens);
            continue;
        }

        if (tokens.count == 0) {
            free_tokens(&tokens);
            continue;
        }

        if (parse_tokens(&tokens, &pipeline) == -1) {
            free_tokens(&tokens);
            continue;
        }

        if (pipeline.command_count == 1 &&
            handle_builtin(&pipeline.commands[0], &should_exit)) {
            free_tokens(&tokens);
            continue;
        }

        execute_pipeline(&pipeline);
        free_tokens(&tokens);
    }

    free(line);
    return 0;
}
