CC      = gcc
CSTD    ?= -std=c11
CFLAGS  ?= $(CSTD) -Wall -Wextra -O2
CPPFLAGS ?= -Isrc -Ithird_party/cJSON
# pthread: el registro y el logger usan mutex, el servidor crea un hilo por cliente TCP
PTHREAD := -pthread

BUILD_DIR := build

# Comandos de shell según el OS
ifeq ($(OS),Windows_NT)
EXE      := .exe
MKDIR    = if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
RM_BUILD = if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
else
EXE      :=
MKDIR    = mkdir -p $(BUILD_DIR)
RM_BUILD = rm -rf $(BUILD_DIR)
endif

# Librería JSON incorporada
CJSON_SRC := third_party/cJSON/cJSON.c

# Módulos propios y sus pruebas portables (solo para desarrollo)
PROTOCOL_SRC   := src/protocol.c
MSGSTREAM_SRC  := src/msgstream.c
HANDLERS_SRC   := src/mmp_handlers.c
TEST_PROTO_SRC := tests/test_protocol.c
TEST_STREAM_SRC := tests/test_msgstream.c
TEST_PIPE_SRC  := tests/test_recv_pipeline.c
TEST_FLOWS_SRC := tests/test_flows.c
TEST_CONC_SRC  := tests/test_concurrency.c

# Servidor y su capa de transporte (incluye el framing TCP y los flujos MMP)
SERVER_SRC := src/server.c src/net.c src/log.c $(MSGSTREAM_SRC) $(HANDLERS_SRC) $(PROTOCOL_SRC) $(CJSON_SRC)

TEST_PROTO_BIN  := $(BUILD_DIR)/test_protocol$(EXE)
TEST_STREAM_BIN := $(BUILD_DIR)/test_msgstream$(EXE)
TEST_PIPE_BIN   := $(BUILD_DIR)/test_recv_pipeline$(EXE)
TEST_FLOWS_BIN  := $(BUILD_DIR)/test_flows$(EXE)
TEST_CONC_BIN   := $(BUILD_DIR)/test_concurrency$(EXE)
SERVER_BIN      := $(BUILD_DIR)/mmp_server$(EXE)

# Prefijo de ejecución del binario según el OS
ifeq ($(OS),Windows_NT)
RUN_PREFIX :=
else
RUN_PREFIX := ./
endif

.PHONY: all test server clean

all: test

# Compila y ejecuta las pruebas portables (protocolo, framing TCP, integración del flujo y flujos MMP)
test: $(TEST_PROTO_BIN) $(TEST_STREAM_BIN) $(TEST_PIPE_BIN) $(TEST_FLOWS_BIN) $(TEST_CONC_BIN)
	$(RUN_PREFIX)$(TEST_PROTO_BIN)
	$(RUN_PREFIX)$(TEST_STREAM_BIN)
	$(RUN_PREFIX)$(TEST_PIPE_BIN)
	$(RUN_PREFIX)$(TEST_FLOWS_BIN)
	$(RUN_PREFIX)$(TEST_CONC_BIN)

$(TEST_PROTO_BIN): $(TEST_PROTO_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_PROTO_SRC) $(PROTOCOL_SRC) $(CJSON_SRC)

$(TEST_STREAM_BIN): $(TEST_STREAM_SRC) $(MSGSTREAM_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_STREAM_SRC) $(MSGSTREAM_SRC)

$(TEST_PIPE_BIN): $(TEST_PIPE_SRC) $(MSGSTREAM_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_PIPE_SRC) $(MSGSTREAM_SRC) $(PROTOCOL_SRC) $(CJSON_SRC)

# test_flows y test_concurrency enlazan el registro con mutex, por eso usan -pthread
$(TEST_FLOWS_BIN): $(TEST_FLOWS_SRC) $(HANDLERS_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_FLOWS_SRC) $(HANDLERS_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) $(PTHREAD)

$(TEST_CONC_BIN): $(TEST_CONC_SRC) $(HANDLERS_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(TEST_CONC_SRC) $(HANDLERS_SRC) $(PROTOCOL_SRC) $(CJSON_SRC) $(PTHREAD)

# Validación del servidor en función del OS
ifeq ($(OS),Windows_NT)
server:
	@echo El servidor MMP es codigo objetivo Linux/POSIX y no compila con MinGW.
	@echo Compilelo en Linux real con: make server
	@exit 1
else
server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200112L $(CPPFLAGS) -o $@ $(SERVER_SRC) $(PTHREAD)
endif

$(BUILD_DIR):
	$(MKDIR)

clean:
	$(RM_BUILD)
