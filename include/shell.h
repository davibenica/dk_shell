#ifndef SIMPLE_SHELL_H
#define SIMPLE_SHELL_H

#include <list>
#include <vector>
#include <iostream>
#include <sys/types.h>

#include "process.h"

#define MAX_LINE 81
#define PATH_MAX 1024

class Shell {
 public:
  Shell();
  ~Shell();

  void run(); 
  bool isQuit(Process *process) const;
  bool isCd(Process *process) const;
  std::list<Process*> process_list;

  void display_prompt() const;
  void cleanup(char *input_line);
  char *read_input();
  void sanitize(char *cmd);
  void parse_input(char *input_line);
  void handle_cd(Process *proc);
  bool run_commands();

  void close_pipe(int fd) const;
};

#endif
