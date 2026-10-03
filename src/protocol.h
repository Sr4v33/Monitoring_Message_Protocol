#ifndef MMP_PROTOCOL_H
#define MMP_PROTOCOL_H

#include "cJSON.h"

// Versión del protocolo que lleva cada mensaje
#define MMP_PROTOCOL_VERSION 1

// Longitud máxima del identificador de cada mensaje
#define MMP_ID_MAX_LEN 64
// Longitud máxima del identificador de cada nodo
#define MMP_NODE_ID_MAX_LEN 64
// Longitud máxima del timestamp
#define MMP_TIMESTAMP_MAX_LEN 32

// Definición de tipos de mensajes
typedef enum {
    MMP_TYPE_UNKNOWN = 0,
    MMP_TYPE_REGISTER,
    MMP_TYPE_REGISTER_ACK,
    MMP_TYPE_STATUS,
    MMP_TYPE_EVENT,
    MMP_TYPE_EVENT_ACK,
    MMP_TYPE_QUERY,
    MMP_TYPE_RESPONSE,
    MMP_TYPE_ERROR
} MmpMessageType;

// Interpretación del buffer
typedef enum {
    MMP_PARSE_OK = 0,
    MMP_PARSE_INVALID_FORMAT, // el buffer no es JSON válido
    MMP_PARSE_INVALID_PARAMETER, // JSON válido pero falta un campo necesario o el campo es inválido
    MMP_PARSE_UNKNOWN_MESSAGE // el "type" no es un mensaje MMP conocido
} MmpParseResult;

// Estructura del mensaje MMP, con encabezado y payload específico
typedef struct {
    int version; // campo "v"
    MmpMessageType type;                   // campo "type"
    char id[MMP_ID_MAX_LEN];               // campo "id"
    char node_id[MMP_NODE_ID_MAX_LEN];     // campo "node_id" (puede ser "")
    char timestamp[MMP_TIMESTAMP_MAX_LEN]; // campo "ts" (puede ser "")
    cJSON *payload;                        // campo "payload" (puede ser NULL)
} MmpMessage;

MmpMessageType mmp_type_from_string(const char *type_name);
const char *mmp_type_to_string(MmpMessageType type);

// Inicialización del mensaje con valores vacíos
void mmp_message_init(MmpMessage *message);

// Limpieza del payload
void mmp_message_free(MmpMessage *message);

// Serialización del mensaje, si falla retorna NULL
char *mmp_serialize(const MmpMessage *message);

// Interpretación del buffeer JSON a mensaje
MmpParseResult mmp_parse(const char *json_buffer, MmpMessage *out_message);

#endif
