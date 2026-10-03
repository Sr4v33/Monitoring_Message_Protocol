// Capa de transporte del servidor MMP
#ifndef MMP_NET_H
#define MMP_NET_H

#include <stddef.h>
#include <sys/socket.h>

// Tamaño para el texto "IP:puerto" del cliente
#define NET_PEER_TEXT_MAX_LEN 64

// Crea el socket TCP de escucha (getaddrinfo+bind+listen)
// Con el host NULL se enlaza a todas las interfaces. Retorna -1 si falla
int net_create_tcp_listener(const char *host, const char *port, int backlog);

// Crea el socket UDP enlazado (misma resolución que TCP) y retorna -1 si falla
int net_create_udp_socket(const char *host, const char *port);

// Formatea la dirección del cliente como "IP:puerto" para el log
// Si es exitoso 0, y -1 si no cupo o falló
int net_format_peer(const struct sockaddr *address, socklen_t address_len,
                    char *out_text, size_t out_text_size);

// Cierra el socket si es mayor o igual a 0 (válido)
void net_close(int socket_fd);

#endif
