#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/wait.h>


/*-----------------parseState
**    This enum type is used in parse_lines to keep track of the connective
**  bits between commands that are passed in in a single line.
*/

#define ARG_MAX (sysconf(_SC_ARG_MAX))

enum parseState {CONTINUING, WAITING, PIPING, REDIRECTING, SKIPPING, ENDING};

/*----------------Globals
**
*/

char** __PB__;
char*** __P__;
char** __NAB__;
char*** __NA__;
FILE** __S__;

/*-----------------free_and_leave
**
*/

void free_and_leave(int code) {
  free(*__PB__);
  free(*__P__);
  free(*__NAB__);
  free(*__NA__);
  exit(code);
}

/*-----------------error
**
*/

void error() {
  char error_message[30] = "An error has occurred\n";
  write(STDERR_FILENO, error_message, strlen(error_message));
}

/*-----------------run_command
**    A helper function used by parse_line to do the heavy-lifting work of
**  running commands.  It takes as inputs:
**        paths:
**        nargv:
**        redirect_file:
**        pipeIn:
**        pipeOut:
**
**
*/

int run_command (char* paths[], char* nargv[], char* redirect_file, int* pipeIn, int* pipeOut) {
  if (strcmp(nargv[0], "path") == 0) {
    char** arg_pos = nargv+1; //because nargv would just be a pointer
    char** path_pos = paths+1;
    char* next_path_start = paths[0]; //not a pointer into PATH_BUFFER -
    // the actual location of the string in path buffer
    // We want to retain the rule that PATH_BUFFER contains a series of null-
    //  terminated strings, one after another, each string representing one
    //  of the known paths of the shell; while paths contains a NULL-
    //  terminated series of pointers into PATH_BUFFER, marking the start of
    //  each path.  So, we start with arg_pos pointing to the first path in
    //  new_argsv (nargv), path_pos pointing to the start of paths, and
    //  next_path_start pointing to the start of PATH_BUFFER (which is
    //  conveniently always stored in paths[0]).  As long as there are more args
    //  to read, the loop will copy the path from NEW_ARGS_BUFFER (through
    //  the pointer to it in arg_pos) into the path_buffer
    while (*arg_pos) { // I see where the seffault is coming from.  paths[0] is null after path is called  with no args.  So, I need to dstore a pointer to the start of path_buffer somewhere, even in that case.
      *path_pos = next_path_start; //store in paths the start of this path
      // in path_buffer
      next_path_start = stpcpy(next_path_start, *arg_pos) + 1; // copy the path -?> segfault here . . . scary. . . but I'm going to do some yoga
      // from the argument buffer to the path buffer, and point to the start
      // of the next path in bath_buffer (+1 skips the \0 byte)
      arg_pos = arg_pos + 1; //move to next argument
      path_pos = path_pos + 1; //move to next position in paths
    }
    *path_pos = (char*) NULL; //mark that there are no more paths;

    // OK - this should work.
  } else if (strcmp(nargv[0], "cd") == 0) {
    // check correct number of arguments - nargv[1] should be a string,
    // nargv[2] should be NULL.
    if (!nargv[1] || nargv[2]) {
      return -1;
    }
    int was_err = chdir(nargv[1]);
    if (was_err != 0) {
      //another fit!
    } else {
      return 0; // need to correctly interpret this on the other side
    }
  } else if (strcmp(nargv[0], "exit") == 0) {
    if (nargv[1] == (char*) NULL) {
      free_and_leave(0); // just jump out of the whole program.
    } else {
      return -1; //any arguments indicates an error.
    }
  } else { // the command is not built-in
    //printf("Got here (4)?\n");
    char** path_pos = paths+1;
    char path[2*ARG_MAX]; // much too big - but it will never overflow.
    while (*path_pos) {
      stpcpy(stpcpy((stpcpy(path, *path_pos)), "/"), nargv[0]); // something like that
      int access_success = access(path, X_OK);
      if (access_success == 0) {
        break;
      }
      path_pos = path_pos + 1;
    }
    if (!*path_pos) {
      return -1; // An error!  None of the paths worked (signified by us
      // reaching NULL-termination of the path-pointers in paths).
    }
    int pid = fork();
    if (pid != 0) { // we're in the parent; either fork worked or didn't.
      return pid;
    } else if (pid == 0) {
      if (redirect_file) {
        FILE* outfile = freopen(redirect_file, "w", stdout);
        if (!outfile) {
          error();
          exit(1);
        }
        if (dup2(fileno(outfile), fileno(stderr)) == -1) {
          error();
          exit(1);
        }
      }
      if (pipeIn) {//use this pipe as input
        if (close(pipeIn[1]) == -1) {
          error();
          exit(1);
        }
        if (dup2(STDIN_FILENO, pipeIn[0]) == -1) {
          error();
          exit(1);
        }
      }
      if (pipeOut) { // use this pipe as output
        if (close(pipeOut[0]) == -1) {
          error();
          exit(1);
        }
        if (dup2(STDOUT_FILENO, pipeOut[1]) == -1) {
          error();
          exit(1);
        }
      }
      int err = execv(path, nargv);
      exit(err); // this will only happen if execv returns; in this case, we
                // need to get out of here!
      }
    }
    return 0; // this shouldn't ever happen, I think.
  }

/*-----------------parse_line (helper function for main)
**    parse_line's job within this program is to take lines of input and break
**  them down into a series of commands, and arguments to those commands.  If
**  the command in question is a built-in command, it calls the function in
**  wish.c associated with that built-in.  If the command is not built-in, then
**  parse_line will search the various paths stored in PATH for a target
**  command.  If it finds a target, it will fork and run the command in a child.
**
**    If parse_line can't find the target command, or if it encounters any other
**  error, it will write the one and only allowed error message to stderror.

*/



/*  line :-> cmd con |OR cmd
**  cmd :-> cmd argstring |OR cmdstring
**  con :> | line |OR & line |OR > filename
**        this is innacurate, but it should shed some light.
**    parse_line will run in a loop.  It will mark the con, if there is one,
**    and parse a command.  Then, depending on what the con was, it will run the
**    command in a certain way.  Then (or possibly after), if there was a con,
**    it will scan ahead and try to mark the next con, and then try the next
**    command.
**
**    An error prints the error message, and then quits back to the main loop.
*/
// againL: 1. scan for connective (strpbrk)
//      if you don't find one:
//          parse command
//          run command
//          return (0)
//      otherwise:
//         record connective and set to "\0"
//         parse the command before the connective
//         depending on the connective, either run the command, or parse the file after it
//         do what must be done
//         loop
//      if you run into an error anywhere, return (1).



int parse_line(char* line, char* new_argsv[], char* paths[]) {
  char* connectives = ";&|>"; //& means 'don't wait'
  char* arg_sep = " \t\n"; //the symbol(s) which separate(s) commands and arguments

  enum parseState curr_state = WAITING;
  enum parseState prev_state = WAITING;

  int pipe_in_fds[2];
  int pipe_out_fds[2];
  int* inPipe;

  char* con_ptr;

  while (1) {
    con_ptr = strpbrk(line, connectives);
    if (!con_ptr) {
      curr_state = ENDING; // If there's no connectives remaining in the line,
      // this must be the last command, so we'll break after trying to execute
      // it.
    } else {
      if (*con_ptr == ';') {
        curr_state = WAITING; //run the command, wait for it to finish, display
        // the output, and then move on to the next command.
      } else if (*con_ptr == '&') {
        curr_state = CONTINUING; //run the command, but don't wait for it
        // to finish (or display the output)
      } else if (*con_ptr == '|') {
        curr_state = PIPING; // set up a pipe from this command's output
        // to the next command's input (this is why we need prev_state -
        // so that when we run the next command, we can pipe this command's
        // output to that command's input).
      } else if (*con_ptr == '>') { //this case could be an else, probably
        curr_state = REDIRECTING; // This this current command is to be i
        // interpreted as normal - but the NEXT command is actually a file
        // name.  Parse it, then run the command, sending output to the file.
        // cmd args > file ; . . . or cmd args > file & . . . is fine.
        // cmd args > file | . . . or cmd args > file > . . . is an error (
        // because there's output to pipe or redirect to a file).
      }
      *con_ptr = '\0'; //mark the end of the command to be parsed.

    }

    //parse the command. . .
    char* save_ptr = (char*) NULL;

    char** command_arg_pointer = new_argsv;
    char* command_token_ptr = strtok_r(line, arg_sep, &save_ptr);
    while (command_token_ptr) {
      *command_arg_pointer = command_token_ptr;
      command_arg_pointer += 1;
      command_token_ptr = strtok_r(NULL, arg_sep, &save_ptr);
    }

    if (command_arg_pointer == new_argsv) { // the loop was never entered - no command found
      if (curr_state == ENDING) { // No connective was found either - an empty input
        return 0;
      } else if (curr_state == WAITING || curr_state == CONTINUING){ // A connective with no command.  If we don't need the output, we skip it.  If we do. . . that's an error.
        curr_state = SKIPPING;
      } else {
        return -1;
      }
    }

    if (curr_state != SKIPPING) {

      *command_arg_pointer = (char*) NULL;

      if (prev_state == PIPING) {
        inPipe = pipe_in_fds;
      } else {
        inPipe = (int*) NULL;
      }

      if (curr_state == ENDING || curr_state == WAITING) {
        int child_pid = run_command(paths, new_argsv, (char*) NULL, inPipe, (int*) NULL);
        if (child_pid == -1) {
          return -1;
        }
        int status;
        waitpid(child_pid, &status, 0);
      } else if (curr_state == CONTINUING) { // need to ignore the output. . . if there are no more commands.
        /*
        char* redir;
        if (strlen(con_ptr+1) != strspn(con_ptr+1, arg_sep)) { // see if there are non-empty characters left in the string
          redir = "/dev/null";
        } else {
          redir = (char*) NULL;
        }
        */
        int child_pid = run_command(paths, new_argsv, (char*) NULL, inPipe, (int*) NULL);
        if (child_pid == -1) {
          return -1;
        }
      } else if (curr_state == PIPING) {
        if (pipe(pipe_out_fds) == -1) { // there was an error!
          return -1;
        }
        int child_pid = run_command(paths, new_argsv, (char*) NULL, inPipe, pipe_out_fds);
        if (child_pid == -1) {
          return -1;
        }
      } else if (curr_state == REDIRECTING){ // this could be an else, but I want to make it clear that we're redirecting
        // keep parsing forward from con_ptr;
        //printf("Got here (1)?\n");
        char* next_con_ptr = strpbrk(con_ptr + 1, connectives);
        if (next_con_ptr) {//there was another connective.
          if (*next_con_ptr == '&') {
            curr_state = CONTINUING;
          } else { // all other symbols treated as ; - can't pipe or redirect again
          // anyway, since there's no output.
            curr_state = WAITING;
          }
          *next_con_ptr = '\0';//doesn't matter what it was - overwrite it to limit the filename read.
        } else { //the file just ended, so we CHANGE curr_state to ENDING to get out of the loop
          curr_state = ENDING;
        }
        //printf("Got here (2)?\n");
        char* new_saveptr = (char*) NULL;
        char* redir_file = strtok_r(con_ptr + 1, arg_sep, &new_saveptr);
        if (!redir_file) {
          return -1;
        }
        char* next_arg = strtok_r((char*) NULL, arg_sep, &new_saveptr);
        if (next_arg) {
          return -1;
        }

        //printf("Got here (3)?\n");

        con_ptr = next_con_ptr;
        int child_pid = run_command(paths, new_argsv, redir_file, inPipe, (int*) NULL);
        if (child_pid == -1) {
          return -1;
        } else {
          if (curr_state != CONTINUING) {
            int status;
            waitpid(child_pid, &status, 0);
          }
        }
      }

    }

    // close the previous pipes
    if (prev_state == PIPING) {
      close(pipe_in_fds[0]);
      close(pipe_in_fds[1]);
    }
    if (curr_state == PIPING) {
      pipe_in_fds[0] = pipe_out_fds[0];
      pipe_in_fds[1] = pipe_out_fds[1];
    }
    if (curr_state == ENDING) { // the pipes will all be closed at this point.
      return 0;
    }
    new_argsv = command_arg_pointer+1; //reset new_argsv to store the next set of arguments
    line = con_ptr+1;//reset line to parse the next command
    prev_state = curr_state;

  }
  return 0;
}


/*-----------------main
**    main's job within this program is to handle input, and to hold the loop
**  that runs the program.
**
**    The first thing Main needs to do is parse its command-line inputs.  This
**  should either be a single file, or nothing at all.  If it's a single file,
**  main should open it, check for errors, and then pass each line in turn to
**  parse_line.  Once it's done with parse_line, it should return.
**
**    If the command-line input is empty, main enters its interactive mode.
**  here, main loops repeatedly.  It repeatedly grabs lines of input from the
**  user.  If the line is EOF, it exits - otherwise, it passes them to
**  parse_line.  main doesn't need to do anything else at this point -
**  parse_line will handle the rest.
*/

int main(int argc, char* argv[]) {
  char* PATH_BUFFER = (char*) calloc(sizeof(char), ARG_MAX);
  __PB__ = &PATH_BUFFER;
  char** PATHS = (char**) calloc(sizeof(char*), ARG_MAX);
  __P__ = &PATHS;
  char* NEW_ARGS_BUFFER = (char*) calloc(sizeof(char), ARG_MAX );
  NEW_ARGS_BUFFER[0] = 'h';
  NEW_ARGS_BUFFER[1] = 'i';
  NEW_ARGS_BUFFER[2] = '\0';
  __NAB__ = &NEW_ARGS_BUFFER;
  char** NEW_ARGSV = (char**) calloc (sizeof(char*), ARG_MAX);
  __NA__ = &NEW_ARGSV;
  strcpy (PATH_BUFFER, "/bin");
  PATHS[0] = PATH_BUFFER;
  PATHS[1] = PATH_BUFFER;
  PATHS[2] = (char*) NULL;

  FILE* source;
  if (argc == 1) { // interactve
    source = stdin;
  } else if (argc == 2) { // read from a file
    source = fopen(argv[1], "r");
    if (!source) {
      error();
      free_and_leave(1);
    }
  } else {
    error();
    free_and_leave(1);
  }
  while (1) {
    if (argc == 1) {
      printf("wish> ");
    }
    size_t buffer_size = ARG_MAX;
    if (getline(&NEW_ARGS_BUFFER, &buffer_size, source) == -1) {
      break;
    } else {
      int result = parse_line(NEW_ARGS_BUFFER, NEW_ARGSV, PATHS);
      if (result == -1) {
        error();
        if (argc == 2) {
          //free_and_leave(0); // I guess I'm not supposed to exit if there's an error in batch mode?
          buffer_size = 1;
        }
      }
    }
  }
  free_and_leave(0);
  return 0;
}
