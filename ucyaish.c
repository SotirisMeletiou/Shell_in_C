/**

ATTENTION commands  with semicolon needs to be seperated ls ; cd .. 
and maybe some commands needs to be like this
but cut -f -d works without spaces

*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h> 

#define MAX_INPUT_SIZE 1024
#define MAX_TOKENS 100

char* global_variable = NULL; 

typedef struct {
    char* command;
} HistoryEntry;

// Global array to store command history
HistoryEntry* command_history = NULL;
int history_size = 0;

void execute_command(char *tokens[], int run_in_background) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
    } else if (pid == 0) { // Child process
        if (run_in_background) {
            if (setsid() == -1) {
                perror("setsid");
                exit(EXIT_FAILURE);
            }
        }

        // Check for input and output redirection
        for (int i = 0; tokens[i] != NULL; ++i) {
            if (strcmp(tokens[i], "<") == 0) {
                int fd = open(tokens[i + 1], O_RDONLY);
                if (fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
                tokens[i] = NULL; // Remove "<" and the filename from the command
                break;
            } else if (strcmp(tokens[i], ">") == 0) {
                int fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                tokens[i] = NULL; // Remove ">" and the filename from the command
                break;
            } else if (strcmp(tokens[i], ">>") == 0) {
                int fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0666);
                if (fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
                tokens[i] = NULL; // Remove ">>" and the filename from the command
                break;
            } else if (strcmp(tokens[i], "2>") == 0) {
                int fd = open(tokens[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
                tokens[i] = NULL; // Remove "2>" and the filename from the command
                break;
            }
        }

        execvp(tokens[0], tokens);
        perror("execvp"); // This line is reached only if execvp fails
        exit(EXIT_FAILURE);
    } else { // Parent process
        if (!run_in_background) {
            waitpid(pid, NULL, 0);
        }
    }
}

void execute_command_with_pipes(char *tokens[], int run_in_background) {
    int num_pipes = 0;

    // Count the number of pipes
    for (int i = 0; tokens[i] != NULL; ++i) {
        if (strcmp(tokens[i], "|") == 0) {
            num_pipes++;
        }
    }

    int pipe_fd[2 * num_pipes]; // Two file descriptors for each pipe
    for (int i = 0; i < num_pipes; ++i) {
        if (pipe(pipe_fd + 2 * i) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    int token_index = 0;
    for (int pipe_num = 0; pipe_num <= num_pipes; ++pipe_num) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // Child process
            if (pipe_num < num_pipes) {
                dup2(pipe_fd[2 * pipe_num + 1], STDOUT_FILENO);
            }

            if (pipe_num > 0) {
                dup2(pipe_fd[2 * (pipe_num - 1)], STDIN_FILENO);
            }

            // Close all pipe file descriptors in the child process
            for (int i = 0; i < 2 * num_pipes; ++i) {
                close(pipe_fd[i]);
            }

            // Execute the command part
            char *command_tokens[MAX_TOKENS];
            int command_token_index = 0;

            while (tokens[token_index] != NULL && strcmp(tokens[token_index], "|") != 0) {
                command_tokens[command_token_index++] = tokens[token_index++];
            }

            command_tokens[command_token_index] = NULL;

            // Use execvp for command execution
            execvp(command_tokens[0], command_tokens);

            // If execvp fails, print an error message and exit
            perror("execvp");
            exit(EXIT_FAILURE);
        } else { // Parent process
            // Close the appropriate pipe file descriptors in the parent process
            if (pipe_num < num_pipes) {
                close(pipe_fd[2 * pipe_num + 1]);
            }

            if (pipe_num > 0) {
                close(pipe_fd[2 * (pipe_num - 1)]);
            }

            // Move to the next part of the command
            while (tokens[token_index] != NULL && strcmp(tokens[token_index], "|") != 0) {
                token_index++;
            }

            if (tokens[token_index] != NULL) {
                token_index++; // Skip the "|"
            }
        }
    }

    // Close all remaining pipe file descriptors in the parent process
    for (int i = 0; i < 2 * num_pipes; ++i) {
        close(pipe_fd[i]);
    }

    if (!run_in_background) {
        // Wait for all child processes to finish, unless it's a background process
        for (int i = 0; i <= num_pipes; ++i) {
            wait(NULL);
        }
    }
}

void handle_cd(char* tokens[]) {
    char* target_directory = (tokens[1] != NULL) ? tokens[1] : getenv("HOME");

    if (target_directory == NULL) {
        fprintf(stderr, "ucysh: cd: HOME not set\n");
    } else {
        char* current_directory = getcwd(NULL, 0);

        if (chdir(target_directory) != 0) {
            perror("ucysh");
        } else {
            // Update the environment variables
            setenv("OLDPWD", current_directory, 1);
            setenv("PWD", getcwd(NULL, 0), 1);
        }

        free(current_directory);
    }
}

void handle_set(char* tokens[]) {
    if (tokens[1] != NULL && tokens[2] != NULL) {
        // Set environment variable using setenv
        setenv(tokens[1], tokens[2], 1);
        printf("Set environment variable: %s=%s\n", tokens[1], getenv(tokens[1]));
    } else {
        fprintf(stderr, "ucysh: set: missing arguments\n");
    }
}

void handle_echo(char* tokens[]) {
    for (int i = 1; tokens[i] != NULL; ++i) {
        if (tokens[i][0] == '$' && tokens[i][1] != '\0') {
            // Expand environment variable using getenv
            char* variable_name = tokens[i] + 1;
            char* variable_value = getenv(variable_name);
            if (variable_value != NULL) {
                printf("%s", variable_value);
            } else {
                printf("$%s", variable_name);
            }
        } else {
            printf("%s", tokens[i]);
        }

        if (tokens[i + 1] != NULL) {
            printf(" ");
        }
    }
      printf("\n");
}

void handle_logout(char* tokens[]) {
    // Simulate the logout built-in function
    exit(EXIT_SUCCESS);
}

void handle_declare(char* tokens[]) {
    // Simulate the declare built-in function
    int is_integer = 0;

    // Check for the -i option
    if (tokens[1] != NULL && strcmp(tokens[1], "-i") == 0) {
        is_integer = 1;
        tokens++; // Skip the -i option
    }

    for (int i = 1; tokens[i] != NULL; ++i) {
        char* variable_name = tokens[i];

        // Check if the token contains '='
        char* equal_sign = strchr(variable_name, '=');

        if (equal_sign != NULL) {
            if (equal_sign == variable_name) {
                fprintf(stderr, "ucysh: declare: invalid syntax: %s\n", variable_name);
            } else {
                *equal_sign = '\0'; // Split the token into variable name and value
                char* value = equal_sign + 1;

                if (is_integer) {
                    // Handle integer case
                    int num = atoi(value);
                    printf("Declared integer variable: %s=%d\n", variable_name, num);
                } else {
                    // Handle regular case
                    setenv(variable_name, value, 1);
                    printf("Declared environment variable: %s=%s\n", variable_name, value);
                }
            }
        }
    }
}

void add_to_history(const char* command) {
    command_history = realloc(command_history, (history_size + 1) * sizeof(HistoryEntry));

    if (command_history == NULL) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }

    command_history[history_size].command = strdup(command);

    if (command_history[history_size].command == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    history_size++;
}

void handle_history(char* tokens[]) {
    // Print the command history
    printf("Command history:\n");

    for (int i = 0; i < history_size; i++) {
        printf("%d. %s\n", i + 1, command_history[i].command);
    }
}

// Cleanup function to free memory used by command history
void cleanup_history() {
    for (int i = 0; i < history_size; i++) {
        free(command_history[i].command);
    }

    free(command_history);
}

void handle_kill(char* tokens[]) {
    // Simulate the kill built-in function
    // Implement your kill functionality here
    if (tokens[1] != NULL) {
        int pid = atoi(tokens[1]);
        if (pid > 0) {
            if (kill(pid, SIGTERM) == 0) {
                printf("Process %d terminated\n", pid);
            } else {
                perror("kill");
            }
        } else {
            fprintf(stderr, "Invalid process ID\n");
        }
    } else {
        fprintf(stderr, "Usage: kill <pid>\n");
    }
}

void handle_let(char* tokens[]) {
    // Simulate the let built-in function
    // Implement your let functionality here
    if (tokens[1] != NULL && tokens[2] != NULL) {
        int result = atoi(tokens[1]) + atoi(tokens[2]);
        printf("%d\n", result);
    } else {
        fprintf(stderr, "Usage: let <num1> <num2>\n");
    }
}

void handle_local(char* tokens[]) {
    // Simulate the local built-in function
    // Implement your local functionality here
    if (tokens[1] != NULL) {
        printf("Local variable: %s\n", tokens[1]);
        // You might want to handle local variables here
    } else {
        fprintf(stderr, "Usage: local <variable>\n");
    }
}

void handle_read(char* tokens[]) {
    char buffer[MAX_INPUT_SIZE];

    // Check if there are enough arguments
    if (tokens[1] == NULL) {
        fprintf(stderr, "ucysh: read: missing variable name\n");
        return;
    }

    char* prompt = NULL;
    int option_index = 1;

    // Check for options
    while (tokens[option_index] != NULL && tokens[option_index][0] == '-') {
        if (strcmp(tokens[option_index], "-p") == 0) {
            // -p option for specifying a prompt
            option_index++;
            if (tokens[option_index] != NULL) {
                prompt = tokens[option_index];
            } else {
                fprintf(stderr, "ucysh: read: missing prompt after -p\n");
                return;
            }
        } else {
            fprintf(stderr, "ucysh: read: unknown option: %s\n", tokens[option_index]);
            return;
        }

        option_index++;
    }

    // Print the prompt if provided
    if (prompt != NULL) {
        printf("%s ", prompt);
        fflush(stdout);
    }

    // Read input
    if (fgets(buffer, MAX_INPUT_SIZE, stdin) != NULL) {
        // Remove newline character
        buffer[strcspn(buffer, "\n")] = '\0';

        // Remove surrounding quotes if present
        if (strlen(buffer) >= 2 && buffer[0] == '"' && buffer[strlen(buffer) - 1] == '"') {
            memmove(buffer, buffer + 1, strlen(buffer) - 1);
            buffer[strlen(buffer) - 1] = '\0';
        }

        // Set the variable specified in the command line
        setenv(tokens[option_index], buffer, 1);
    } else {
        fprintf(stderr, "ucysh: read: error reading input\n");
    }
}

void display_prompt() {
    // Fetch values for environment variables
    char* PS1 = getenv("PS1");
    char* PWD = getcwd(NULL, 0);
    char* OLD_PWD = getenv("OLDPWD");
    char* HOSTNAME = getenv("HOSTNAME");
    
    if (PS1 == NULL) {
        PS1 = "ucysh> ";
        setenv("PS1", PS1, 1);
    }

    if (PWD == NULL) {
        PWD = getcwd(NULL, 0);
        setenv("PWD", PWD, 1);
    }

    if (OLD_PWD == NULL) {
        OLD_PWD = PWD;
        setenv("OLDPWD", OLD_PWD, 1);
    }

    if (HOSTNAME == NULL) {
        HOSTNAME = "ucysh";
        setenv("HOSTNAME", HOSTNAME, 1);
    }
    
    setenv("PWD", PWD, 1);

    // Display the prompt
    printf("%s", PS1);
    fflush(stdout);
}

int tokenize_input(char *input, char *tokens[]) {
    // Tokenize input with quotes and optional spaces around delimiters
    int token_count = 0;
    char *token = strtok(input, "\" \t\n");
    while (token != NULL && token_count < MAX_TOKENS - 1) {
        // Check for quotes and handle accordingly
        if (token[0] == '"' && token[strlen(token) - 1] == '"') {
            // Remove surrounding quotes
            token[strlen(token) - 1] = '\0';
            tokens[token_count++] = token + 1;
        } else {
            // Check for optional spaces around delimiters
            char *delimiter = strchr(token, '=');
            if (delimiter != NULL) {
                // Split the token into parts separated by '='
                *delimiter = '\0';
                tokens[token_count++] = token;
                tokens[token_count++] = delimiter + 1;
            } else {
                tokens[token_count++] = token;
            }
        }

        token = strtok(NULL, "\" \t\n");
    }

    tokens[token_count] = NULL; // Null-terminate the array

    return token_count;
}

void execute_command_tokens(char *command_tokens[], int background) {
    // Check if it's a built-in command and execute accordingly
    if (strcmp(command_tokens[0], "exit") == 0) {
        exit(EXIT_SUCCESS);
    } else if (strcmp(command_tokens[0], "cd") == 0) {
        handle_cd(command_tokens);
    } else if (strcmp(command_tokens[0], "set") == 0) {
        handle_set(command_tokens);
    } else if (strcmp(command_tokens[0], "echo") == 0) {
        handle_echo(command_tokens);
    } else if (strcmp(command_tokens[0], "declare") == 0) {
        handle_declare(command_tokens);
    } else if (strcmp(command_tokens[0], "history") == 0) {
        handle_history(command_tokens);
    } else if (strcmp(command_tokens[0], "kill") == 0) {
        handle_kill(command_tokens);
    } else if (strcmp(command_tokens[0], "let") == 0) {
        handle_let(command_tokens);
    } else if (strcmp(command_tokens[0], "local") == 0) {
        handle_local(command_tokens);
    } else if (strcmp(command_tokens[0], "logout") == 0) {
        handle_logout(command_tokens);
    } else if (strcmp(command_tokens[0], "read") == 0) {
        handle_read(command_tokens);
    } else {
        // It's not a built-in command, execute it as usual
        int pipe_index = -1;
        for (int i = 0; command_tokens[i] != NULL; ++i) {
            if (strcmp(command_tokens[i], "|") == 0) {
                pipe_index = i;
                break;
            }
        }

        if (pipe_index != -1) {
            // Handle command with multiple pipes
            execute_command_with_pipes(command_tokens, background);
        } else {
            // Handle normal command without pipe
            execute_command(command_tokens, background);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        if (strcmp(argv[1], "-m") == 0) {
            // Run the main interactive shell loop
            char input[MAX_INPUT_SIZE];
            char *tokens[MAX_TOKENS];

            while (1) {
                display_prompt();

                if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
                    perror("fgets");
                    exit(EXIT_FAILURE);
                }

                // Remove newline character
                input[strcspn(input, "\n")] = '\0';

                int token_count = tokenize_input(input, tokens);

                if (token_count > 0) {
                    int background = 0;
                    if (strcmp(tokens[token_count - 1], "&") == 0) {
                        background = 1;
                        tokens[token_count - 1] = NULL; // Remove "&" from the command
                    }

                    // Handle normal command without pipe
                    if (token_count > 0) {
                        int command_index = 0;

                        while (tokens[command_index] != NULL) {
                            // Find the end of the current command (up to the next semicolon or NULL)
                            int end_index = command_index;
                            while (tokens[end_index] != NULL && strcmp(tokens[end_index], ";") != 0) {
                                end_index++;
                            }

                            // Extract the current command tokens
                            char *command_tokens[MAX_TOKENS];
                            int command_token_index = 0;

                            for (int i = command_index; i < end_index; ++i) {
                                command_tokens[command_token_index++] = tokens[i];
                            }

                            command_tokens[command_token_index] = NULL;
                            execute_command_tokens(command_tokens, background);
                            command_index = (tokens[end_index] != NULL) ? end_index + 1 : end_index;
                        }
                    }
                }
            }

            cleanup_history();
        } else if (strcmp(argv[1], "-a") == 0) {
            printf("Sorry, this functionality was not implemented.\n");
        } else {
            printf("Invalid command-line argument. Usage: %s [-m | -a]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        printf("Incorrect number of command-line arguments. Usage: %s [-m | -a]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    return 0;
}
