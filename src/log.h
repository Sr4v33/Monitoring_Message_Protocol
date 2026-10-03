// Logging básico, archivo + stderr con timestamp
#ifndef MMP_LOG_H
#define MMP_LOG_H

// Destino de log
typedef struct {
    void *file; // FILE* del archivo de log
} Logger;

// Abre el log en modo "añadir", 0 si éxito, -1 si no abre.
int logger_open(Logger *logger, const char *log_path);

// Escribe una línea con marca de tiempo.
void logger_printf(Logger *logger, const char *format, ...);

// Cierra el archivo de log si estaba abierto
void logger_close(Logger *logger);

#endif
