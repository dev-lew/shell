#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_ARG_LEN 32
#define MAX_LINE_LEN 512

int count_delimiter(char *, char);

/* Wrapper for strtok that returns a dynamically-allocated
   string array

   We allocate a size n + 1 array to hold pointers to our string tokens
   where n is equal to the number of delimiters in the string.
   We split on '|', and 1 '|' yields two separate commands.
   Then we allocate space for each individual string.
   To fill the string arrays we copy the token to the dynamically
   allocated memory.
*/
char **split(char *input, const char* delimiter) {
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

int count_delimiter(char *str, char delimiter) {
  int result = 0;

  for (char *s = str; *s; s++) {
    if (*s == delimiter)
      result++;
  }

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
  /* if (!fgets(cmd, MAX_ARG_LEN, stdin)) { */
  /*   perror("Failed to read input"); */
  /*   exit(-1); */
  /* } */

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

void repl(bool suppress_prompt) {
  while (!feof(stdin)) {
    char cmds[MAX_ARG_LEN] = {0};
    char **cmd_list;

    if (!suppress_prompt)
      prompt();

    read_cmd(cmds);
    /* strcpy(cmds, "/bin/echo -e a\nb\nc | grep b | cat -n"); */

    cmd_list = split(cmds, "|");
    /* for (char **cmd = cmd_list; *cmd; cmd++) { */
    /*   // For first deadline */
    /*   *cmd = strip(*cmd); */
    /*   printf("%s\n", *cmd); */
    /*   /\* printf("%zu\n", strlen(*cmd)); *\/ */
    /* } */

    for (char **cmd = cmd_list; *cmd; cmd++) {
      *cmd = strip(*cmd); // overwrite with split string

      char **argv = split(*cmd, " ");
      char executable[strlen(argv[0]) + 1];
      strcpy(executable, argv[0]);

      /* Overwrite possible / form of the executable
         in argv (the new len will be <= to the original) */
      argv[0] = get_filename(argv[0]);
      /* printf("%s\n", argv[0]); */
      /* printf("%s\n", argv[1]); */
      /* printf("%s\n", executable); */

      pid_t pid;
      int wstatus;

      fflush(stdout);
      if ((pid = fork()) > 0) {
        // Parent process
        wait(&wstatus);

      } else if (pid == 0) {
        // int estatus;
        if (execv(executable, argv))
          printf("my_shell: %s: %s\n", argv[0], strerror(errno));
      } else {
        // error
        printf("Error");
      }
      /* for mini deadline */
      break;
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
