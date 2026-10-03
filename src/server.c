// Arranque y transporte del servidor
#include "log.h"
#include "net.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_TCP_BACKLOG 16

static volatile sig_atomic_t stop_requested = 0;

static void handle_stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

// Señal de cierre
static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_stop_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

// Aceptación de la conexión TCP y registro del cliente
static void accept_and_log_client(int tcp_listener, Logger *logger)
{
    struct sockaddr_storage client_address;
    socklen_t client_address_len = sizeof(client_address);

    int client_fd = accept(tcp_listener,
                           (struct sockaddr *)&client_address,
                           &client_address_len);
    if (client_fd < 0) {
        if (!stop_requested) {
            logger_printf(logger, "aviso: accept falló");
        }
        return;
    }

    char peer_text[NET_PEER_TEXT_MAX_LEN];
    if (net_format_peer((struct sockaddr *)&client_address,
                        client_address_len,
                        peer_text, sizeof(peer_text)) == 0) {
        logger_printf(logger, "conexión TCP aceptada desde %s", peer_text);
    } else {
        logger_printf(logger, "conexión TCP aceptada (cliente no identificable)");
    }

    net_close(client_fd);
}

int main(int argc, char **argv)
{
    // Se definen puerto y archivo de logs como argumentos necesarios, el host es opcional
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
                "uso: %s <puerto> <archivo_log> [host]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *port = argv[1];
    const char *log_path = argv[2];
    const char *host = (argc == 4) ? argv[3] : NULL;

    Logger logger;
    if (logger_open(&logger, log_path) != 0) {
        // Error al abrir el archivo de log
        fprintf(stderr,
                "aviso: no se pudo abrir el archivo de log '%s'; "
                "se registrará solo en stderr\n",
                log_path);
    }

    install_signal_handlers();

    // Socket TCP de escucha
    int tcp_listener = net_create_tcp_listener(host, port, SERVER_TCP_BACKLOG);
    if (tcp_listener < 0) {
        logger_printf(&logger, "error: no se pudo crear el socket TCP en el puerto %s", port);
        logger_close(&logger);
        return EXIT_FAILURE;
    }

    // Socket UDP para STATUS
    int udp_socket = net_create_udp_socket(host, port);
    if (udp_socket < 0) {
        logger_printf(&logger, "error: no se pudo crear el socket UDP en el puerto %s", port);
        net_close(tcp_listener);
        logger_close(&logger);
        return EXIT_FAILURE;
    }

    logger_printf(&logger,
                  "servidor MMP iniciado: TCP y UDP escuchando en el puerto %s", port);

    // Bucle de aceptación
    while (!stop_requested) {
        accept_and_log_client(tcp_listener, &logger);
    }

    logger_printf(&logger, "servidor MMP deteniéndose; cerrando recursos");
    net_close(tcp_listener);
    net_close(udp_socket);
    logger_close(&logger);
    return EXIT_SUCCESS;
}
