#include "shell.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

Shell::Shell() {}

Shell::~Shell() {
  for (Process *p : process_list) {
    delete p;
  }
  process_list.clear();
}

void Shell::display_prompt() const {
  char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        std::cout << "$ " << std::flush;
        return;
    }

    std::string path(cwd);

    const char* color_start = "\033[1;32m";  //green
    const char* color_end   = "\033[0m";

    std::cout << color_start << "[" << path << "]" << color_end << " $ " << std::flush;
}


void Shell::close_pipe(int fd) const {
  if (fd != -1) close(fd);
}

void Shell::cleanup(char *input_line) {
  for (Process *p : process_list) {
    delete p;
  }
  process_list.clear();
  free(input_line);
}

char *Shell::read_input() {
  char *input = nullptr;
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

void Shell::sanitize(char *cmd) {
  size_t len = strlen(cmd);
  while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' ||
                     cmd[len - 1] == ' '  || cmd[len - 1] == '\t')) {
    cmd[--len] = '\0';
  }
}

bool Shell::isQuit(Process *p) const {
  return p && p->cmdTokens && p->cmdTokens[0]
         && std::strcmp(p->cmdTokens[0], "quit") == 0;
}

bool Shell::isCd(Process *process) const
{
    return process && process->cmdTokens && process->cmdTokens[0]
           && std::strcmp(process->cmdTokens[0], "cd") == 0;
}

void Shell::parse_input(char *cmd) {
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
    if (last && last->pipe_out) {
      last->pipe_out = false;
    }
  }
}

void Shell::handle_cd(Process *proc)
{
    char *path = (proc->tok_index > 1) ? proc->cmdTokens[1] : nullptr;

  if (!path || std::strcmp(path, "~") == 0) {
    const char *home = std::getenv("HOME");
    if (!home) {
      std::cerr << "cd: HOME not set\n";
      return;
    }
    path = const_cast<char*>(home);
  }

  if (chdir(path) != 0) {
    std::perror("cd");
  }
}

bool Shell::run_commands() {
  bool is_quit = false;

  if (process_list.empty()) {
    return false;
  }

  std::vector<pid_t> pids;
  pids.reserve(process_list.size());

  int prev_read_end = -1;
  for (Process* proc : process_list) {
    if (!proc || !proc->cmdTokens || !proc->cmdTokens[0]) {
      continue;
    }
    if (isQuit(proc)) {
      is_quit = true;
      break;
    }
    if (isCd(proc)) {
      handle_cd(proc);
      continue;
    }

    if (proc->pipe_out) {
      if (pipe(proc->pipe_fd) == -1) {
        std::perror("Pipe error");
        close_pipe(prev_read_end);
        return false;
      }
    }

    pid_t pid = fork();
    if (pid < 0) {
      std::perror("Fork error");
      close_pipe(prev_read_end);
      close_pipe(proc->pipe_fd[0]);
      close_pipe(proc->pipe_fd[1]);
      for (pid_t cpid : pids) { int st; waitpid(cpid, &st, 0); }
      return false;
    }

    if (pid == 0) {
      // child
      if (proc->pipe_out) {
        if (dup2(proc->pipe_fd[1], STDOUT_FILENO) == -1) {
          std::perror("dup2 error");
          _exit(EXIT_FAILURE);
        }
      }
      if (proc->pipe_in) {
        if (prev_read_end == -1) {
          std::perror("prev pipe not initialized");
          _exit(EXIT_FAILURE);
        }
        if (dup2(prev_read_end, STDIN_FILENO) == -1) {
          std::perror("dup2 error");
          _exit(EXIT_FAILURE);
        }
      }

      if (proc->pipe_in) close(prev_read_end);
      if (proc->pipe_out) {
        close(proc->pipe_fd[0]);
        close(proc->pipe_fd[1]);
      }

      execvp(proc->cmdTokens[0], proc->cmdTokens);
      perror("execvp failed");
      _exit(EXIT_FAILURE);
    }

    // parent
    pids.push_back(pid);
    if (proc->pipe_out) {
      close_pipe(proc->pipe_fd[1]);
      close_pipe(prev_read_end);
      prev_read_end = proc->pipe_fd[0];
    } else {
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

void Shell::run() {
  bool quit = false;
  while (!quit) {
    display_prompt();
    char *input_line = read_input();
    if (input_line == nullptr) {
      cleanup(input_line);
      break;
    }
    sanitize(input_line);
    if (input_line[0] == '\0') {
      free(input_line);
      continue;
    }
    parse_input(input_line);
    quit = run_commands();
    cleanup(input_line);
  }
}
