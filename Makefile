_DEPS   = process.h protocol.h shell.h tsh.h
_SOBJ   = shell.o process.o
_MOBJ   = main.o
_TOBJ   = test.o
_SRVOBJ = cloud_server.o

APPBIN    = dk_shell_app
TESTBIN   = dk_shell_test
SERVERBIN = cloud_server

DEBUG = -DDEBUGMODE

IDIR = include
CC = g++
CFLAGS = -I$(IDIR) -Wall $(DEBUG) -Wextra -g -pthread
ODIR = obj
SDIR = src
LDIR = lib
TDIR = test
LIBS = -lm
XXLIBS = $(LIBS) -lstdc++ -lgtest -lgtest_main -lpthread

DEPS   = $(patsubst %,$(IDIR)/%,$(_DEPS))
OBJ    = $(patsubst %,$(ODIR)/%,$(_SOBJ))
MOBJ   = $(patsubst %,$(ODIR)/%,$(_MOBJ))
TOBJ   = $(patsubst %,$(ODIR)/%,$(_TOBJ))
SRVOBJ = $(patsubst %,$(ODIR)/%,$(_SRVOBJ))

.DEFAULT_GOAL := all

.PHONY: all clean submission

all: $(APPBIN) $(TESTBIN) $(SERVERBIN) submission

$(ODIR):
	mkdir -p $(ODIR)

$(ODIR)/%.o: $(SDIR)/%.cpp $(DEPS) | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR)/test.o: $(TDIR)/test.cpp $(DEPS) | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(APPBIN): $(OBJ) $(MOBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

$(TESTBIN): $(TOBJ) $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(XXLIBS)

$(SERVERBIN): $(SRVOBJ) $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

submission:
	find . -name "*~" -exec rm -rf {} \;
	zip -r submission src lib include test Makefile

clean:
	rm -f $(ODIR)/*.o *~ core $(IDIR)/*~
	rm -f $(APPBIN) $(TESTBIN) $(SERVERBIN)
	rm -f submission.zip
