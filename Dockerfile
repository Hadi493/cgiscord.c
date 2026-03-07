FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    gcc \
    libsqlite3-dev \
    libssl-dev \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN make clean && gcc -Wall -O2 -Iinclude \
    src/main.c src/server.c src/ws.c src/http.c \
    src/db.c src/auth.c src/frontend.c src/voice.c \
    -o cgiscord \
    -lsqlite3 -lssl -lcrypto -lpthread \
    && echo "build ok" && ls -lh cgiscord

CMD ["./cgiscord"]
