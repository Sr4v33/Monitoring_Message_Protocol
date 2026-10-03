// Implementación de mensajes MMP
#include "protocol.h"

#include <string.h>

// Par de tipo_mensaje y su "type"
typedef struct {
    MmpMessageType type;
    const char *name;
} MmpTypeName;

// Tabla de emparejamiento entre nombre y tipo
static const MmpTypeName MMP_TYPE_NAMES[] = {
    { MMP_TYPE_REGISTER, "REGISTER" },
    { MMP_TYPE_REGISTER_ACK, "REGISTER_ACK" },
    { MMP_TYPE_STATUS, "STATUS" },
    { MMP_TYPE_EVENT, "EVENT" },
    { MMP_TYPE_EVENT_ACK, "EVENT_ACK" },
    { MMP_TYPE_QUERY, "QUERY" },
    { MMP_TYPE_RESPONSE, "RESPONSE" },
    { MMP_TYPE_ERROR, "ERROR" }
};

// Cantidad de entradas de la tabla con los tipos
static const size_t MMP_TYPE_COUNT =
    sizeof(MMP_TYPE_NAMES) / sizeof(MMP_TYPE_NAMES[0]);

MmpMessageType mmp_type_from_string(const char *type_name)
{
    if (type_name == NULL) {
        return MMP_TYPE_UNKNOWN;
    }
    for (size_t i = 0; i < MMP_TYPE_COUNT; i++) {
        if (strcmp(type_name, MMP_TYPE_NAMES[i].name) == 0) {
            return MMP_TYPE_NAMES[i].type;
        }
    }
    return MMP_TYPE_UNKNOWN;
}

const char *mmp_type_to_string(MmpMessageType type)
{
    for (size_t i = 0; i < MMP_TYPE_COUNT; i++) {
        if (MMP_TYPE_NAMES[i].type == type) {
            return MMP_TYPE_NAMES[i].name;
        }
    }
    return "UNKNOWN";
}

void mmp_message_init(MmpMessage *message)
{
    if (message == NULL) {
        return;
    }
    message->version = MMP_PROTOCOL_VERSION;
    message->type = MMP_TYPE_UNKNOWN;
    message->id[0] = '\0';
    message->node_id[0] = '\0';
    message->timestamp[0] = '\0';
    message->payload = NULL;
}

void mmp_message_free(MmpMessage *message)
{
    if (message == NULL) {
        return;
    }
    if (message->payload != NULL) {
        cJSON_Delete(message->payload);
    }
    mmp_message_init(message);
}

static int copy_bounded_field(char *destination, size_t destination_size,
                              const char *source)
{
    size_t length = strlen(source);
    if (length >= destination_size) {
        length = destination_size - 1;
        memcpy(destination, source, length);
        destination[length] = '\0';
        return 0;
    }
    memcpy(destination, source, length + 1);
    return 1;
}

char *mmp_serialize(const MmpMessage *message)
{
    if (message == NULL) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    // Transmite el encabezado, si es NULL, el node_id y el ts se omiten vacíos
    int ok = 1;
    ok = ok && (cJSON_AddNumberToObject(root, "v", message->version) != NULL);
    ok = ok && (cJSON_AddStringToObject(root, "type",
                                        mmp_type_to_string(message->type)) != NULL);
    ok = ok && (cJSON_AddStringToObject(root, "id", message->id) != NULL);
    if (ok && message->node_id[0] != '\0') {
        ok = cJSON_AddStringToObject(root, "node_id", message->node_id) != NULL;
    }
    if (ok && message->timestamp[0] != '\0') {
        ok = cJSON_AddStringToObject(root, "ts", message->timestamp) != NULL;
    }
    if (ok && message->payload != NULL) {
        // Se duplica para que el mensaje conserve la propiedad del payload
        cJSON *payload_copy = cJSON_Duplicate(message->payload, 1);
        if (payload_copy == NULL) {
            ok = 0;
        } else {
            cJSON_AddItemToObject(root, "payload", payload_copy);
        }
    }

    if (!ok) {
        cJSON_Delete(root);
        return NULL;
    }

    // Se mantiene sin formato para una sola línea de TCP o de UDP
    char *json_text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_text;
}

// Lee el campo requerido, si falta, no es cadena o no cabe, se retorna INVALID_PARAMETER
static MmpParseResult read_required_string(const cJSON *root, const char *key,
                                           char *destination,
                                           size_t destination_size)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(field) || field->valuestring == NULL) {
        return MMP_PARSE_INVALID_PARAMETER;
    }
    if (!copy_bounded_field(destination, destination_size, field->valuestring)) {
        return MMP_PARSE_INVALID_PARAMETER;
    }
    return MMP_PARSE_OK;
}

// Lee el campo de cadena opcional y lo deja vacío si no existe, si existe y es inválido, es INVALID_PARAMETER.
static MmpParseResult read_optional_string(const cJSON *root, const char *key,
                                           char *destination,
                                           size_t destination_size)
{
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(root, key);
    if (field == NULL) {
        destination[0] = '\0';
        return MMP_PARSE_OK;
    }
    if (!cJSON_IsString(field) || field->valuestring == NULL) {
        return MMP_PARSE_INVALID_PARAMETER;
    }
    if (!copy_bounded_field(destination, destination_size, field->valuestring)) {
        return MMP_PARSE_INVALID_PARAMETER;
    }
    return MMP_PARSE_OK;
}

MmpParseResult mmp_parse(const char *json_buffer, MmpMessage *out_message)
{
    if (out_message == NULL) {
        return MMP_PARSE_INVALID_PARAMETER;
    }
    mmp_message_init(out_message);

    if (json_buffer == NULL) {
        return MMP_PARSE_INVALID_FORMAT;
    }

    cJSON *root = cJSON_Parse(json_buffer);
    if (root == NULL) {
        return MMP_PARSE_INVALID_FORMAT;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return MMP_PARSE_INVALID_FORMAT;
    }

    // El "type" que es requerido si está ausente o es inválido es INVALID_PARAMETER,
    // si no es reconocido, es UNKNOWN_MESSAGE
    const cJSON *type_field = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type_field) || type_field->valuestring == NULL) {
        cJSON_Delete(root);
        return MMP_PARSE_INVALID_PARAMETER;
    }
    MmpMessageType parsed_type = mmp_type_from_string(type_field->valuestring);
    if (parsed_type == MMP_TYPE_UNKNOWN) {
        cJSON_Delete(root);
        return MMP_PARSE_UNKNOWN_MESSAGE;
    }
    out_message->type = parsed_type;

    // El "id" requerido que relaciona las solicitudes y las respuestas
    MmpParseResult result =
        read_required_string(root, "id", out_message->id, sizeof(out_message->id));
    if (result != MMP_PARSE_OK) {
        mmp_message_free(out_message);
        cJSON_Delete(root);
        return result;
    }

    // La versión "v" que es opcional, pero por defecto la versión del protocolo
    const cJSON *version_field = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (version_field == NULL) {
        out_message->version = MMP_PROTOCOL_VERSION;
    } else if (cJSON_IsNumber(version_field)) {
        out_message->version = version_field->valueint;
    } else {
        mmp_message_free(out_message);
        cJSON_Delete(root);
        return MMP_PARSE_INVALID_PARAMETER;
    }

    result = read_optional_string(root, "node_id", out_message->node_id,
                                  sizeof(out_message->node_id));
    if (result != MMP_PARSE_OK) {
        mmp_message_free(out_message);
        cJSON_Delete(root);
        return result;
    }

    result = read_optional_string(root, "ts", out_message->timestamp,
                                  sizeof(out_message->timestamp));
    if (result != MMP_PARSE_OK) {
        mmp_message_free(out_message);
        cJSON_Delete(root);
        return result;
    }

    // El "payload" opcional, que si existe debe ser objeto y se duplica para que el mensaje lo contenga
    const cJSON *payload_field = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (payload_field != NULL) {
        if (!cJSON_IsObject(payload_field)) {
            mmp_message_free(out_message);
            cJSON_Delete(root);
            return MMP_PARSE_INVALID_PARAMETER;
        }
        out_message->payload = cJSON_Duplicate(payload_field, 1);
        if (out_message->payload == NULL) {
            mmp_message_free(out_message);
            cJSON_Delete(root);
            return MMP_PARSE_INVALID_PARAMETER;
        }
    }

    cJSON_Delete(root);
    return MMP_PARSE_OK;
}

