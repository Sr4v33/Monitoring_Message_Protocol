import json

# Versión del protocolo que lleva cada mensaje (campo "v")
PROTOCOL_VERSION = 1

# Tipos de mensaje definidos
TYPE_REGISTER = "REGISTER"
TYPE_REGISTER_ACK = "REGISTER_ACK"
TYPE_STATUS = "STATUS"
TYPE_EVENT = "EVENT"
TYPE_EVENT_ACK = "EVENT_ACK"
TYPE_QUERY = "QUERY"
TYPE_RESPONSE = "RESPONSE"
TYPE_ERROR = "ERROR"

# Resultados de parseo equivalentes
PARSE_OK = "OK"
PARSE_INVALID_FORMAT = "INVALID_FORMAT"
PARSE_INVALID_PARAMETER = "INVALID_PARAMETER"
PARSE_UNKNOWN_MESSAGE = "UNKNOWN_MESSAGE"

# Conjunto de tipos para distinguir un tipo desconocido de uno válido
_KNOWN_TYPES = {
    TYPE_REGISTER, TYPE_REGISTER_ACK, TYPE_STATUS, TYPE_EVENT,
    TYPE_EVENT_ACK, TYPE_QUERY, TYPE_RESPONSE, TYPE_ERROR,
}


def build_message(message_type, message_id, node_id=None, timestamp=None, payload=None):
    """Se construye un mensaje MMP como diccionario con el encabezado común"""
    message = {"v": PROTOCOL_VERSION, "type": message_type, "id": message_id}
    if node_id:
        message["node_id"] = node_id
    if timestamp:
        message["ts"] = timestamp
    if payload is not None:
        message["payload"] = payload
    return message


def serialize(message):
    """Se serializa el mensaje a una línea JSON UTF-8 sin salto de linea, porque el '\\n' lo agrega el transporte"""
    return json.dumps(message, separators=(",", ":"))


def parse(buffer):
    """Parsea un buffer JSON a (resultado, mensaje), el mensaje es None salvo en PARSE_OK"""
    try:
        message = json.loads(buffer)
    except (ValueError, TypeError):
        return PARSE_INVALID_FORMAT, None

    if not isinstance(message, dict):
        return PARSE_INVALID_FORMAT, None

    message_type = message.get("type")
    if not isinstance(message_type, str):
        return PARSE_INVALID_PARAMETER, None
    if message_type not in _KNOWN_TYPES:
        return PARSE_UNKNOWN_MESSAGE, None

    # "id" es obligatorio porque correlaciona solicitudes y respuestas
    if not isinstance(message.get("id"), str):
        return PARSE_INVALID_PARAMETER, None

    return PARSE_OK, message
