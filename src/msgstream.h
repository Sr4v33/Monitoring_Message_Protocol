// Acumulador de bytes TCP
#ifndef MMP_MSGSTREAM_H
#define MMP_MSGSTREAM_H

#include <stddef.h>

#define MSGSTREAM_MAX_MESSAGE_LEN 65536

// Resultado de añadir bytes al acumulador.
typedef enum {
    MSGSTREAM_APPEND_OK = 0, // bytes acumulados correctamente
    MSGSTREAM_APPEND_OVERFLOW, // se superó el límite sin delimitador y se descarta hasta el próximo '\n'
    MSGSTREAM_APPEND_ERROR // fallo de memoria
} MsgStreamAppendResult;

// Estado del acumulador
typedef struct {
    char *buffer; // bytes recibidos aún no entregados
    size_t length; // bytes válidos en el buffer
    size_t capacity; // capacidad asignada del buffer
    char *line_buffer; // copia NUL-terminada del último mensaje extraído
    size_t line_capacity; // capacidad del line_buffer
    int discarding; // 1 mientras se descartan bytes de un mensaje que excedió el límite
} MsgStream;

// Inicializa el acumulador vacío
void msgstream_init(MsgStream *stream);

// Libera la memoria del acumulador y lo deja inicializado
void msgstream_free(MsgStream *stream);

// Añade bytes reales (los devueltos por recv)
MsgStreamAppendResult msgstream_append(MsgStream *stream,
                                       const char *data, size_t length);

// Extrae el siguiente mensaje completo (sin el '\n') como cadena NUL-terminada
// Devuelve 1 y fija out_line/out_length si había uno o 0 si aún no hay mensaje completo
int msgstream_next(MsgStream *stream, const char **out_line, size_t *out_length);

#endif
