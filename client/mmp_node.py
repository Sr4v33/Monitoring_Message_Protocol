"""Nodo MMP, se registra por TCP y envía STATUS por UDP y EVENT por TCP"""

import argparse
import datetime
import logging
import sys

import mmp_protocol as proto
import node_state
from mmp_connection import TcpConnection, send_udp_datagram

logger = logging.getLogger("mmp_node")


def _now_iso():
    """Devuelve la marca de tiempo UTC"""
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class MmpNode:
    """Nodo que ejecuta los flujos REGISTER, STATUS y EVENT"""

    def __init__(self, node_id, host, tcp_port, udp_port):
        self.node_id = node_id
        self.host = host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.state = node_state.NodeStateMachine()
        self._message_counter = 0
        self._tcp = None

    def _next_id(self):
        """Genera un id de mensaje único y legible para el nodo"""
        self._message_counter += 1
        return "{}-{:06d}".format(self.node_id, self._message_counter)

    def _transition(self, event):
        """Aplica una transición de estado y la registra (estado anterior -> nuevo)"""
        previous, current = self.state.apply(event)
        logger.info("estado %s -> %s (evento %s)", previous, current, event)

    def register(self):
        """Conecta por TCP y realiza el flujo REGISTER esperando REGISTER_ACK"""
        self._tcp = TcpConnection(self.host, self.tcp_port)
        self._tcp.connect()
        logger.info("conectado al servidor %s:%s", self.host, self.tcp_port)

        self._transition("REGISTER")  # DISCONNECTED -> REGISTERING
        message = proto.build_message(
            proto.TYPE_REGISTER, self._next_id(), node_id=self.node_id,
            timestamp=_now_iso(), payload={"device_type": "sensor", "version": "1.0"},
        )
        self._tcp.send_message(message)
        logger.info("REGISTER enviado id=%s", message["id"])

        return self._await_register_ack()

    def _await_register_ack(self):
        """Espera la respuesta al REGISTER y actualiza el estado segun sea ACK o ERROR"""
        raw = self._tcp.receive_message()
        if raw is None:
            logger.error("el servidor cerró la conexión antes de responder al REGISTER")
            self._transition("DISCONNECT")
            return False

        result, response = proto.parse(raw)
        if result != proto.PARSE_OK:
            logger.error("respuesta de registro ilegible (%s)", result)
            self._transition("REGISTER_ERROR")
            return False

        rtype = response["type"]
        if rtype == proto.TYPE_REGISTER_ACK and \
                response.get("payload", {}).get("status") == "OK":
            logger.info("REGISTER_ACK recibido: registro aceptado")
            self._transition("REGISTER_OK")  # REGISTERING -> REGISTERED
            return True
        if rtype == proto.TYPE_ERROR:
            logger.error("registro rechazado: %s", response.get("payload", {}).get("code"))
            self._transition("REGISTER_ERROR")
            return False

        logger.error("respuesta inesperada al REGISTER: tipo %s", rtype)
        self._transition("REGISTER_ERROR")
        return False

    def send_status(self, cpu_usage):
        """Envía un STATUS por UDP, pero no espera respuesta"""
        if not self.state.can("SEND_STATUS"):
            logger.warning("STATUS no permitido en el estado %s", self.state.state)
            return
        message = proto.build_message(
            proto.TYPE_STATUS, self._next_id(), node_id=self.node_id,
            timestamp=_now_iso(),
            payload={"cpu_usage": cpu_usage, "operational_state": "RUNNING",
                     "availability": True},
        )
        send_udp_datagram(self.host, self.udp_port, message)
        self._transition("SEND_STATUS")
        logger.info("STATUS enviado por UDP id=%s cpu_usage=%s", message["id"], cpu_usage)

    def send_event(self, event_type, severity, description):
        """Envía un EVENT por TCP y procesa el EVENT_ACK o el ERROR de respuesta"""
        if not self.state.can("SEND_EVENT"):
            logger.warning("EVENT no permitido en el estado %s", self.state.state)
            return
        message = proto.build_message(
            proto.TYPE_EVENT, self._next_id(), node_id=self.node_id,
            timestamp=_now_iso(),
            payload={"event_type": event_type, "severity": severity,
                     "description": description},
        )
        self._tcp.send_message(message)
        self._transition("SEND_EVENT")  # REGISTERED -> WAITING_EVENT_ACK
        logger.info("EVENT enviado id=%s tipo=%s", message["id"], event_type)
        self._await_event_ack(message["id"])

    def _await_event_ack(self, event_id):
        """Espera el EVENT_ACK correlacionado por id, o procesa un ERROR del servidor"""
        raw = self._tcp.receive_message()
        if raw is None:
            logger.error("servidor desconectado mientras se esperaba EVENT_ACK")
            self._transition("DISCONNECT")
            return

        result, response = proto.parse(raw)
        if result != proto.PARSE_OK:
            logger.error("respuesta ilegible al EVENT (%s)", result)
            return

        if response["type"] == proto.TYPE_EVENT_ACK:
            if response.get("id") == event_id:
                logger.info("EVENT_ACK recibido para id=%s", event_id)
            else:
                logger.warning("EVENT_ACK con id distinto: %s", response.get("id"))
            self._transition("EVENT_ACK")  # vuelve a REGISTERED
        elif response["type"] == proto.TYPE_ERROR:
            # Un ERROR del servidor no debe terminar el proceso, por lo que se registra y se vuelve a REGISTERED
            logger.error("ERROR al enviar EVENT: %s", response.get("payload", {}).get("code"))
            self._transition("EVENT_ACK")
        else:
            logger.warning("respuesta inesperada al EVENT: tipo %s", response["type"])

    def close(self):
        """Cierra la conexión TCP del nodo de forma ordenada"""
        if self._tcp is not None:
            self._tcp.close()
            self._tcp = None
        logger.info("nodo detenido")


def main(argv=None):
    """Punto de entrada, se encarga de parsear argumentos y ejecutar una demostración de los flujos del nodo"""
    parser = argparse.ArgumentParser(description="Nodo MMP")
    parser.add_argument("--node-id", required=True, help="identificador lógico del nodo")
    parser.add_argument("--host", required=True, help="nombre o dirección del servidor")
    parser.add_argument("--tcp-port", required=True, help="puerto TCP del servidor")
    parser.add_argument("--udp-port", help="puerto UDP del servidor, que por defecto es igual al TCP")
    parser.add_argument("--cpu-usage", type=float, default=42.5, help="valor de cpu_usage del STATUS")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s [%(name)s] %(message)s")

    udp_port = args.udp_port if args.udp_port else args.tcp_port
    node = MmpNode(args.node_id, args.host, args.tcp_port, udp_port)
    logger.info("iniciando nodo node_id=%s servidor=%s tcp=%s udp=%s",
                args.node_id, args.host, args.tcp_port, udp_port)

    try:
        if not node.register():
            return 1
        node.send_status(args.cpu_usage)
        node.send_event("HIGH_TEMPERATURE", "CRITICAL", "temperatura sobre el umbral")
    except (ConnectionError, OSError) as error:
        logger.error("fallo de comunicación: %s", error)
        return 1
    finally:
        node.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
