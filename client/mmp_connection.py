"""Transporte del lado cliente con resolucion por nombre, TCP con framing '\\n' y enví UDP"""

import socket

from mmp_protocol import serialize


class LineStream:
    """Acumulador de bytes TCP, entrega mensajes completos delimitados por '\\n' del lado del cliente"""

    def __init__(self):
        self._buffer = b""

    def feed(self, data):
        """Agrega bytes reales recibidos del socket"""
        self._buffer += data

    def next_line(self):
        """Extrae el siguiente mensaje completo (sin '\\n') como texto, o None si aú no hay"""
        index = self._buffer.find(b"\n")
        if index < 0:
            return None
        line = self._buffer[:index]
        self._buffer = self._buffer[index + 1:]
        return line.decode("utf-8", errors="replace")


class TcpConnection:
    """Conexión TCP al servidor que envía mensajes MMP y recibe respuestas por línea"""

    def __init__(self, host, port, timeout=5.0):
        self._host = host
        self._port = port
        self._timeout = timeout
        self._sock = None
        self._stream = LineStream()

    def connect(self):
        """Resuelve el nombre con getaddrinfo (sin la IP quemada) y abre la conexión TCP"""
        last_error = None
        for family, socktype, proto, _canon, sockaddr in socket.getaddrinfo(
                self._host, self._port, socket.AF_UNSPEC, socket.SOCK_STREAM):
            try:
                self._sock = socket.socket(family, socktype, proto)
                self._sock.settimeout(self._timeout)
                self._sock.connect(sockaddr)
                return
            except OSError as error:
                last_error = error
                if self._sock is not None:
                    self._sock.close()
                    self._sock = None
        raise ConnectionError(
            "no fue posible conectar a {}:{} ({})".format(self._host, self._port, last_error)
        )

    def send_message(self, message):
        """Serializa el mensaje y lo envía con el delimitador TCP '\\n'."""
        data = (serialize(message) + "\n").encode("utf-8")
        self._sock.sendall(data)

    def receive_message(self):
        """Devuelve el siguiente mensaje de texto recibido o None si el servidor cerró la conexión"""
        while True:
            line = self._stream.next_line()
            if line is not None:
                return line
            chunk = self._sock.recv(4096)
            if not chunk:
                return None  # cierre del servidor
            self._stream.feed(chunk)

    def close(self):
        """Cierra la conexión TCP si está abierta"""
        if self._sock is not None:
            self._sock.close()
            self._sock = None


def send_udp_datagram(host, port, message):
    """Envía un único datagrama UDP con un mensaje MMP, salvo STATUS que no espera respuesta"""
    data = serialize(message).encode("utf-8")
    last_error = None
    for family, socktype, proto, _canon, sockaddr in socket.getaddrinfo(
            host, port, socket.AF_UNSPEC, socket.SOCK_DGRAM):
        try:
            with socket.socket(family, socktype, proto) as sock:
                sock.sendto(data, sockaddr)
                return
        except OSError as error:
            last_error = error
    raise ConnectionError(
        "no fue posible enviar UDP a {}:{} ({})".format(host, port, last_error)
    )
