# Estados definidos para el nodo.
DISCONNECTED = "DISCONNECTED"
REGISTERING = "REGISTERING"
REGISTERED = "REGISTERED"
WAITING_EVENT_ACK = "WAITING_EVENT_ACK"
ERROR = "ERROR"

# Transiciones permitidas: estado_actual -> {evento: nuevo_estado}
_TRANSITIONS = {
    DISCONNECTED: {
        "REGISTER": REGISTERING,
    },
    REGISTERING: {
        "REGISTER_OK": REGISTERED,
        "REGISTER_ERROR": ERROR,
        "DISCONNECT": DISCONNECTED,
    },
    REGISTERED: {
        "SEND_STATUS": REGISTERED, # STATUS no cambia el estado de registro
        "SEND_EVENT": WAITING_EVENT_ACK,
        "DISCONNECT": DISCONNECTED,
    },
    WAITING_EVENT_ACK: {
        "EVENT_ACK": REGISTERED,
        "SEND_STATUS": WAITING_EVENT_ACK, # STATUS sigue siendo independiente por UDP
        "DISCONNECT": DISCONNECTED,
    },
    ERROR: {},
}


class NodeStateMachine:
    """Mantiene el estado actual del nodo y aplica solo las transiciones definidas"""

    def __init__(self):
        self.state = DISCONNECTED

    def can(self, event):
        """Indica si el evento es una transición válida desde el estado actual"""
        return event in _TRANSITIONS.get(self.state, {})

    def apply(self, event):
        """Aplica una transición, devolviendo (estado_anterior, estado_nuevo) o lanza error si es inválida"""
        allowed = _TRANSITIONS.get(self.state, {})
        if event not in allowed:
            raise ValueError(
                "transición inválida: {} desde {}".format(event, self.state)
            )
        previous = self.state
        self.state = allowed[event]
        return previous, self.state
