// Implementación del acumulador de bytes TCP
#include "msgstream.h"

#include <stdlib.h>
#include <string.h>

void msgstream_init(MsgStream *stream)
{
    if (stream == NULL) {
        return;
    }
    stream->buffer = NULL;
    stream->length = 0;
    stream->capacity = 0;
    stream->line_buffer = NULL;
    stream->line_capacity = 0;
    stream->discarding = 0;
}

void msgstream_free(MsgStream *stream)
{
    if (stream == NULL) {
        return;
    }
    free(stream->buffer);
    free(stream->line_buffer);
    msgstream_init(stream);
}

// Asegura la capacidad de bytes en el buffer acumulador.
static int ensure_capacity(MsgStream *stream, size_t needed)
{
    if (needed <= stream->capacity) {
        return 0;
    }
    // Crece al doble o a lo necesario
    size_t new_capacity = (stream->capacity == 0) ? 256 : stream->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    char *grown = realloc(stream->buffer, new_capacity);
    if (grown == NULL) {
        return -1;
    }
    stream->buffer = grown;
    stream->capacity = new_capacity;
    return 0;
}

// Descarta los bytes hasta el primer '\n'
static void drop_until_newline(MsgStream *stream)
{
    char *newline = memchr(stream->buffer, '\n', stream->length);
    if (newline == NULL) {
        // Si no hay delimitador aún, se descarta todo y se sigue en modo descarte
        stream->length = 0;
        return;
    }
    size_t consumed = (size_t)(newline - stream->buffer) + 1;
    size_t remaining = stream->length - consumed;
    memmove(stream->buffer, stream->buffer + consumed, remaining);
    stream->length = remaining;
    stream->discarding = 0;
}

MsgStreamAppendResult msgstream_append(MsgStream *stream,
                                       const char *data, size_t length)
{
    if (stream == NULL || data == NULL) {
        return MSGSTREAM_APPEND_ERROR;
    }
    if (length == 0) {
        return MSGSTREAM_APPEND_OK;
    }

    if (ensure_capacity(stream, stream->length + length) != 0) {
        return MSGSTREAM_APPEND_ERROR;
    }
    memcpy(stream->buffer + stream->length, data, length);
    stream->length += length;

    // Si venimos descartando un mensaje gigante, intenta resincronizar con el próximo '\n'
    if (stream->discarding) {
        drop_until_newline(stream);
    }

    // Si el buffer supera el límite y no hay delimitador, se entra en modo descarte
    if (stream->length > MSGSTREAM_MAX_MESSAGE_LEN &&
        memchr(stream->buffer, '\n', stream->length) == NULL) {
        stream->length = 0;
        stream->discarding = 1;
        return MSGSTREAM_APPEND_OVERFLOW;
    }

    return MSGSTREAM_APPEND_OK;
}

static int copy_to_line_buffer(MsgStream *stream, const char *source,
                               size_t length)
{
    if (length + 1 > stream->line_capacity) {
        char *grown = realloc(stream->line_buffer, length + 1);
        if (grown == NULL) {
            return -1;
        }
        stream->line_buffer = grown;
        stream->line_capacity = length + 1;
    }
    memcpy(stream->line_buffer, source, length);
    stream->line_buffer[length] = '\0';
    return 0;
}

int msgstream_next(MsgStream *stream, const char **out_line, size_t *out_length)
{
    if (stream == NULL || stream->buffer == NULL) {
        return 0;
    }

    char *newline = memchr(stream->buffer, '\n', stream->length);
    if (newline == NULL) {
        return 0; // si aún no hay un mensaje completo
    }

    size_t line_length = (size_t)(newline - stream->buffer);
    if (copy_to_line_buffer(stream, stream->buffer, line_length) != 0) {
        return 0; // en caso de que no haya memoria, se trata como si no hubiera mensaje todavía
    }

    // Toma la línea y el '\n', conservando los bytes restantes para el próximo mensaje
    size_t consumed = line_length + 1;
    size_t remaining = stream->length - consumed;
    memmove(stream->buffer, stream->buffer + consumed, remaining);
    stream->length = remaining;

    if (out_line != NULL) {
        *out_line = stream->line_buffer;
    }
    if (out_length != NULL) {
        *out_length = line_length;
    }
    return 1;
}
