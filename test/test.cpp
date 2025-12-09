#include <fcntl.h>
#include <gtest/gtest.h>
#include <tsh.h>
#include <unistd.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <cstring>

using namespace std;


void write_line(const string &filename, const char *msg) {
  std::ofstream outfile(filename);
  outfile << msg;
  outfile.close();
}

struct RedirectionContext {
  FILE *original_stdin;
  FILE *redirected_file;
  int file_descriptor;
};

RedirectionContext setup_stdin_redirection(const char *filename) {
  RedirectionContext context = {nullptr, nullptr, -1};

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
  stdin = context.redirected_file;  // Redirect stdin
  dup2(context.file_descriptor, STDIN_FILENO);

  return context;
}

void restore_stdin_redirection(RedirectionContext &context,
                               const char *filename) {
  if (context.original_stdin) {
    stdin = context.original_stdin;  // Restore the original stdin
  }

  if (context.redirected_file) {
    fclose(context.redirected_file);
  }

  if (context.file_descriptor >= 0) {
    close(context.file_descriptor);
  }

  if (filename) {
    remove(filename);  // Cleanup the file
  }
}
static std::vector<Process*> to_vec(const std::list<Process*>& lst) {
  return std::vector<Process*>(lst.begin(), lst.end()); // ✅ range ctor
}

static void free_list(std::list<Process*>& lst) {
  for (auto* p : lst) delete p;
  lst.clear();
}

static void expect_tokens(const Process& p, const std::vector<const char*>& want) {
  ASSERT_EQ(p.get_size(), want.size()) << "tokens.size() mismatch"; // ✅ public API
  for (size_t i = 0; i < want.size(); ++i) {
    EXPECT_STREQ(p.get_token(i), want[i]) << "token mismatch at index " << i; // ✅ content compare
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
  char cmd[] = "ls";
  std::list<Process*> plist;
  parse_input(cmd, plist);

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 1u);
  expect_proc(*v[0], {"ls"}, /*pipe_in=*/false, /*pipe_out=*/false);

  free_list(plist);
}

TEST(ParseInputTest, MixedSemicolonAndPipe) {
  char cmd[] = "echo hi|grep h;pwd";
  std::list<Process*> plist;
  parse_input(cmd, plist);

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 3u);
  expect_proc(*v[0], {"echo", "hi"}, /*in=*/false, /*out=*/true);
  expect_proc(*v[1], {"grep", "h"},  /*in=*/true,  /*out=*/false);
  expect_proc(*v[2], {"pwd"},        /*in=*/false, /*out=*/false);

  free_list(plist);
}

TEST(ParseInputTest, EmptyInputProducesNoProcess) {
  char cmd[] = "";
  std::list<Process*> plist;
  parse_input(cmd, plist);

  auto v = to_vec(plist);
  EXPECT_EQ(v.size(), 0u);

  free_list(plist);
}


// test quit
TEST(ShellTest, Quit) {
  Process p(0, 0);
  p.add_token((char *)"quit");

  EXPECT_TRUE(isQuit(&p)) << "passing quit should return true" << endl;
}

// // test exit
TEST(ShellTest, NotQuit) {
  Process p(0, 0);
  p.add_token((char *)"exit");

  EXPECT_FALSE(isQuit(&p)) << "passing quit should return true" << endl;
}

TEST(ShellTest, TestSimpleRun) {
  std::string expected_output = "$ hello\n$ ";
  char input_string[1092] = "echo hello";

  const char *filename = "hello.txt";
  write_line(filename, input_string);

  testing::internal::CaptureStdout();
  RedirectionContext context = setup_stdin_redirection(filename);

  // run your test
  run();

  std::string output = testing::internal::GetCapturedStdout();
  restore_stdin_redirection(context, filename);

  EXPECT_TRUE(output == expected_output) << "Your Output\n"
                                         << output << "\nexpected outputs:\n"
                                         << expected_output;
}

TEST(ParseInputTest, Exactly25TokensAccepted) {
  // 25 tokens total: echo + 24 args
  char cmd[] =
    "echo a01 a02 a03 a04 a05 a06 a07 a08 a09 a10 "
    "a11 a12 a13 a14 a15 a16 a17 a18 a19 a20 a21 a22 a23 a24";

  std::list<Process*> plist;
  parse_input(cmd, plist);

  auto v = to_vec(plist);
  ASSERT_EQ(v.size(), 1u) << "should produce exactly one Process";

  // Verify token count == 25
  ASSERT_EQ(v[0]->get_size(), 25) << "should accept exactly 25 tokens";

  // Verify exact tokens and no pipes
  expect_proc(
    *v[0],
    {"echo","a01","a02","a03","a04","a05","a06","a07","a08","a09",
     "a10","a11","a12","a13","a14","a15","a16","a17","a18","a19",
     "a20","a21","a22","a23","a24"},
    /*pipe_in=*/false, /*pipe_out=*/false
  );

  free_list(plist);
}





int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
