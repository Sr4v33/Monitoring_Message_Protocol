// Implementación de los flujos REGISTER/STATUS/EVENT/QUERY/ERRO
#include "mmp_handlers.h"

#include <stdio.h>
#include <string.h>

void registry_init(NodeRegistry *registry)
{
    if (registry == NULL) {
        return;
    }
    // Se limpian solo las entradas de nodos, el mutex se inicializa aparte
    memset(registry->nodes, 0, sizeof(registry->nodes));
    pthread_mutex_init(&registry->lock, NULL);
}

void registry_destroy(NodeRegistry *registry)
{
    if (registry == NULL) {
        return;
    }
    pthread_mutex_destroy(&registry->lock);
}

// Busca un nodo por su id y devuelve la entrada o NULL si no está registrado
static NodeEntry *registry_find(NodeRegistry *registry, const char *node_id)
{
    for (int i = 0; i < REGISTRY_MAX_NODES; i++) {
        if (registry->nodes[i].in_use &&
            strcmp(registry->nodes[i].node_id, node_id) == 0) {
            return &registry->nodes[i];
        }
    }
    return NULL;
}

// Reserva la primera entrada libre para un nodo nuevo, si el registro está lleno retorna NULL
static NodeEntry *registry_add(NodeRegistry *registry, const char *node_id)
{
    for (int i = 0; i < REGISTRY_MAX_NODES; i++) {
        if (!registry->nodes[i].in_use) {
            NodeEntry *entry = &registry->nodes[i];
            memset(entry, 0, sizeof(*entry));
            entry->in_use = 1;
            snprintf(entry->node_id, sizeof(entry->node_id), "%s", node_id);
            return entry;
        }
    }
    return NULL;
}

// Copia el header del request a la respuesta y fija el tipo - estado base
static void init_response(MmpMessage *response, MmpMessageType type,
                          const MmpMessage *request)
{
    mmp_message_init(response);
    response->type = type;
    // El id de la respuesta reutiliza el del request para correlacionar
    snprintf(response->id, sizeof(response->id), "%s", request->id);
    snprintf(response->node_id, sizeof(response->node_id), "%s", request->node_id);
}

// Construye un mensaje tipo ERROR con el código indicado y un mensaje legible
static void build_error(MmpMessage *response, const MmpMessage *request,
                        const char *code, const char *detail)
{
    init_response(response, MMP_TYPE_ERROR, request);
    response->payload = cJSON_CreateObject();
    if (response->payload != NULL) {
        cJSON_AddStringToObject(response->payload, "code", code);
        cJSON_AddStringToObject(response->payload, "message", detail);
    }
}

// El REGISTER, registra el nodo y responde REGISTER_ACK OK, o ERROR si falta el node_id
static DispatchOutcome handle_register(NodeRegistry *registry,
                                       const MmpMessage *request,
                                       MmpMessage *response)
{
    DispatchOutcome outcome = { 1, 0 };

    if (request->node_id[0] == '\0') {
        build_error(response, request, "INVALID_PARAMETER", "falta node_id en REGISTER");
        outcome.response_is_error = 1;
        return outcome;
    }

    NodeEntry *entry = registry_find(registry, request->node_id);
    if (entry == NULL) {
        entry = registry_add(registry, request->node_id);
    }
    if (entry == NULL) {
        build_error(response, request, "INTERNAL_ERROR", "registro de nodos lleno");
        outcome.response_is_error = 1;
        return outcome;
    }

    // La respuesta REGISTER_ACK retorna status OK
    init_response(response, MMP_TYPE_REGISTER_ACK, request);
    response->payload = cJSON_CreateObject();
    if (response->payload != NULL) {
        cJSON_AddStringToObject(response->payload, "status", "OK");
    }
    return outcome;
}

// Guardado del histórico de cpu_usage del STATUS en el buffer del nodo
static void record_status(NodeEntry *entry, const MmpMessage *request)
{
    StatusRecord record;
    memset(&record, 0, sizeof(record));
    snprintf(record.timestamp, sizeof(record.timestamp), "%s", request->timestamp);

    if (request->payload != NULL) {
        const cJSON *cpu = cJSON_GetObjectItemCaseSensitive(request->payload, "cpu_usage");
        if (cJSON_IsNumber(cpu)) {
            record.cpu_usage = cpu->valuedouble;
            record.has_cpu_usage = 1;
        }
    }

    entry->history[entry->history_next] = record;
    entry->history_next = (entry->history_next + 1) % REGISTRY_HISTORY_PER_NODE;
    if (entry->history_count < REGISTRY_HISTORY_PER_NODE) {
        entry->history_count++;
    }
}

// El STATUS actualiza el último estado del nodo, pero no genera respuesta
static DispatchOutcome handle_status(NodeRegistry *registry,
                                     const MmpMessage *request,
                                     MmpMessage *response)
{
    (void)response;
    DispatchOutcome outcome = { 0, 0 };

    if (request->node_id[0] == '\0') {
        // si no existe node_id , el STATUS no puede asociarse, entonces se ignora sin responder
        return outcome;
    }
    NodeEntry *entry = registry_find(registry, request->node_id);
    if (entry == NULL) {
        // STATUS no responde si el nodo no quda registrado, así que solo se descarta
        return outcome;
    }
    record_status(entry, request);
    return outcome;
}

// el EVENT requiere de un nodo registrado, y responde EVENT_ACK con el mismo id
static DispatchOutcome handle_event(NodeRegistry *registry,
                                    const MmpMessage *request,
                                    MmpMessage *response)
{
    DispatchOutcome outcome = { 1, 0 };

    if (request->node_id[0] == '\0') {
        build_error(response, request, "INVALID_PARAMETER", "falta node_id en EVENT");
        outcome.response_is_error = 1;
        return outcome;
    }
    if (registry_find(registry, request->node_id) == NULL) {
        build_error(response, request, "UNKNOWN_NODE", "el nodo no está registrado");
        outcome.response_is_error = 1;
        return outcome;
    }

    // se da EVENT_ACK con el mismo id del EVENT para que el nodo lo correlacione
    init_response(response, MMP_TYPE_EVENT_ACK, request);
    response->payload = cJSON_CreateObject();
    if (response->payload != NULL) {
        cJSON_AddStringToObject(response->payload, "status", "RECEIVED");
    }
    return outcome;
}

// Se construye el array de registros históricos (ts + cpu_usage) para una RESPONSE HISTORICAL
static cJSON *build_history_array(const NodeEntry *entry, int limit)
{
    cJSON *records = cJSON_CreateArray();
    if (records == NULL) {
        return NULL;
    }

    // Recorre el histórico del más antiguo al más reciente respetando el buffer
    int count = entry->history_count;
    int start = (entry->history_count == REGISTRY_HISTORY_PER_NODE)
                    ? entry->history_next : 0;
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        if (limit > 0 && emitted >= limit) {
            break;
        }
        const StatusRecord *record =
            &entry->history[(start + i) % REGISTRY_HISTORY_PER_NODE];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            continue;
        }
        cJSON_AddStringToObject(item, "ts", record->timestamp);
        if (record->has_cpu_usage) {
            cJSON_AddNumberToObject(item, "cpu_usage", record->cpu_usage);
        }
        cJSON_AddItemToArray(records, item);
        emitted++;
    }
    return records;
}

// El QUERY valida parámetros y responde RESPONSE con el histórico del nodo, o ERROR
static DispatchOutcome handle_query(NodeRegistry *registry,
                                    const MmpMessage *request,
                                    MmpMessage *response)
{
    DispatchOutcome outcome = { 1, 0 };

    if (request->node_id[0] == '\0') {
        build_error(response, request, "INVALID_PARAMETER", "falta node_id en QUERY");
        outcome.response_is_error = 1;
        return outcome;
    }

    // Lee el limit del payload, que debe ser mayor que cero si existe
    int limit = 0;
    if (request->payload != NULL) {
        const cJSON *limit_field =
            cJSON_GetObjectItemCaseSensitive(request->payload, "limit");
        if (limit_field != NULL) {
            if (!cJSON_IsNumber(limit_field) || limit_field->valueint <= 0) {
                build_error(response, request, "INVALID_PARAMETER", "limit debe ser mayor que cero");
                outcome.response_is_error = 1;
                return outcome;
            }
            limit = limit_field->valueint;
        }
    }

    NodeEntry *entry = registry_find(registry, request->node_id);
    if (entry == NULL) {
        build_error(response, request, "UNKNOWN_NODE", "el nodo no está registrado");
        outcome.response_is_error = 1;
        return outcome;
    }

    init_response(response, MMP_TYPE_RESPONSE, request);
    response->payload = cJSON_CreateObject();
    if (response->payload != NULL) {
        cJSON_AddStringToObject(response->payload, "status", "OK");
        cJSON *records = build_history_array(entry, limit);
        if (records != NULL) {
            cJSON_AddItemToObject(response->payload, "records", records);
        }
    }
    return outcome;
}

DispatchOutcome mmp_dispatch(NodeRegistry *registry,
                             const MmpMessage *request,
                             int origin_is_tcp,
                             MmpMessage *out_response)
{
    DispatchOutcome outcome = { 0, 0 };
    if (registry == NULL || request == NULL || out_response == NULL) {
        return outcome;
    }

    // El caso default no toca el estado compartido, así que no necesita el mutex
    if (request->type != MMP_TYPE_REGISTER && request->type != MMP_TYPE_STATUS &&
        request->type != MMP_TYPE_EVENT && request->type != MMP_TYPE_QUERY) {
        // Tipos que el servidor no procesa como entrada (ACKs, RESPONSE, ERROR o desconocido),
        // solo se responde con ERROR si llegó por TCP, donde el emisor espera correlación
        if (origin_is_tcp) {
            build_error(out_response, request, "UNKNOWN_MESSAGE",
                        "tipo de mensaje no procesable por el servidor");
            outcome.has_response = 1;
            outcome.response_is_error = 1;
        }
        return outcome;
    }

    // Región crítica mínima, los handlers leen y escriben el registro compartido
    pthread_mutex_lock(&registry->lock);
    switch (request->type) {
    case MMP_TYPE_REGISTER:
        outcome = handle_register(registry, request, out_response);
        break;
    case MMP_TYPE_STATUS:
        outcome = handle_status(registry, request, out_response);
        break;
    case MMP_TYPE_EVENT:
        outcome = handle_event(registry, request, out_response);
        break;
    case MMP_TYPE_QUERY:
        outcome = handle_query(registry, request, out_response);
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&registry->lock);
    return outcome;
}
