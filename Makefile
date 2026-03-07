CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g -Iinclude
LDFLAGS = -lsqlite3 -lssl -lcrypto

SRCS    = src/main.c src/server.c src/ws.c src/http.c \
          src/db.c src/auth.c src/frontend.c src/voice.c
TARGET  = cgiscord

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✓ cord built successfully"
	@echo "  → run:  ./cgiscord"
	@echo "  → open: http://localhost:7000"
	@echo ""

clean:
	rm -f $(TARGET) discord.db
