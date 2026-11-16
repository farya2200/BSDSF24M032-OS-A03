
#include "shell.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <ctype.h>

static void show_shell_variables(void);

/* Command completion */

static char *command_generator(const char *text, int state) {
    static const char *commands[] = {
        "cd", "exit", "help", "jobs", "history",
        "pwd", "whoami", "date", "ls", "cat",
        "mkdir", "rm", "cp", "mv", "clear", "echo",
        "set", NULL
    };

    static int index, len;

    if (!state) {
        index = 0;
        len = strlen(text);
    }

    while (commands[index] != NULL) {
        const char *cmd = commands[index++];

        if (strncmp(cmd, text, len) == 0)
            return strdup(cmd);
    }

    return NULL;
}

static char **shell_completion(const char *text, int start, int end) {
    (void)end;

    if (start == 0)
        return rl_completion_matches(text, command_generator);

    return rl_completion_matches(text, rl_filename_completion_function);
}

/* Read command */

char* read_cmd(char* prompt, FILE* fp) {
    rl_attempted_completion_function = shell_completion;
    rl_bind_key('\t', rl_complete);
    (void)fp;

    return readline(prompt);
}

/* Tokenize command */

char** tokenize(char* cmdline) {
    if (cmdline == NULL || cmdline[0] == '\0')
        return NULL;

    char** args = calloc(MAXARGS + 1, sizeof(char*));

    if (args == NULL)
        return NULL;

    int count = 0;
    char* p = cmdline;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        if (*p == '<' || *p == '>' || *p == '|' || *p == '&') {
            if (count >= MAXARGS)
                break;

            args[count++] = strndup(p, 1);
            p++;
            continue;
        }

        char token[ARGLEN * 8];
        int len = 0;
        char quote = '\0';

        while (*p) {
            if (quote) {
                if (*p == quote) {
                    quote = '\0';
                    p++;
                } else if (len < (int)sizeof(token) - 1) {
                    token[len++] = *p++;
                } else {
                    p++;
                }
            } else if (*p == '"' || *p == '\'') {
                quote = *p++;
            } else if (*p == ' ' || *p == '\t' ||
                       *p == '<' || *p == '>' ||
                       *p == '|' || *p == '&') {
                break;
            } else if (len < (int)sizeof(token) - 1) {
                token[len++] = *p++;
            } else {
                p++;
            }
        }

        token[len] = '\0';

        if (len > 0 && count < MAXARGS)
            args[count++] = strdup(token);
    }

    args[count] = NULL;

    if (count == 0) {
        free(args);
        return NULL;
    }

    return args;
}

/* Shell variable implementation */

typedef struct ShellVariable {
    char *name;
    char *value;
    struct ShellVariable *next;
} ShellVariable;

static ShellVariable *variables = NULL;

static int valid_variable_name(const char *name, size_t length) {
    if (length == 0 ||
        !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;

    for (size_t i = 1; i < length; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
            return 0;
    }

    return 1;
}

int is_variable_assignment(const char *token) {
    if (token == NULL)
        return 0;

    const char *equal = strchr(token, '=');

    if (equal == NULL)
        return 0;

    return valid_variable_name(token, (size_t)(equal - token));
}

int assign_shell_variable(const char *token) {
    const char *equal = strchr(token, '=');
    size_t name_length = (size_t)(equal - token);

    char *name = strndup(token, name_length);
    char *value = strdup(equal + 1);

    if (name == NULL || value == NULL) {
        free(name);
        free(value);
        return 1;
    }

    ShellVariable *current = variables;

    /* Update an existing variable */

    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            free(current->value);
            current->value = value;
            free(name);
            return 0;
        }

        current = current->next;
    }

    /* Create a new variable */

    ShellVariable *new_variable = malloc(sizeof(ShellVariable));

    if (new_variable == NULL) {
        free(name);
        free(value);
        return 1;
    }

    new_variable->name = name;
    new_variable->value = value;
    new_variable->next = variables;

    variables = new_variable;

    return 0;
}

static const char *get_shell_variable(const char *name) {
    ShellVariable *current = variables;

    while (current != NULL) {
        if (strcmp(current->name, name) == 0)
            return current->value;

        current = current->next;
    }

    return "";
}

void expand_shell_variables(char **arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        const char *input = arglist[i];
        size_t capacity = strlen(input) + 1;
        size_t used = 0;

        char *result = malloc(capacity);
        if (result == NULL)
            continue;

        for (size_t j = 0; input[j] != '\0';) {
            if (input[j] != '$') {
                if (used + 2 > capacity) {
                    capacity *= 2;
                    char *temp = realloc(result, capacity);
                    if (temp == NULL) {
                        free(result);
                        result = NULL;
                        break;
                    }
                    result = temp;
                }
                result[used++] = input[j++];
                continue;
            }

            size_t start = j + 1;
            size_t end = start;
            int braced = 0;

            if (input[start] == '{') {
                braced = 1;
                start++;
                end = start;

                while (input[end] != '\0' && input[end] != '}')
                    end++;

                if (input[end] != '}') {
                    result[used++] = input[j++];
                    continue;
                }
            } else {
                while (isalnum((unsigned char)input[end]) ||
                       input[end] == '_')
                    end++;
            }

            if (!valid_variable_name(input + start, end - start)) {
                result[used++] = input[j++];
                continue;
            }

            char *name = strndup(input + start, end - start);
            if (name == NULL)
                break;

            const char *value = get_shell_variable(name);
            free(name);

            size_t value_length = strlen(value);

            while (used + value_length + 1 > capacity) {
                capacity *= 2;
                char *temp = realloc(result, capacity);
                if (temp == NULL) {
                    free(result);
                    result = NULL;
                    break;
                }
                result = temp;
            }

            if (result == NULL)
                break;

            memcpy(result + used, value, value_length);
            used += value_length;

            j = braced ? end + 1 : end;
        }

        if (result != NULL) {
            result[used] = '\0';
            free(arglist[i]);
            arglist[i] = result;
        }
    }
}
static void show_shell_variables(void) {
    ShellVariable *current = variables;

    while (current != NULL) {
        printf("%s=%s\n", current->name, current->value);
        current = current->next;
    }
}

/* Built-in commands */

int handle_builtin(char** arglist) {
    if (strcmp(arglist[0], "exit") == 0) {
        printf("Shell exited.\n");
        exit(0);
    }
    else if (strcmp(arglist[0], "cd") == 0) {
        char* dir = arglist[1] ? arglist[1] : getenv("HOME");

        if (chdir(dir) != 0)
            perror("cd");

        return 1;
    }
    else if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("exit - Exit the shell\n");
        printf("cd <directory> - Change directory\n");
        printf("help - Display this help message\n");
        printf("jobs - Display job control status\n");
        printf("history - Display command history\n");
        printf("set - Display shell variables\n");
        printf("!n - Execute a previous command\n");

        return 1;
    }
    else if (strcmp(arglist[0], "history") == 0) {
        show_history();
        return 1;
    }
    else if (strcmp(arglist[0], "jobs") == 0) {
        show_jobs();
        return 1;
    }
    else if (strcmp(arglist[0], "set") == 0) {
        show_shell_variables();
        return 1;
    }

    return 0;
}

/* Command history */

#define HISTORY_SIZE 20

static char* command_history[HISTORY_SIZE];
static int history_count = 0;

void store_history(char* cmd) {
    if (history_count == HISTORY_SIZE) {
        free(command_history[0]);

        for (int i = 1; i < HISTORY_SIZE; i++)
            command_history[i - 1] = command_history[i];

        history_count--;
    }

    command_history[history_count++] = strdup(cmd);
    add_history(cmd);
}

void show_history(void) {
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, command_history[i]);
}

char* get_history(int n) {
    if (n < 1 || n > history_count)
        return NULL;

    return command_history[n - 1];
}

/* Background jobs */

#define MAX_JOBS 100

static BackgroundJob background_jobs[MAX_JOBS];
static int job_count = 0;

void add_background_job(pid_t pid, const char* command) {
    if (job_count >= MAX_JOBS) {
        printf("Background job list is full.\n");
        return;
    }

    background_jobs[job_count].pid = pid;

    snprintf(background_jobs[job_count].command,
             MAX_LEN, "%s", command);

    job_count++;

    printf("[%d] %d running in background\n", job_count, pid);
}

void reap_background_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (background_jobs[i].pid == pid) {
                printf("\n[%d] %d completed: %s\n",
                       i + 1, pid, background_jobs[i].command);

                for (int j = i; j < job_count - 1; j++)
                    background_jobs[j] = background_jobs[j + 1];

                job_count--;
                break;
            }
        }
    }
}

void show_jobs(void) {
    if (job_count == 0) {
        printf("No background jobs.\n");
        return;
    }

    for (int i = 0; i < job_count; i++) {
        printf("[%d] PID: %d  Running: %s\n",
               i + 1,
               background_jobs[i].pid,
               background_jobs[i].command);
    }
}
