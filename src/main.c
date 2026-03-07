#include "../include/server.h"
#include "../include/ws.h"
#include "../include/db.h"
#include "../include/auth.h"
#include "../include/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static volatile int running = 1;

void handle_signal(int sig) {
    (void)sig;
    printf("\n[server] shutting down...\n");
    running = 0;
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    printf("[server] initializing database...\n");
    if (db_init("discord.db") != 0) {
        fprintf(stderr, "[server] failed to init database\n");
        return 1;
    }

    printf("[server] starting on port %d...\n", SERVER_PORT);
    server_t server;
    if (server_init(&server) != 0) {
        fprintf(stderr, "[server] failed to init server\n");
        return 1;
    }

    printf("[server] ready! open http://localhost:%d in your browser\n", SERVER_PORT);

    while (running) {
        server_poll(&server);
        server_tick(&server);
    }

    server_destroy(&server);
    db_close();
    return 0;
}
