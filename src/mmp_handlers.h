// Flujos MMP, registro de nodos y despacho de mensajes a su handler
#ifndef MMP_HANDLERS_H
#define MMP_HANDLERS_H
#include <pthread.h>
#include "protocol.h"

// Máximo de nodos registrados simultáneamente
#define REGISTRY_MAX_NODES 32
// Registros históricos de STATUS por nodo
#define REGISTRY_HISTORY_PER_NODE 8

// Métrica histórica mínima tomada de STATUS (ts + cpu_usage)
typedef struct {
    char timestamp[MMP_TIMESTAMP_MAX_LEN];
    double cpu_usage;
    int has_cpu_usage; // es 1 si el STATUS traía cpu_usage
} StatusRecord;

// Estado conocido de un nodo registrado
typedef struct {
    char node_id[MMP_NODE_ID_MAX_LEN];
    int in_use; // es 1 si la entrada está ocupada
    StatusRecord history[REGISTRY_HISTORY_PER_NODE];
    int history_count; // cantidad de registros válidos
    int history_next; // índice circular de escritura
} NodeEntry;

// Registro central de nodos, el mutex protege su acceso concurrente desde varios hilos
typedef struct {
    NodeEntry nodes[REGISTRY_MAX_NODES];
    pthread_mutex_t lock;
} NodeRegistry;

// Inicialización del registro vacío y de su mutex
void registry_init(NodeRegistry *registry);

// Libera los recursos del registro (mutex), se llama al terminar el servidor
void registry_destroy(NodeRegistry *registry);

// Resultado del despacho, que indica si debe enviarse una respuesta y por medio de qué transporte lógico
typedef struct {
    int has_response; // es 1 si out_response quedó listo para enviar
    int response_is_error; // es 1 si la respuesta es un mensaje ERROR
} DispatchOutcome;

// Procesa una solicitud ya parseada y, cuando el flujo lo requiere, construye la respuesta
// origin_is_tcp distingue el transporte de llegada
// out_response se rellena solo si outcome.has_response es 1 porque el llamador lo libera con mmp_message_free
DispatchOutcome mmp_dispatch(NodeRegistry *registry,
                             const MmpMessage *request,
                             int origin_is_tcp,
                             MmpMessage *out_response);

#endif
