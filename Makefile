_DEPS = tsh.h
_OBJ = shell.o process.o
_MOBJ = main.o
_TOBJ = test.o

APPBIN = dk_shell_app
TESTBIN = dk_shell_test

DEBUG = -DDEBUGMODE


IDIR = include
CC = g++
# Require C++17 to support modern std features used by Homebrew googletest
CFLAGS = -std=gnu++17 -I$(IDIR) -Wall $(DEBUG) -Wextra -g -pthread 

# Try to detect Homebrew-installed googletest and add its include/lib paths
GTEST_PREFIX ?= $(shell brew --prefix googletest 2>/dev/null)
GTEST_INCLUDE = $(GTEST_PREFIX)/include
GTEST_LIB = $(GTEST_PREFIX)/lib

# If googletest is found via Homebrew, add its include path to CFLAGS
ifeq ($(wildcard $(GTEST_INCLUDE)),)
# no-op
else
CFLAGS += -I$(GTEST_INCLUDE)
LDFLAGS += -L$(GTEST_LIB)
endif
ODIR = obj
SDIR = src
LDIR = lib
TDIR = test
LIBS = -lm
XXLIBS = $(LIBS) -lstdc++ -lgtest -lgtest_main -lpthread $(LDFLAGS)
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))
MOBJ = $(patsubst %,$(ODIR)/%,$(_MOBJ))
TOBJ = $(patsubst %,$(ODIR)/%,$(_TOBJ)) 

$(ODIR)/%.o: $(SDIR)/%.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR)/%.o: $(TDIR)/%.cpp $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(APPBIN) $(TESTBIN) submission

$(APPBIN): $(OBJ) $(MOBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(TESTBIN): $(TOBJ) $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(XXLIBS)

submission:
	find . -name "*~" -exec rm -rf {} \;
	zip -r submission src lib include


.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~
	rm -f $(APPBIN) $(TESTBIN)
	rm -f submission.zip
