// Arranque del servidor MMP y recepción de mensajes TCP (flujo) y UDP (datagrama).
#include "log.h"
#include "mmp_handlers.h"
#include "msgstream.h"
#include "net.h"
#include "protocol.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_TCP_BACKLOG 16
#define SERVER_RECV_BUFFER 4096

// Argumentos que recibe cada hilo de cliente, el propio hilo libera esta estructura
typedef struct {
    NodeRegistry *registry;
    Logger *logger;
    int client_fd;
    char peer_text[NET_PEER_TEXT_MAX_LEN];
} ClientThreadArgs;

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

// Envía una respuesta de MMP por TCP, por lo que serializa, agrega el delimitador '\n' y la transmite
static void send_tcp_response(int client_fd, const MmpMessage *response,
                              const char *origin, Logger *logger)
{
    char *json_text = mmp_serialize(response);
    if (json_text == NULL) {
        logger_printf(logger, "%s error: no se pudo serializar la respuesta", origin);
        return;
    }

    // El delimitador TCP del protocolo es '\n' y sse añade al enviar
    size_t json_length = strlen(json_text);
    char *framed = malloc(json_length + 2);
    if (framed == NULL) {
        free(json_text);
        logger_printf(logger, "%s error: sin memoria al preparar la respuesta", origin);
        return;
    }
    memcpy(framed, json_text, json_length);
    framed[json_length] = '\n';
    framed[json_length + 1] = '\0';

    if (net_send_all(client_fd, framed, json_length + 1) != 0) {
        logger_printf(logger, "%s error: fallo al enviar la respuesta", origin);
    } else {
        logger_printf(logger, "%s respuesta enviada tipo %s id=%s",
                      origin, mmp_type_to_string(response->type), response->id);
    }

    free(framed);
    free(json_text);
}

// Para procesar una línea, parsea, despacha el flujo y responde por TCP cuando corresponde
// tcp_client_fd < 0 indica origen UDP (sin respuesta por conexión)
static void process_message(NodeRegistry *registry, const char *line,
                            const char *origin, Logger *logger,
                            int tcp_client_fd)
{
    MmpMessage request;
    MmpParseResult result = mmp_parse(line, &request);

    // Los errores del parser se registran y en TCP, se devuelve un ERROR controlado
    if (result != MMP_PARSE_OK) {
        const char *code = (result == MMP_PARSE_INVALID_FORMAT) ? "INVALID_FORMAT"
                         : (result == MMP_PARSE_UNKNOWN_MESSAGE) ? "UNKNOWN_MESSAGE"
                         : "INVALID_PARAMETER";
        logger_printf(logger, "%s mensaje rechazado: %s", origin, code);
        if (tcp_client_fd >= 0) {
            // Se arma un ERROR mínimo reutilizando el protocolo, porque el id puede ir vacío si no se parseó
            MmpMessage error_response;
            mmp_message_init(&error_response);
            error_response.type = MMP_TYPE_ERROR;
            error_response.payload = cJSON_CreateObject();
            if (error_response.payload != NULL) {
                cJSON_AddStringToObject(error_response.payload, "code", code);
            }
            send_tcp_response(tcp_client_fd, &error_response, origin, logger);
            mmp_message_free(&error_response);
        }
        return;
    }

    logger_printf(logger, "%s mensaje válido tipo %s id=%s",
                  origin, mmp_type_to_string(request.type), request.id);

    MmpMessage response;
    DispatchOutcome outcome = mmp_dispatch(registry, &request,
                                           tcp_client_fd >= 0, &response);

    if (outcome.has_response) {
        if (tcp_client_fd >= 0) {
            logger_printf(logger, "%s flujo %s -> respuesta %s",
                          origin, mmp_type_to_string(request.type),
                          mmp_type_to_string(response.type));
            send_tcp_response(tcp_client_fd, &response, origin, logger);
        }
        mmp_message_free(&response);
    } else {
        logger_printf(logger, "%s flujo %s procesado sin respuesta",
                      origin, mmp_type_to_string(request.type));
    }

    mmp_message_free(&request);
}

static void serve_tcp_client(NodeRegistry *registry, int client_fd,
                             const char *peer_text, Logger *logger)
{
    MsgStream stream;
    msgstream_init(&stream);

    char recv_buffer[SERVER_RECV_BUFFER];
    char origin[128];
    snprintf(origin, sizeof(origin), "TCP %s", peer_text);

    while (!stop_requested) {
        ssize_t received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);
        if (received == 0) {
            // Cierre ordenado del cliente: fin normal de la conexión
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
            process_message(registry, line, origin, logger, client_fd);
        }
    }

    msgstream_free(&stream);
    net_close(client_fd);
}

// Punto de entrada del hilo de cliente, este atiende la conexión y libera sus propios recursos
static void *client_thread_main(void *raw_args)
{
    ClientThreadArgs *args = (ClientThreadArgs *)raw_args;
    serve_tcp_client(args->registry, args->client_fd, args->peer_text, args->logger);
    free(args); // el hilo detached libera lo que recibió
    return NULL;
}

// Acepta un cliente TCP y lanza un hilo independiente que lo atiende sin bloquear al principal
static void accept_and_serve_client(NodeRegistry *registry, int tcp_listener,
                                    Logger *logger)
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

    ClientThreadArgs *args = malloc(sizeof(*args));
    if (args == NULL) {
        logger_printf(logger, "error: sin memoria para atender un nuevo cliente");
        net_close(client_fd);
        return;
    }
    args->registry = registry;
    args->logger = logger;
    args->client_fd = client_fd;
    if (net_format_peer((struct sockaddr *)&client_address,
                        client_address_len,
                        args->peer_text, sizeof(args->peer_text)) != 0) {
        snprintf(args->peer_text, sizeof(args->peer_text), "cliente-no-identificable");
    }
    logger_printf(logger, "conexión TCP aceptada desde %s", args->peer_text);

    pthread_t thread;
    if (pthread_create(&thread, NULL, client_thread_main, args) != 0) {
        logger_printf(logger, "error: no se pudo crear el hilo para %s", args->peer_text);
        net_close(client_fd);
        free(args);
        return;
    }
    pthread_detach(thread);
}

// Recibe UDP con cada datagrama como mensaje y lo entrega al parser
static void receive_udp_datagram(NodeRegistry *registry, int udp_socket,
                                 Logger *logger)
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

    // UDP no mantiene conexión de respuesta, por eso se pasa -1 como client_fd
    process_message(registry, datagram, origin, logger, -1);
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

    // Registro de nodos compartido por todos los flujos
    NodeRegistry registry;
    registry_init(&registry);

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
            receive_udp_datagram(&registry, udp_socket, &logger);
        }
        if (FD_ISSET(tcp_listener, &read_set)) {
            accept_and_serve_client(&registry, tcp_listener, &logger);
        }
    }

    // Al parar se cierran los sockets de escucha para no aceptar más conexiones,
    // los hilos de cliente se separan, terminan por su cuenta y liberan lo suyo
    logger_printf(&logger, "servidor MMP deteniéndose; cerrando recursos");
    net_close(tcp_listener);
    net_close(udp_socket);
    // No se destruyen registry ni logger aquí para no liberar un mutex que un hilo
    // separado podría estar usando. Es el OS el que libera al terminar el proceso
    return EXIT_SUCCESS;
}
