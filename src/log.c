// Implementación del logging básico del servidor MMP
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

// Escribe la hora local "AAAA-MM-DD HH:MM:SS" y deja cadena vacía si no se puede obtener
static void format_timestamp(char *buffer, size_t buffer_size)
{
    time_t now = time(NULL);
    struct tm local_time;

    // Se usa localtime
    struct tm *result = localtime(&now);
    if (result == NULL) {
        if (buffer_size > 0) {
            buffer[0] = '\0';
        }
        return;
    }
    local_time = *result;

    if (strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_time) == 0) {
        if (buffer_size > 0) {
            buffer[0] = '\0';
        }
    }
}

// Escribe una línea con su marca de tiempo en el destino
static void write_line(FILE *destination, const char *timestamp,
                       const char *message)
{
    if (destination == NULL) {
        return;
    }
    fprintf(destination, "[%s] %s\n", timestamp, message);
    fflush(destination);
}

int logger_open(Logger *logger, const char *log_path)
{
    if (logger == NULL) {
        return -1;
    }
    logger->file = NULL;
    pthread_mutex_init(&logger->lock, NULL);

    if (log_path == NULL) {
        return -1;
    }

    FILE *file = fopen(log_path, "a");
    if (file == NULL) {
        // Si el archivo no se pudo abrir, el servidor seguirá registrando en stderr
        return -1;
    }
    logger->file = file;
    return 0;
}

void logger_printf(Logger *logger, const char *format, ...)
{
    char timestamp[32];
    char message[1024];

    format_timestamp(timestamp, sizeof(timestamp));

    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    FILE *log_file = (logger != NULL) ? (FILE *)logger->file : NULL;

    // Una sola región crítica para que archivo y stderr salgan sin entrelazarse
    if (logger != NULL) {
        pthread_mutex_lock(&logger->lock);
    }
    write_line(log_file, timestamp, message);
    write_line(stderr, timestamp, message);
    if (logger != NULL) {
        pthread_mutex_unlock(&logger->lock);
    }
}

void logger_close(Logger *logger)
{
    if (logger == NULL) {
        return;
    }
    if (logger->file != NULL) {
        fclose((FILE *)logger->file);
        logger->file = NULL;
    }
    pthread_mutex_destroy(&logger->lock);
}
