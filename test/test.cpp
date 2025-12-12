#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <cstring>
#include <memory>

#include "shell.h"    // NEW: Shell class
#include "process.h"  // NEW: Process class

using namespace std;

void write_line(const string &filename, const char *msg) {
  std::ofstream outfile(filename);
  outfile << msg;
  outfile.close();
}

struct RedirectionContext {
  FILE *original_stdin;
  FILE *redirected_file;
  int original_stdin_fd;
  int file_descriptor;
};

struct StdoutCapture {
  int original_fd;
  int pipe_read_fd;
  int pipe_write_fd;
  bool active;
};

static StdoutCapture start_stdout_capture() {
  StdoutCapture ctx = {-1, -1, -1, false};
  fflush(stdout);
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    perror("Failed to create capture pipe");
    return ctx;
  }
  ctx.pipe_read_fd = pipe_fds[0];
  ctx.pipe_write_fd = pipe_fds[1];

  ctx.original_fd = dup(STDOUT_FILENO);
  if (ctx.original_fd < 0) {
    perror("Failed to duplicate STDOUT");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    ctx.pipe_read_fd = ctx.pipe_write_fd = -1;
    return ctx;
  }

  if (dup2(ctx.pipe_write_fd, STDOUT_FILENO) < 0) {
    perror("Failed to redirect STDOUT");
    close(ctx.original_fd);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    ctx.original_fd = ctx.pipe_read_fd = ctx.pipe_write_fd = -1;
    return ctx;
  }

  close(ctx.pipe_write_fd);
  ctx.pipe_write_fd = -1;
  ctx.active = true;
  return ctx;
}

static std::string finish_stdout_capture(StdoutCapture &ctx) {
  std::string output;
  if (!ctx.active) {
    return output;
  }

  fflush(stdout);

  if (ctx.original_fd >= 0) {
    dup2(ctx.original_fd, STDOUT_FILENO);
    close(ctx.original_fd);
    ctx.original_fd = -1;
  }

  char buffer[256];
  ssize_t n;
  while ((n = read(ctx.pipe_read_fd, buffer, sizeof(buffer))) > 0) {
    output.append(buffer, buffer + n);
  }
  if (ctx.pipe_read_fd >= 0) {
    close(ctx.pipe_read_fd);
    ctx.pipe_read_fd = -1;
  }
  ctx.active = false;
  return output;
}

RedirectionContext setup_stdin_redirection(const char *filename) {
  RedirectionContext context = {nullptr, nullptr, -1, -1};

  context.file_descriptor = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (context.file_descriptor < 0) {
    perror("Failed to open file");
    return context;
  }

  context.redirected_file = fopen(filename, "r");
  if (!context.redirected_file) {
    perror("Failed to open file as FILE*");
    close(context.file_descriptor);
    return context;
  }

  context.original_stdin = stdin;   // Store the original stdin
  context.original_stdin_fd = dup(STDIN_FILENO);
  if (context.original_stdin_fd < 0) {
    perror("Failed to duplicate STDIN");
    fclose(context.redirected_file);
    close(context.file_descriptor);
    context.redirected_file = nullptr;
    context.file_descriptor = -1;
    return context;
  }
  stdin = context.redirected_file;  // Redirect stdin
  dup2(context.file_descriptor, STDIN_FILENO);

  return context;
}

void restore_stdin_redirection(RedirectionContext &context,
                               const char *filename) {
  if (context.original_stdin_fd >= 0) {
    dup2(context.original_stdin_fd, STDIN_FILENO);
    close(context.original_stdin_fd);
    context.original_stdin_fd = -1;
  }

  if (context.original_stdin) {
    stdin = context.original_stdin;  // Restore the original stdin
  }

  if (context.redirected_file) {
    fclose(context.redirected_file);
  }

  if (context.file_descriptor >= 0) {
    close(context.file_descriptor);
  }

  if (context.file_descriptor >= 0) {
    close(context.file_descriptor);
  }

  if (filename) {
    remove(filename);  // Cleanup the file
  }
}

static std::vector<Process*> to_vec(const std::list<Process*>& lst) {
  return std::vector<Process*>(lst.begin(), lst.end());
}

static void free_list(std::list<Process*>& lst) {
  for (auto* p : lst) delete p;
  lst.clear();
}

static void expect_tokens(const Process& p, const std::vector<const char*>& want) {
  ASSERT_EQ(p.get_size(), want.size()) << "tokens.size() mismatch";
  for (size_t i = 0; i < want.size(); ++i) {
    EXPECT_STREQ(p.get_token(i), want[i]) << "token mismatch at index " << i;
  }
}

static void expect_proc(const Process& p,
                        const std::vector<const char*>& tokens,
                        bool pipe_in, bool pipe_out) {
  expect_tokens(p, tokens);
  EXPECT_EQ(p.pipe_in,  pipe_in)  << "pipe_in mismatch";
  EXPECT_EQ(p.pipe_out, pipe_out) << "pipe_out mismatch";
}

TEST(ParseInputTest, SingleCommand) {
  Shell shell;                    // NEW
  char cmd[] = "ls";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  // move parsed processes out of the shell's internal vector into our test list
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 1u);
  expect_proc(*v[0], {"ls"}, /*pipe_in=*/false, /*pipe_out=*/false);

  free_list(plist);
}

TEST(ParseInputTest, MixedSemicolonAndPipe) {
  Shell shell;                              // NEW
  char cmd[] = "echo hi|grep h;pwd";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 3u);
  expect_proc(*v[0], {"echo", "hi"}, /*in=*/false, /*out=*/true);
  expect_proc(*v[1], {"grep", "h"},  /*in=*/true,  /*out=*/false);
  expect_proc(*v[2], {"pwd"},        /*in=*/false, /*out=*/false);

  free_list(plist);
}

TEST(ParseInputTest, EmptyInputProducesNoProcess) {
  Shell shell;                    // NEW
  char cmd[] = "";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  EXPECT_EQ(v.size(), 0u);

  free_list(plist);
}

// test quit
TEST(ShellTest, Quit) {
  Shell shell;            // NEW
  Process p(0, 0);
  p.add_token((char *)"quit");

  EXPECT_TRUE(shell.isQuit(&p)) << "passing quit should return true" << endl;
}

// test not quit
TEST(ShellTest, NotQuit) {
  Shell shell;            // NEW
  Process p(0, 0);
  p.add_token((char *)"exit");

  EXPECT_FALSE(shell.isQuit(&p)) << "passing exit should return false" << endl;
}

TEST(ShellTest, TestSimpleRun) {
  const char input_string[] = "echo hello\nquit\n";

  const char *filename = "hello.txt";
  write_line(filename, input_string);

  StdoutCapture stdout_capture = start_stdout_capture();
  RedirectionContext context = setup_stdin_redirection(filename);

  Shell shell;   // NEW
  shell.run();   // CHANGED from run()

  std::string output = finish_stdout_capture(stdout_capture);
  restore_stdin_redirection(context, filename);

  EXPECT_NE(output.find("hello\n"), std::string::npos)
      << "shell output should contain command output 'hello'";

}

TEST(ParseInputTest, Exactly25TokensAccepted) {
  Shell shell;  // NEW

  // 25 tokens total: echo + 24 args
  char cmd[] =
    "echo a01 a02 a03 a04 a05 a06 a07 a08 a09 a10 "
    "a11 a12 a13 a14 a15 a16 a17 a18 a19 a20 a21 a22 a23 a24";

  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 1u) << "should produce exactly one Process";

  ASSERT_EQ(v[0]->get_size(), 25) << "should accept exactly 25 tokens";

  expect_proc(
    *v[0],
    {"echo","a01","a02","a03","a04","a05","a06","a07","a08","a09",
     "a10","a11","a12","a13","a14","a15","a16","a17","a18","a19",
     "a20","a21","a22","a23","a24"},
    /*pipe_in=*/false, /*pipe_out=*/false
  );

  free_list(plist);
}

TEST(ParseInputTest, ConsecutiveSemicolonsSkipEmptyCommands) {
  Shell shell;
  char cmd[] = "ls;;pwd;";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 2u);
  expect_proc(*v[0], {"ls"}, false, false);
  expect_proc(*v[1], {"pwd"}, false, false);

  free_list(plist);
}

TEST(ParseInputTest, TrailingPipeDoesNotLeaveDanglingProcess) {
  Shell shell;
  char cmd[] = "echo hi|";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 1u);
  expect_proc(*v[0], {"echo", "hi"}, false, false);

  free_list(plist);
}

TEST(ParseInputTest, WhitespaceSeparatedPipelineTokens) {
  Shell shell;
  char cmd[] = "   cat   alpha.txt\t|\t grep  beta  ";
  std::list<Process*> plist;
  shell.parse_input(cmd);
  for (auto* p : shell.process_list) {
    plist.push_back(p);
  }
  shell.process_list.clear();

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 2u);
  expect_proc(*v[0], {"cat", "alpha.txt"}, false, true);
  expect_proc(*v[1], {"grep", "beta"}, true, false);

  free_list(plist);
}

TEST(ProcessTest, TokensBeyondLimitIgnored) {
  Process proc(false, false);
  std::vector<std::unique_ptr<char[]>> storage;
  for (int i = 0; i < 30; ++i) {
    std::string tok = "t" + std::to_string(i);
    auto buf = std::make_unique<char[]>(tok.size() + 1);
    std::strcpy(buf.get(), tok.c_str());
    proc.add_token(buf.get());
    storage.push_back(std::move(buf));
  }

  EXPECT_EQ(proc.get_size(), 25);

  std::vector<std::string> expected;
  for (int i = 0; i < 25; ++i) {
    expected.push_back("t" + std::to_string(i));
  }
  std::vector<const char*> expected_raw;
  for (const auto& s : expected) {
    expected_raw.push_back(s.c_str());
  }
  expect_tokens(proc, expected_raw);
}

TEST(ShellTest, BuiltinDetectionMatchesKnownCommands) {
  Shell shell;
  const char* cmds[] = {"cput", "cget", "crm", "cls", "ccon", "cdisc"};
  for (const char* cmd : cmds) {
    Process p(false, false);
    std::vector<char> buf(std::strlen(cmd) + 1);
    std::strcpy(buf.data(), cmd);
    p.add_token(buf.data());
    EXPECT_TRUE(shell.isBuiltin(&p)) << cmd << " should be detected as builtin";
  }

  Process not_builtin(false, false);
  char ls_cmd[] = "ls";
  not_builtin.add_token(ls_cmd);
  EXPECT_FALSE(shell.isBuiltin(&not_builtin));
}

TEST(ShellTest, CdDetectionOnlyMatchesCd) {
  Shell shell;
  Process cd_proc(false, false);
  char cd_cmd[] = "cd";
  cd_proc.add_token(cd_cmd);
  EXPECT_TRUE(shell.isCd(&cd_proc));

  Process other(false, false);
  char other_cmd[] = "cdr";
  other.add_token(other_cmd);
  EXPECT_FALSE(shell.isCd(&other));
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
