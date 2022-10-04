#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARG_LEN 32
#define MAX_LINE_LEN 512

/* -rw-r--r-- */
#define CREAT_MODE 0644

typedef enum redirection { LEFT, RIGHT, NONE } redirection;

int count_delimiter(char *, char);

int count_delimiter(char *str, char delimiter) {
  int result = 0;

  for (char *s = str; *s; s++) {
    if (*s == delimiter)
      result++;
  }

  return result;
}

/* Wrapper for strtok that returns a dynamically-allocated
   string array

   We allocate a size n + 1 array to hold pointers to our string tokens
   where n is equal to the number of delimiters in the string.
   We split on '|', and 1 '|' yields two separate commands.
   Then we allocate space for each individual string.
   To fill the string arrays we copy the token to the dynamically
   allocated memory.
*/
char **split(char *input, const char *delimiter) {
  char **result =
      malloc(sizeof(char *) * count_delimiter(input, *delimiter) + 2);
  int i = 0;

  for (char *word = strtok(input, delimiter); word;
       word = strtok(NULL, delimiter), i++) {
    result[i] = malloc(strlen(word) + 1);
    strcpy(result[i], word);
  }
  result[i] = NULL;

  return result;
}

/* Mutates original string */
char *strip(char *str) {
  /* Rare case of a single space */
  if (strlen(str) == 1) {
    if (strcmp(str, " "))
      return str;
  }

  char *post = str + strlen(str) - 1;

  while (*str == ' ')
    str++;

  while (*post == ' ')
    post--;

  *(post + 1) = '\0';
  return str;
}

void prompt() { printf("my_shell$ "); }

void read_cmd(char *cmd) {
  fgets(cmd, MAX_LINE_LEN, stdin);

  int len = strlen(cmd);

  if (cmd[len - 1] == '\n')
    cmd[len - 1] = '\0';
}

/* Given a string in the form /_/_/.../x
   return x

   May mutate original string
*/
char *get_filename(char *executable) {
  int i = strlen(executable) - 1;

  while (i > 0 && executable[i] != '/')
    i--;

  switch (i) {
  case 0:
    /* No slashes, the filename is in the correct format */
    return executable;
  case 1:
    /* TODO check for possible newline */
    return "Not implemented";
    break;
  default:
    /* Skip past the last slash */
    executable += (i + 1);
    return executable;
  }
}

void print_execute_error(char **argv) {
  fprintf(stderr, "my_shell: %s: %s\n", get_filename(argv[0]), strerror(errno));
}

redirection find_redirection(char *str) {
  if (count_delimiter(str, '<')) {
    return LEFT;
  } else if (count_delimiter(str, '>')) {
    return RIGHT;
  } else {
    return NONE;
  }
}

bool find_ampersand(char *cmd) { return count_delimiter(cmd, '&') == 1; }

void replace_char(char *str, char to_replace, char c) {
  char *s = str;

  while (*s && *s != to_replace) {
    s++;
  };

  if (*s) {
    *s = c;
  }
}

/* Execute a command given argv with stdin and stdout specified
   by in and out respectively

   This function will not be called when in and out are both
   STDIN_FILENO and STDOUT_FILENO respectively
*/
int piped_execute(int in, int out, char **argv) {
  pid_t pid;
  int wstatus;

  fflush(stdout);
  if ((pid = fork()) > 0) {
    wait(&wstatus);
  } else if (pid == 0) {
    if (in != STDIN_FILENO) {
      /* We are reading input from a previous process */
      if (dup2(in, STDIN_FILENO) == -1) {
        print_execute_error(argv);
        close(in);
        return -1;
      }

      close(in);
    }

    /* We are redirecting our output to the next process
       There is no need to check if out != STDOUT_FILENO -
       that would only be true if we were executing the last
       command in the pipeline (this case is handled separately)
    */
    if (dup2(out, STDOUT_FILENO) == -1) {
      print_execute_error(argv);
      close(out);
      return -1;
    };

    close(out);

    if (execv(argv[0], argv)) {
      print_execute_error(argv);
      return -1;
    }
  } else {
    print_execute_error(argv);
    return -1;
  }

  return pid;
}

void restore_input_output(int original_stdin, int original_stdout) {
  dup2(original_stdin, STDIN_FILENO);
  dup2(original_stdout, STDOUT_FILENO);
}

char *get_last_arg(char **argv) {
  char *arg = malloc(sizeof(char *));
  char **c = argv;

  /* Get to last argument */
  while (*(c + 1)) {
    c++;
  }
  arg = *c;

  return arg;
}

/* Find a string in an argument vector */
char *find(char **argv, char *needle) {
  char *str = malloc(sizeof(char *));
  char **c = argv;

  while (*c++) {
    if (!strcmp(*c, needle)) {
      str = *c;
    }
  }

  return str;
}

void repl(bool suppress_prompt) {
  /* In the event of an error, restore original in/out */
  const int ORIGINAL_STDIN = dup(STDIN_FILENO);
  const int ORIGINAL_STDOUT = dup(STDOUT_FILENO);

  while (!feof(stdin)) {
    char cmds[MAX_ARG_LEN] = {0};
    char **cmd_list;
    int fd[2];

    int in = STDIN_FILENO;
    int out = STDOUT_FILENO;

    bool redirect_in = false;
    bool redirect_out = false;

    bool last_command = false;

    bool wait_for_child = true;

    if (!suppress_prompt)
      prompt();

    read_cmd(cmds);

    int num_commands = count_delimiter(cmds, '|') + 1;
    /* strcpy(cmds, "/bin/echo -e a\nb\nc | grep b | cat -n"); */
    cmd_list = split(cmds, "|");

    /* If there are 2 or more commands here, they form a pipeline */
    for (char **cmd = cmd_list; *cmd; cmd++) {
      *cmd = strip(*cmd); // overwrite with split string

      switch (find_redirection(*cmd)) {
      case (RIGHT):
        redirect_out = true;
        break;
      case (LEFT):
        redirect_in = true;
        break;
      default:
        break;
      }

      if (redirect_out) {
        /* Split input of the form cmd > file */
        char **redir_args = split(*cmd, ">");
        char *file = strip(redir_args[1]);
        *cmd = strip(redir_args[0]);
        if ((out = creat(file, CREAT_MODE)) == -1) {
          perror("ERROR");
          restore_input_output(ORIGINAL_STDIN, ORIGINAL_STDOUT);
        };
        redirect_out = false;

      }

      if (redirect_in) {
        /* Split input of the form cmd < file */
        char **redir_args = split(*cmd, "<");
        char *file = strip(redir_args[1]);
        *cmd = strip(redir_args[0]);
        if ((in = open(file, O_RDONLY)) == -1) {
          perror("ERROR");
          restore_input_output(ORIGINAL_STDIN, ORIGINAL_STDOUT);
        };

        /* Commands like x < y | z will break without this */
        redirect_in = false;
      }

      if (find_ampersand(*cmd)) {
        wait_for_child = false;
        replace_char(*cmd, '&', '\0');
        strip(*cmd);
      }

      char **argv = split(*cmd, " ");
      pipe(fd);

      if (!*(cmd + 1))
        last_command = true;

      if (!last_command) {
        if (piped_execute(in, fd[1], argv) == -1) {
          restore_input_output(ORIGINAL_STDIN, ORIGINAL_STDOUT);
          break;
        }

        close(fd[1]);

        /* Preserve the read end of the pipe - the previous child's output is
         * here */
        in = fd[0];
      } else {
        pid_t pid;
        int wstatus;

        /* We need one more process to execute the last command */
        if ((pid = fork()) > 0) {
          if (wait_for_child) {
            wait(&wstatus);
          } else {
            /* SA_NOCLDWAIT - If signum is SIGCHLD, do not transform children into zombies when
               they terminate. This flag is meaningful only when establishing a handler
               for SIGCHLD, or when setting that signal's disposition to SIG_DFL.
            */
            struct sigaction no_zombie = {
              .sa_handler = SIG_DFL,
              .sa_flags = SA_NOCLDWAIT
            };
            sigaction(SIGCHLD, &no_zombie, NULL);
          }
        } else if (pid == 0) {
          /* Read output from previous child (or terminal if we aren't in a pipeline
             Note that in this process, stdout is still default (terminal)
             unless we are redirecting to a file (>)
          */
          if (in != STDIN_FILENO)
            dup2(in, STDIN_FILENO);

          /* Check if we are redirecting to a file. We assume redirection can
             only happen as the last command in a pipeline!
          */
          if (out != STDOUT_FILENO) {
            dup2(out, STDOUT_FILENO);
          }

          if (execv(argv[0], argv))
            print_execute_error(argv);

        } else {
          print_execute_error(argv);
        }
      }
    }
  }
}

int main(int argc, char *argv[]) {
  bool suppress_prompt;

  if (argc == 2) {
    if (!strcmp(argv[1], "-n"))
      suppress_prompt = true;
    else {
      printf("Invalid argument %s\n", argv[1]);
      exit(-1);
    }
  } else {
    suppress_prompt = false;
  }

  repl(suppress_prompt);
  return 0;
}
