#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"

#include <stdio.h>

/*
 * INSERT AST를 실행해 CSV에 한 행 append.
 * @return 0 성공, -1 실패
 */
int executor_execute_insert(const InsertStmt *stmt);

/*
 * SELECT AST를 실행해 결과를 TSV(stdout 형식)로 출력.
 * @return 0 성공, -1 실패
 */
int executor_execute_select(const SelectStmt *stmt, FILE *out);

#endif
