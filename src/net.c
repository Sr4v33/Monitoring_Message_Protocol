// Implementación del transporte
#include "net.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Recorre las direcciones y enlaza la primera que funcione. Si es TCP listen, sino -1 para ninguna
static int bind_first_working_address(struct addrinfo *candidates,
                                      int is_tcp, int backlog)
{
    for (struct addrinfo *candidate = candidates;
         candidate != NULL;
         candidate = candidate->ai_next) {

        int socket_fd = socket(candidate->ai_family,
                               candidate->ai_socktype,
                               candidate->ai_protocol);
        if (socket_fd < 0) {
            continue; // probar la siguiente dirección posible
        }

        int reuse = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) < 0) {
            close(socket_fd);
            continue;
        }

        if (bind(socket_fd, candidate->ai_addr, candidate->ai_addrlen) < 0) {
            close(socket_fd);
            continue;
        }

        if (is_tcp && listen(socket_fd, backlog) < 0) {
            close(socket_fd);
            continue;
        }

        return socket_fd;
    }

    return -1;
}

// Resuelve host/puerto y delega el bind
static int create_bound_socket(const char *host, const char *port,
                               int socktype, int is_tcp, int backlog)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  // IPv4 o IPv6
    hints.ai_socktype = socktype; // SOCK_STREAM o SOCK_DGRAM
    if (host == NULL) {
        hints.ai_flags = AI_PASSIVE; // sin host, enlaza a todas las interfaces
    }

    struct addrinfo *candidates = NULL;
    int status = getaddrinfo(host, port, &hints, &candidates);
    if (status != 0) {
        // Si falla la resolución se notifica pero no se finaliza el processo
        fprintf(stderr, "net: fallo al resolver %s:%s -> %s\n",
                host != NULL ? host : "(todas las interfaces)",
                port, gai_strerror(status));
        return -1;
    }

    int socket_fd = bind_first_working_address(candidates, is_tcp, backlog);
    freeaddrinfo(candidates);

    if (socket_fd < 0) {
        fprintf(stderr, "net: no fue posible enlazar en %s:%s (%s)\n",
                host != NULL ? host : "(todas las interfaces)",
                port, strerror(errno));
    }
    return socket_fd;
}

int net_create_tcp_listener(const char *host, const char *port, int backlog)
{
    return create_bound_socket(host, port, SOCK_STREAM, 1, backlog);
}

int net_create_udp_socket(const char *host, const char *port)
{
    // UDP no usa backlog entoncs se pasa a 0 y se ignora porque is_tcp es 0
    return create_bound_socket(host, port, SOCK_DGRAM, 0, 0);
}

int net_format_peer(const struct sockaddr *address, socklen_t address_len,
                    char *out_text, size_t out_text_size)
{
    if (address == NULL || out_text == NULL || out_text_size == 0) {
        return -1;
    }

    char host_text[NI_MAXHOST];
    char port_text[NI_MAXSERV];

    // getnameinfo devuelve la IP y el puerto en texto
    int status = getnameinfo(address, address_len,
                             host_text, sizeof(host_text),
                             port_text, sizeof(port_text),
                             NI_NUMERICHOST | NI_NUMERICSERV);
    if (status != 0) {
        return -1;
    }

    int written = snprintf(out_text, out_text_size, "%s:%s",
                           host_text, port_text);
    if (written < 0 || (size_t)written >= out_text_size) {
        return -1;
    }
    return 0;
}

int net_send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;
    // send puede escribir menos de lo pedido
    while (sent < length) {
        ssize_t n = send(socket_fd, data + sent, length - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue; // si es interrumpido por señal, se reintenta
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

void net_close(int socket_fd)
{
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}
