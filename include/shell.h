#ifndef SIMPLE_SHELL_H
#define SIMPLE_SHELL_H

#include <list>
#include <vector>
#include <iostream>
#include <sys/types.h>

#include "process.h"

#define MAX_LINE 81

class Shell {
 public:
  Shell();
  ~Shell();

  void run(); 
  // Public helpers used by unit tests
  // parse_input populates the internal `process_list`
  bool isQuit(Process *process) const;
  // Make internal helpers public so unit tests can call them
  std::list<Process*> process_list;

  void display_prompt() const;
  void cleanup(char *input_line);
  char *read_input();
  void sanitize(char *cmd);
  void parse_input(char *input_line);
  bool run_commands();

  void close_pipe(int fd) const;
};

#endif
