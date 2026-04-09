#ifndef STATEMENT_SPLITTER_H
#define STATEMENT_SPLITTER_H

#include <stddef.h>

typedef struct {
    char **items;
    size_t count;
} StatementList;

int split_sql_statements(const char *sql_text, StatementList *out_list);
void free_statement_list(StatementList *list);

#endif
