# Monitoring Message Protocol

Monitoring Message Protocol (MMP) es un proyecto académico de monitoreo distribuido. Permite que nodos reporten métricas y eventos a un servidor central, mientras clientes administrativos consultan el estado y el histórico registrado.

La especificación técnica del protocolo está disponible en la [wiki del proyecto](https://github.com/Sr4v33/Monitoring_Message_Protocol/wiki).

## Arquitectura

- **Servidor central:** escrito en C con sockets Berkeley/POSIX. Atiende conexiones TCP mediante un hilo por cliente y protege el estado compartido con mutexes.
- **Nodo:** escrito en Python. Se registra, envía estados periódicos y reporta eventos.
- **Cliente administrativo:** escrito en Python. Consulta el histórico almacenado por el servidor.

No existe comunicación directa entre nodos y clientes administrativos.

## Protocolo

Los mensajes usan JSON codificado en UTF-8.

| Mensaje | Transporte | Función |
|---|---|---|
| `REGISTER` / `REGISTER_ACK` | TCP | Registro de nodos |
| `STATUS` | UDP | Actualización periódica de métricas |
| `EVENT` / `EVENT_ACK` | TCP | Reporte y confirmación de eventos |
| `QUERY` / `RESPONSE` | TCP | Consulta del histórico |
| `ERROR` | TCP | Notificación de errores del protocolo |

En TCP, cada mensaje termina con `\n`. En UDP, cada datagrama contiene un mensaje completo.

## Requisitos

- Linux/POSIX para compilar y ejecutar el servidor.
- GCC y GNU Make.
- Python 3 para el nodo y el cliente administrativo.

No se requieren frameworks, bases de datos ni paquetes externos de Python. La dependencia cJSON está incluida en `third_party/cJSON`.

## Compilación

En Linux, desde la raíz del repositorio:

```sh
make server
```

El binario se genera en `build/mmp_server`.

## Ejecución

Iniciar el servidor:

```sh
./build/mmp_server <puerto> <archivo_log> [host]
```

Ejemplo:

```sh
./build/mmp_server 9300 build/mmp.log
```

Ejecutar un nodo:

```sh
python3 client/mmp_node.py --node-id NODE-001 --host localhost --tcp-port 9300
```

El puerto UDP utiliza el mismo valor del TCP salvo que se indique `--udp-port`.

Consultar el histórico:

```sh
python3 client/mmp_client.py --host localhost --tcp-port 9300 --node-id NODE-001 --limit 5
```

## Estructura

```text
src/          Servidor, protocolo, transporte y handlers en C
client/       Nodo y cliente administrativo en Python
third_party/  Dependencia cJSON
Makefile      Compilación del servidor
```