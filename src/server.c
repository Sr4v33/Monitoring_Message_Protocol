// Arranque del servidor MMP y recepción de mensajes TCP (flujo) y UDP (datagrama).
#include "log.h"
#include "msgstream.h"
#include "net.h"
#include "protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_TCP_BACKLOG 16
#define SERVER_RECV_BUFFER 4096

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

// Interpretación de una línea ya delimitada con el parser del protocolo
static void parse_and_log(const char *line, const char *origin, Logger *logger)
{
    MmpMessage message;
    MmpParseResult result = mmp_parse(line, &message);

    switch (result) {
    case MMP_PARSE_OK:
        logger_printf(logger, "%s mensaje válido tipo %s id=%s",
                      origin, mmp_type_to_string(message.type), message.id);
        mmp_message_free(&message);
        break;
    case MMP_PARSE_INVALID_FORMAT:
        logger_printf(logger, "%s mensaje rechazado: formato inválido (no es JSON)", origin);
        break;
    case MMP_PARSE_INVALID_PARAMETER:
        logger_printf(logger, "%s mensaje rechazado: parámetro inválido", origin);
        break;
    case MMP_PARSE_UNKNOWN_MESSAGE:
        logger_printf(logger, "%s mensaje rechazado: tipo desconocido", origin);
        break;
    }
}

static void serve_tcp_client(int client_fd, const char *peer_text, Logger *logger)
{
    MsgStream stream;
    msgstream_init(&stream);

    char recv_buffer[SERVER_RECV_BUFFER];
    char origin[128];
    snprintf(origin, sizeof(origin), "TCP %s", peer_text);

    while (!stop_requested) {
        ssize_t received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);
        if (received == 0) {
            // Cierre ordenado del cliente: fin normal de la conexión.
            logger_printf(logger, "conexión TCP cerrada por %s", peer_text);
            break;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue; // interrumpido por señal: reintentar o salir según stop_requested
            }
            logger_printf(logger, "error: recv falló en %s", peer_text);
            break;
        }

        // Se usan los bytes reales recibidos
        MsgStreamAppendResult append_result =
            msgstream_append(&stream, recv_buffer, (size_t)received);
        if (append_result == MSGSTREAM_APPEND_OVERFLOW) {
            logger_printf(logger, "TCP %s mensaje rechazado: excede el tamaño máximo", peer_text);
        } else if (append_result == MSGSTREAM_APPEND_ERROR) {
            logger_printf(logger, "error: sin memoria procesando datos de %s", peer_text);
            break;
        }

        // Extrae todos los mensajes completos presentes en el buffer
        const char *line = NULL;
        size_t line_length = 0;
        while (msgstream_next(&stream, &line, &line_length)) {
            parse_and_log(line, origin, logger);
        }
    }

    msgstream_free(&stream);
    net_close(client_fd);
}

// Acepta un cliente TCP, registra su IP:puerto y lo atiende hasta que cierre
static void accept_and_serve_client(int tcp_listener, Logger *logger)
{
    struct sockaddr_storage client_address;
    socklen_t client_address_len = sizeof(client_address);

    int client_fd = accept(tcp_listener,
                           (struct sockaddr *)&client_address,
                           &client_address_len);
    if (client_fd < 0) {
        if (!stop_requested && errno != EINTR) {
            logger_printf(logger, "aviso: accept falló");
        }
        return;
    }

    char peer_text[NET_PEER_TEXT_MAX_LEN];
    if (net_format_peer((struct sockaddr *)&client_address,
                        client_address_len,
                        peer_text, sizeof(peer_text)) != 0) {
        snprintf(peer_text, sizeof(peer_text), "cliente-no-identificable");
    }
    logger_printf(logger, "conexión TCP aceptada desde %s", peer_text);

    serve_tcp_client(client_fd, peer_text, logger);
}

// Recibe UDP con cada datagrama como mensaje y lo entrega al parser
static void receive_udp_datagram(int udp_socket, Logger *logger)
{
    char datagram[SERVER_RECV_BUFFER];
    struct sockaddr_storage source_address;
    socklen_t source_len = sizeof(source_address);

    ssize_t received = recvfrom(udp_socket, datagram, sizeof(datagram) - 1, 0,
                                (struct sockaddr *)&source_address, &source_len);
    if (received < 0) {
        if (errno != EINTR) {
            logger_printf(logger, "error: recvfrom falló");
        }
        return;
    }

    datagram[received] = '\0';

    char peer_text[NET_PEER_TEXT_MAX_LEN];
    char origin[128];
    if (net_format_peer((struct sockaddr *)&source_address, source_len,
                        peer_text, sizeof(peer_text)) == 0) {
        snprintf(origin, sizeof(origin), "UDP %s", peer_text);
    } else {
        snprintf(origin, sizeof(origin), "UDP origen-no-identificable");
    }

    parse_and_log(datagram, origin, logger);
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

    int max_fd = (tcp_listener > udp_socket) ? tcp_listener : udp_socket;
    while (!stop_requested) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(tcp_listener, &read_set);
        FD_SET(udp_socket, &read_set);

        int ready = select(max_fd + 1, &read_set, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger_printf(&logger, "error: select falló");
            break;
        }

        if (FD_ISSET(udp_socket, &read_set)) {
            receive_udp_datagram(udp_socket, &logger);
        }
        if (FD_ISSET(tcp_listener, &read_set)) {
            accept_and_serve_client(tcp_listener, &logger);
        }
    }

    logger_printf(&logger, "servidor MMP deteniéndose; cerrando recursos");
    net_close(tcp_listener);
    net_close(udp_socket);
    logger_close(&logger);
    return EXIT_SUCCESS;
}
