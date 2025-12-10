#include "process.h"
#include <cstring>

Process::Process(bool _pipe_in_flag, bool _pipe_out_flag) {
  pipe_in = _pipe_in_flag;
  pipe_out = _pipe_out_flag;
  tok_index = 0;
  pipe_fd[0] = -1;
  pipe_fd[1] = -1;
}

Process::~Process() {
  if (pipe_fd[0] != -1) close(pipe_fd[0]);
  if (pipe_fd[1] != -1) close(pipe_fd[1]);
}

void Process::add_token(char *tok) {
  if (!tok) return;
  if (tok_index < 25) {
    cmdTokens[tok_index++] = tok;
  }
}

int Process::get_size() const {
  return tok_index;
}

char* Process::get_token(int i) const {
  return cmdTokens[i];
}
