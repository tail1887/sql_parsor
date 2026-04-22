CC ?= cc
BUILD_DIR ?= build
CPPFLAGS ?= -Iinclude
CFLAGS ?= -std=c11
LDFLAGS ?=
LDLIBS ?= -pthread
PORT ?= 8080
WORKERS ?= 4

SQL_CORE_SOURCES = \
	src/sql_processor.c \
	src/sql_result.c \
	src/lexer.c \
	src/parser.c \
	src/ast.c \
	src/csv_storage.c \
	src/executor.c \
	src/week7/bplus_tree.c \
	src/week7/week7_index.c

SQL_TRACE_SOURCES = \
	src/sql_trace.c \
	src/sql_result.c \
	src/lexer.c \
	src/parser.c \
	src/ast.c \
	src/csv_storage.c \
	src/executor.c \
	src/week7/bplus_tree.c \
	src/week7/week7_index.c

API_SERVER_SOURCES = \
	src/api_server.c \
	src/engine_adapter.c \
	src/http_parser.c \
	src/response_builder.c

APP_TARGETS = \
	$(BUILD_DIR)/sql_processor \
	$(BUILD_DIR)/sql_processor_trace \
	$(BUILD_DIR)/sql_api_server

TEST_TARGETS = \
	$(BUILD_DIR)/test_bootstrap \
	$(BUILD_DIR)/test_lexer \
	$(BUILD_DIR)/test_parser_insert \
	$(BUILD_DIR)/test_parser_select \
	$(BUILD_DIR)/test_csv_storage \
	$(BUILD_DIR)/test_executor \
	$(BUILD_DIR)/test_main_integration \
	$(BUILD_DIR)/test_data_integrity \
	$(BUILD_DIR)/test_sql_processor_api \
	$(BUILD_DIR)/test_http_parser \
	$(BUILD_DIR)/test_api_server \
	$(BUILD_DIR)/test_bplus_tree

BENCH_TARGETS = \
	$(BUILD_DIR)/bench_bplus \
	$(BUILD_DIR)/bench_bplus_wide

CHECK_TARGETS = \
	test_bootstrap \
	test_lexer \
	test_parser_insert \
	test_parser_select \
	test_csv_storage \
	test_executor \
	test_main_integration \
	test_data_integrity \
	test_sql_processor_api \
	test_http_parser \
	test_api_server \
	test_bplus_tree

.DEFAULT_GOAL := all

.PHONY: all apps tests benchmarks check clean run-api run-cli

all: apps tests benchmarks

apps: $(APP_TARGETS)

tests: $(TEST_TARGETS)

benchmarks: $(BENCH_TARGETS)

check: tests
	cd $(CURDIR) && \
	for test_bin in $(CHECK_TARGETS); do \
		./$(BUILD_DIR)/$$test_bin || exit $$?; \
	done

clean:
	rm -rf $(BUILD_DIR)

run-api: $(BUILD_DIR)/sql_api_server
	./$(BUILD_DIR)/sql_api_server $(PORT) $(WORKERS)

run-cli: $(BUILD_DIR)/sql_processor
	./$(BUILD_DIR)/sql_processor sample.sql

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/sql_processor: src/main.c $(SQL_CORE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/sql_processor_trace: src/main_trace.c $(SQL_TRACE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/sql_api_server: src/main_api_server.c $(API_SERVER_SOURCES) $(SQL_CORE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_bootstrap: tests/test_bootstrap.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_lexer: tests/test_lexer.c src/lexer.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_parser_insert: tests/test_parser_insert.c src/parser.c src/ast.c src/lexer.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_parser_select: tests/test_parser_select.c src/parser.c src/ast.c src/lexer.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_csv_storage: tests/test_csv_storage.c src/csv_storage.c src/ast.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_executor: tests/test_executor.c src/executor.c src/sql_result.c src/csv_storage.c src/parser.c src/ast.c src/lexer.c src/week7/bplus_tree.c src/week7/week7_index.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_main_integration: tests/test_main_integration.c $(SQL_CORE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_data_integrity: tests/test_data_integrity.c src/executor.c src/sql_result.c src/csv_storage.c src/parser.c src/ast.c src/lexer.c src/week7/bplus_tree.c src/week7/week7_index.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_sql_processor_api: tests/test_sql_processor_api.c $(SQL_CORE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_http_parser: tests/test_http_parser.c src/http_parser.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_api_server: tests/test_api_server.c $(API_SERVER_SOURCES) $(SQL_CORE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_bplus_tree: tests/test_bplus_tree.c src/week7/bplus_tree.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/bench_bplus: src/bench_bplus.c src/week7/bplus_tree.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/bench_bplus_wide: src/bench_bplus.c src/week7/bplus_tree.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBP_MAX_KEYS=31 $^ $(LDFLAGS) $(LDLIBS) -o $@
