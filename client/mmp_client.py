"""Cliente, que envía QUERY por TCP y muestra la RESPONSE o el ERROR"""

import argparse
import datetime
import logging
import sys

import mmp_protocol as proto
from mmp_connection import TcpConnection

logger = logging.getLogger("mmp_client")


def _now_iso():
    """Marca de tiempo UTC en formato ISO"""
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def run_query(host, tcp_port, node_id, limit):
    """Conecta por TCP, envia un QUERY histórico y procesa la respuesta del servidor"""
    connection = TcpConnection(host, tcp_port)
    connection.connect()
    logger.info("conectado al servidor %s:%s", host, tcp_port)

    # Se define el QUERY historico con query_type y limit
    payload = {"query_type": "HISTORICAL", "limit": limit}
    message = proto.build_message(proto.TYPE_QUERY, "query-000001",
                                  node_id=node_id, timestamp=_now_iso(), payload=payload)
    connection.send_message(message)
    logger.info("QUERY enviado node_id=%s limit=%s", node_id, limit)

    exit_code = _handle_response(connection)
    connection.close()
    return exit_code


def _handle_response(connection):
    """Recibe y clasifica la respuesta al QUERY: RESPONSE con registros o ERROR controlado."""
    raw = connection.receive_message()
    if raw is None:
        logger.error("el servidor cerró la conexion sin responder")
        return 1

    result, response = proto.parse(raw)
    if result != proto.PARSE_OK:
        logger.error("respuesta ilegible del servidor (%s)", result)
        return 1

    if response["type"] == proto.TYPE_RESPONSE:
        records = response.get("payload", {}).get("records", [])
        logger.info("RESPONSE recibida: %d registro(s)", len(records))
        for record in records:
            logger.info("  ts=%s cpu_usage=%s",
                        record.get("ts"), record.get("cpu_usage"))
        return 0
    if response["type"] == proto.TYPE_ERROR:
        logger.error("ERROR del servidor: %s", response.get("payload", {}).get("code"))
        return 1

    logger.warning("respuesta inesperada: tipo %s", response["type"])
    return 1


def main(argv=None):
    """Punto de entrada: parsea argumentos y ejecuta una consulta historica."""
    parser = argparse.ArgumentParser(description="Cliente administrativo MMP")
    parser.add_argument("--host", required=True, help="nombre o direccion del servidor")
    parser.add_argument("--tcp-port", required=True, help="puerto TCP del servidor")
    parser.add_argument("--node-id", required=True, help="nodo a consultar")
    parser.add_argument("--limit", type=int, default=5, help="cantidad de registros a solicitar")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s [%(name)s] %(message)s")

    try:
        return run_query(args.host, args.tcp_port, args.node_id, args.limit)
    except (ConnectionError, OSError) as error:
        logger.error("fallo de comunicación: %s", error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
