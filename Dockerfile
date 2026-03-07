FROM alpine:latest

RUN apk add --no-cache gcc musl-dev sqlite-dev openssl-dev openssl-libs-static sqlite-static make

WORKDIR /app
COPY . .

RUN make clean && gcc -Wall -O2 -Iinclude \
    src/main.c src/server.c src/ws.c src/http.c \
    src/db.c src/auth.c src/frontend.c src/voice.c \
    -o cgiscord \
    -static \
    -lsqlite3 -lssl -lcrypto -lpthread -ldl \
    && echo "build ok" && ls -lh cgiscord

CMD ["./cgiscord"]
