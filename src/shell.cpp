#include "shell.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <fstream>
#include <vector>
#include <string>
#include "protocol.h"

Shell::Shell() : server_fd(-1) {}

Shell::~Shell() {
  for (Process *p : process_list) {
    delete p;
  }
  process_list.clear();

  if (server_fd != -1) {
    close(server_fd);
  }
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

bool Shell::isBuiltin(Process *process) const
{
  if (!process || !process->cmdTokens || !process->cmdTokens[0]) {
    return false;
  }
  std::string cmd(process->cmdTokens[0]);
  return (cmd == "cput" || cmd == "cget" || cmd == "crm" || cmd == "cls" || cmd == "ccon" || cmd == "cdisc");
}


void Shell::handleBuiltin(Process *process) {
  
  switch (process->cmdTokens[0][1]) {
    case 'p':  // cput
      handleCput(process);
      // Implement file upload logic here
      break;
    case 'g':  // cget
      handleCget(process);
      break;
    case 'r':  // crm
      handleCrm(process);
      break;
    case 'l':  // cls
      handleCls(process);
      break;
    case 'c':
      handleCcon(process);
      break;
    case 'd':  // cdisc
      if (server_fd != -1) {
          close(server_fd);
          server_fd = -1;
          std::cout << "Disconnected from server.\n";
      } else {
          std::cerr << "Not connected to any server.\n";
      }
      break;
    default:
      std::cerr << "Unknown builtin command\n";
      break;
  }
}

void Shell::handleCput(Process *process) {
    if (process->tok_index < 3) {
        std::cerr << "Usage: cput <local_file> <remote_file>\n";
        return;
    }
    if (server_fd == -1) {
        std::cerr << "Error: not connected to server.\n";
        return;
    }

    std::string localfile  = process->cmdTokens[1];
    std::string remotefile = process->cmdTokens[2];

    std::ifstream file(localfile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open file " << localfile << "\n";
        return;
    }
    
    std::streampos pos = file.tellg();
    if (pos < 0) {
        std::cerr << "Error: failed to get file size for " << localfile << "\n";
        file.close();
        return;
    }
    size_t filesize = static_cast<size_t>(pos);
    file.seekg(0, std::ios::beg);
    

    std::vector<char> buffer(filesize);
    if (filesize > 0) {
        if (!file.read(buffer.data(), filesize)) {
            std::cerr << "Error: cannot read file " << localfile << "\n";
            file.close();
            return;
        }
    }
    file.close();

    std::string header = std::string(CMD_UPLOAD) + "|" +
                         remotefile + "|" +
                         std::to_string(filesize);

    if (!send_line(server_fd, header)) {
        std::cerr << "Error: failed to send UPLOAD header\n";
        return;
    }

    if (filesize > 0 && !send_all(server_fd, buffer.data(), filesize)) {
        std::cerr << "Error: failed to send file data\n";
        return;
    }

    std::string response = read_line(server_fd);
    if (response.empty()) {
        std::cerr << "Error: no response from server\n";
        return;
    }

    std::cout << "Server response: " << response << "\n";
}

void Shell::handleCcon(Process *process)
{
  if (process->tok_index < 3) {
    std::cerr << "Usage: ccon <server_ip> <server_port>\n";
    return;
  }
  if (server_fd != -1) {
    std::cerr << "Already connected to a server. Disconnect first.\n";
    return;
  }

  std::string server_ip = process->cmdTokens[1];
  int server_port = std::atoi(process->cmdTokens[2]);
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::perror("Socket creation failed");
    server_fd = -1;
    return;
  }
  struct sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid server IP address\n";
    close(server_fd);
    server_fd = -1;
    return;
  }
  if (connect(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    std::perror("Connection failed");
    close(server_fd);
    server_fd = -1;
    return;
  }
  std::cout << "Connected to server " << server_ip << " on port " << server_port << "\n";
}

void Shell::handleCrm(Process *process)
{
  if (process->tok_index < 2) {
    std::cerr << "Usage: crm <remote_file>\n";
    return;
  }

  if (server_fd == -1) {
    std::cerr << "Error: not connected to server.\n";
    return;
  }

  std::string remotefile = process->cmdTokens[1];
  std::string request = std::string(CMD_DELETE) + "|" + remotefile;
  if (!send_line(server_fd, request)) {
    std::cerr << "Error: failed to send DELETE request\n";
    return;
  }
  std::string response = read_line(server_fd);
  if (response.empty()) {
    std::cerr << "Error: no response from server\n";
    return;
  }
  std::cout << "Server response: " << response << "\n";
}

void Shell::handleCget(Process *process)
{
  if (process->tok_index < 3) {
    std::cerr << "Usage: cget <remote_file> <local_file>\n";
      return;
  }
  if (server_fd == -1) {
    std::cerr << "Error: not connected to server.\n";
    return;
  }
  std::string remotefile  = process->cmdTokens[1];
  std::string localfile = process->cmdTokens[2];
  std::string request = std::string(CMD_DOWNLOAD) + "|" + remotefile;
  if (!send_line(server_fd, request)) {
    std::cerr << "Error: failed to send DOWNLOAD request\n";
    return;
  }
  std::string response = read_line(server_fd);
  if (response.empty()) {
    std::cerr << "Error: no response from server\n";
    return;
  }
  std::vector<std::string> parts = split_string(response, '|');
  if (parts.size() < 2 || parts[0] != RESP_OK) {
    std::cerr << "Error: server error: " << response << "\n";
    return;
  }
  size_t filesize = std::stoull(parts[2]);
  std::vector<char> filedata(filesize);
  size_t total_received = 0;
  while (total_received < filesize) {
    ssize_t n = recv(server_fd, filedata.data() + total_received, filesize - total_received, 0);
    if (n <= 0) {
      std::cerr << "Error: failed to receive file data\n";
      return;
    }
    total_received += n;
  }

  std::ofstream file(localfile, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Error: cannot open file " << localfile << " for writing\n";
    return; 
  }
  file.write(filedata.data(), filesize);
  file.close();
  std::cout << "File " << localfile << " downloaded successfully\n";

}

void Shell::handleCls(Process *process)
{
  if (server_fd == -1) {
      std::cerr << "Error: cannot ls file direcotory if not coonected to a server\n";
      return;
  }
  std::string request = std::string(CMD_LIST);
  if (!send_line(server_fd, request)) {
      std::cerr << "Error: failed to send LIST request\n";
      return;
  }
  std::string response = read_line(server_fd);
  std::cout << "Response: " << response << "\n";
  if (response.empty()) {
      std::cerr << "Error: no response from server\n";
      return;
  }
  std::vector<std::string> parts = split_string(response, '|');
  if (parts.size() < 2 || parts[0] != RESP_OK) {
      std::cerr << "Error: server error: " << response << "\n";
      return;
  }
  std::cout << "Files on server:\n";
  std::string file_entry = read_line(server_fd);
  while (!file_entry.empty()) {
    std::cout << " - " << file_entry << "\n";
    file_entry = read_line(server_fd); 
  }
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
    if (isBuiltin(proc)) {
      std::cout << "Handling builtin command: " << proc->cmdTokens[0] << std::endl;
      handleBuiltin(proc);
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
