CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?=

SRC = main.c auth.c repos.c commits.c loading.c
BIN = vcs

.PHONY: all clean sanitize

all: $(BIN)

$(BIN): $(SRC) vcs.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

sanitize: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean $(BIN)

clean:
	rm -f $(BIN) vcs.exe
