#include <tsh.h>

using namespace std;

/**
 * @brief
 * Helper function to print the PS1 pormpt.
 *
 * Linux has multiple promt levels
 * (https://wiki.archlinux.org/title/Bash/Prompt_customization): PS0 is
 * displayed after each command, before any output. PS1 is the primary prompt
 * which is displayed before each command. PS2 is the secondary prompt displayed
 * when a command needs more input (e.g. a multi-line command). PS3 is not very
 * commonly used
 */
void display_prompt() { cout << "$ " << flush; }

/**
 * @brief Cleans up allocated resources to prevent memory leaks.
 *
 * This function deletes all elements in the provided list of Process objects,
 * clears the list, and frees the memory allocated for the input line.
 *
 * @param process_list A pointer to a list of Process pointers to be cleaned up.
 * @param input_line A pointer to the dynamically allocated memory for user
 * input. This memory is freed to avoid memory leaks.
 */
void cleanup(list<Process *> &process_list, char *input_line) {
  for (Process *p : process_list) {
    delete p;
  }
  process_list.clear();
  free(input_line);
  input_line = nullptr;
}

/**
 * @brief Main loop for the shell, facilitating user interaction and command
 * execution.
 *
 * This function initializes a list to store Process objects, reads user input
 * in a loop, and executes the entered commands. The loop continues until the
 * user decides to quit.
 *
 * @note The function integrates several essential components:
 *   1. Displaying the shell prompt to the user.
 *   2. Reading user input using the read_input function.
 *   3. Parsing the input into a list of Process objects using parse_input.
 *   4. Executing the commands using run_commands.
 *   5. Cleaning up allocated resources to prevent memory leaks with the cleanup
 * function.
 *   6. Breaking out of the loop if the user enters the quit command.
 *   7. Continuously prompting the user for new commands until an exit condition
 * is met.
 *
 * @warning
 *  Initialize Necessary Variables:
 *      Declare variables for storing the list of Process objects, the input
 * line, and a flag for quitting. Read User Input in a Loop: Utilize a while
 * loop to continuously read user input until a termination condition is met.
 * Check for a valid input line and skip empty lines. Clean Up Resources: After
 * executing the commands, clean up allocated resources using the cleanup
 *      function to avoid memory leaks.
 *  Check for Quit Command:
 *      Determine if the user entered the quit command. If so, set the exit flag
 * to true and break out of the loop. Repeat the Process: If the user did not
 * quit, continue prompting for input by displaying the prompt again.
 *  Considerations:
 *      Handle edge cases such as empty input lines or unexpected errors
 * gracefully. Ensure proper error handling and informative messages for the
 * user.
 */
void run() {
  list<Process *> process_list;
  char *input_line;
  bool is_quit = false;
  
  while (!is_quit){
    display_prompt();
    input_line = read_input();
    if (input_line == nullptr) {            
      cleanup(process_list, input_line);
      break;                                
    }
    sanitize(input_line);
    if (input_line[0] == '\0') { free(input_line); input_line = nullptr; continue; }
    parse_input(input_line, process_list);
    is_quit = run_commands(process_list);
    cleanup(process_list, input_line);
  }
}

/**
 * @brief Reads input from the standard input (stdin) in chunks and dynamically
 *        allocates memory to store the entire input.
 *
 * This function reads input from the standard input or file in chunks of size
 * MAX_LINE. It dynamically allocates memory to store the entire input, resizing
 * the memory as needed. The input is stored as a null-terminated string. The
 * function continues reading until a newline character is encountered or an
 * error occurs.
 *
 * @return A pointer to the dynamically allocated memory containing the input
 * string. The caller is responsible for freeing this memory when it is no
 * longer needed. If an error occurs or EOF is reached during input, the
 * function returns NULL.
 *
 * @warning Ensure that the memory allocated by this function is freed using
 * free() to avoid memory leaks.
 */
char *read_input() {
  char *input = NULL;
  char tempbuf[MAX_LINE];
  size_t inputlen = 0, templen = 0;

  do {
    char *a = fgets(tempbuf, MAX_LINE, stdin);
    if (a == nullptr) {
      return input;
    }
    templen = strlen(tempbuf);
    input = (char *)realloc(input, inputlen + templen + 1);
    strcpy(input + inputlen, tempbuf);
    inputlen += templen;
  } while (templen == MAX_LINE - 1 && tempbuf[MAX_LINE - 2] != '\n');

  return input;
}

/**
 * @brief
 * removes the new line char of the end in cmd.
 */
void sanitize(char *cmd) {
  size_t len = strlen(cmd);
  while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' ||
                     cmd[len - 1] == ' '  || cmd[len - 1] == '\t')) {
    cmd[--len] = '\0';
  }
}


/**
 * Parses the given command string and populates a list of Process objects.
 *
 * This function takes a command string and a reference to a list of Process
 * pointers. It tokenizes the command based on the delimiters "|; " and creates
 * a new Process object for each token. The created Process objects are added to
 * the provided process_list. Additionally, it sets pipe flags for each Process
 * based on the presence of pipe delimiters '|' in the original command string.
 *
 * @param cmd The command string to be parsed.
 * @param process_list A reference to a list of Process pointers where the
 * created Process objects will be stored.
 *
 * Hints for students:
 * - The 'delimiters' we need to handle are '|', ';', and '[space]'
 * - 'pipe_in_val' is a flag indicating whether the current Process should take
 *      input from a previous Process (1 if true, 0 if false).
 * - 'currProcess' is a pointer to the current Process being created and added
 *      to the list.
 * - When a delim is a space it calls currProcess->add_token()
 * - When the delimiter is a ";" or "|", a new Process object is created with
 *       relevant information, and the pipe flags are set based on the presence
 *       of '|' in the original command.
 * - The created Process objects are added to the process_list.
 */
// ls| grep da

void parse_input(char *cmd, std::list<Process *> &process_list) {
  Process *currProcess = new Process(false, false);
  char *tok_start = nullptr;

  auto flush_cmd = [&](size_t i) {
    if (tok_start) {
      cmd[i] = '\0';                 
      currProcess->add_token(tok_start);
      tok_start = nullptr;
    }
  };

  auto flush_process = [&](bool allocate_next) {
    if (currProcess->tok_index > 0) {
      currProcess->cmdTokens[currProcess->tok_index] = nullptr;
      process_list.push_back(currProcess);
    } else {
      delete currProcess;
    }
    currProcess = allocate_next ? new Process(false, false) : nullptr;
  };

  const size_t n = std::strlen(cmd);
  for (size_t i = 0; i < n; ++i) {
    char c = cmd[i];
    switch (c) {
      case ' ':
      case '\t':
      case '\n':  
      case '\r': 
        flush_cmd(i);
        break;

      case '|': {
        bool had_tokens = (currProcess->tok_index > 0) || (tok_start != nullptr);
        flush_cmd(i);                 
        if (had_tokens) {
          currProcess->pipe_out = true;  
          flush_process(true);          
          currProcess->pipe_in = true;  
        }
        break;
      }

      case ';': {
        bool had_tokens = (currProcess->tok_index > 0) || (tok_start != nullptr);
        flush_cmd(i);                 
        if (had_tokens) {
          flush_process(true);       
        } else {
          delete currProcess;
          currProcess = new Process(false, false);
        }
        break;
      }

      default:
        if (!tok_start) tok_start = &cmd[i];
        break;
    }
  }
  if (tok_start) {
    currProcess->add_token(tok_start);
    tok_start = nullptr;
  }
  flush_process(false);
  if (!process_list.empty()) {
    Process* last = process_list.back();
    if (last->pipe_in && last->tok_index == 0) {
        delete last;
        process_list.pop_back();
    }
    if (last->pipe_out) {
        last->pipe_out = false;  
    }
  }
}


/**
 * Check if the given command represents a quit request.
 *
 * This function compares the first token of the provided command with the
 * string "quit" to determine if the command is a quit request.
 *
 * Parameters:
 *   - p: A pointer to a Process structure representing the command.
 *
 * Returns:
 *   - true if the command is a quit request (the first token is "quit").
 *   - false otherwise.
 */
bool isQuit(Process *p) {
  return p && p->cmdTokens && p->cmdTokens[0]
         && std::strcmp(p->cmdTokens[0], "quit") == 0;
}

void close_pipe(int pipe){
  if (pipe != -1) close(pipe);
}

/**
 * @brief Execute a list of commands using processes and pipes.
 *
 * This function takes a list of processes and executes them sequentially,
 * connecting their input and output through pipes if needed. It handles forking
 * processes, creating pipes, and waiting for child processes to finish.
 *
 * @param command_list A list of Process pointers representing the commands to
 * execute. Each Process object contains information about the command, such as
 *                     command tokens, pipe settings, and file descriptors.
 *
 * @return A boolean indicating whether a quit command was encountered during
 * execution. If true, the execution was terminated prematurely due to a quit
 * command; otherwise, false.
 *
 * @details
 * The function iterates through the provided list of processes and performs the
 * following steps:
 * 1. Check if a quit command is encountered. If yes, terminate execution.
 * 2. Create pipes and fork a child process for each command.
 * 3. In the parent process, close unused pipes, wait for child processes to
 * finish if necessary, and continue to the next command.
 * 4. In the child process, set up pipes for input and output, execute the
 * command using execvp, and handle errors if the command is invalid.
 * 5. Cleanup final process and wait for all child processes to finish.
 *
 * @note
 * - The function uses Process objects, which contain information about the
 * command and pipe settings.
 * - It handles sequential execution of commands, considering pipe connections
 * between them.
 * - The function exits with an error message if execvp fails to execute the
 * command.
 * - Make sure to properly manage file descriptors, close unused pipes, and wait
 * for child processes.
 * - The function returns true if a quit command is encountered during
 * execution; otherwise, false.
 *
 * @warning
 * - Ensure that the Process class is properly implemented and contains
 * necessary information about the command, such as command tokens and pipe
 * settings.
 * - The function relies on proper implementation of the isQuit function for
 * detecting quit commands.
 * - Students should understand the basics of forking, pipes, and process
 * execution in Unix-like systems.
 */
bool run_commands(list<Process *> &command_list) {
  bool is_quit = false;

  if (command_list.empty()){
    return false;
  }

  std::vector<pid_t> pids;
  pids.reserve(command_list.size());

  int prev_read_end = -1;
  for (Process* proc : command_list){
    if(!proc || !proc->cmdTokens || !proc->cmdTokens[0]){
      continue;
    }
    if (isQuit(proc)){
      is_quit = true;
      break;
    }
    if(proc->pipe_out){
      if (pipe(proc->pipe_fd) == -1){
        std::perror("Pipe error");
        close_pipe(prev_read_end);
        return false;
      }
    }

    pid_t pid = fork();
    if (pid < 0){
      std::perror("Fork error"); 
      close_pipe(prev_read_end);
      close_pipe(proc->pipe_fd[0]);
      close_pipe(proc->pipe_fd[1]);
      for (pid_t cpid : pids) { int st; waitpid(cpid, &st, 0); }

      return false;
    }

    if (pid == 0){
      // Child process
      if (proc->pipe_out){ // need to write to pipe
        if (dup2(proc->pipe_fd[1], STDOUT_FILENO) == -1){
            std::perror("dup2 error"); 
            close_pipe(prev_read_end);
            close_pipe(proc->pipe_fd[0]);
            close_pipe(proc->pipe_fd[1]);
            _exit(EXIT_FAILURE);
        }
      }

      if (proc->pipe_in){ // need to read from prev pipe
        
        if (prev_read_end == -1){
          std::perror("prev pipe not initilized");
          _exit(EXIT_FAILURE);
        }
        if (dup2(prev_read_end, STDIN_FILENO) == -1){
          std::perror("dup2 error"); 
            close_pipe(prev_read_end);
            close_pipe(proc->pipe_fd[0]);
            close_pipe(proc->pipe_fd[1]);
            _exit(EXIT_FAILURE);
        }
      }

      if (proc->pipe_in){
        close(prev_read_end);    
      }
      if (proc->pipe_out) {
        close(proc->pipe_fd[0]);   
        close(proc->pipe_fd[1]);   
      }

      execvp(proc->cmdTokens[0], proc->cmdTokens);
      perror("execv faield");
      _exit(EXIT_FAILURE);
    }

    // PARENT block

    pids.push_back(pid);
    if (proc->pipe_out){
      close_pipe(proc->pipe_fd[1]);
      close_pipe(prev_read_end); 
      prev_read_end = proc->pipe_fd[0];
    }
    else{
      close_pipe(prev_read_end);
      prev_read_end = -1;
      for (pid_t cpid : pids) { int st; waitpid(cpid, &st, 0); }
      pids.clear();
    }
  }
    
  for (pid_t cpid : pids) { 
    int st; 
    waitpid(cpid, &st, 0);
  }
  return is_quit;
}

/**
 * @brief Constructor for Process class.
 *
 * Initializes a Process object with the provided plags
 *
 * @param _pipe_in 1: The process takes input form prev, 0: if not
 * @param _pipe_out 1: The output of current proches is piped to next, 0: if not
 */
Process::Process(bool _pipe_in_flag, bool _pipe_out_flag) {
  pipe_in = _pipe_in_flag;
  pipe_out = _pipe_out_flag;
  tok_index = 0;
  pipe_fd[0] = -1;                   
  pipe_fd[1] = -1;  
}

/**
 * @brief Destructor for Process class.
 */
Process::~Process() {
}

/**
 * @brief add a pointer to a command or flags to cmdTokens
 *
 * @param tok
 */
void Process::add_token(char *tok) {
  if (!tok) return;
  if (tok_index < 25) {                  
    cmdTokens[tok_index++] = tok;
  }
}

int Process::get_size() const{
  return tok_index;
}

char* Process::get_token(int i) const{
  return cmdTokens[i];
}


