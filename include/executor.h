#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"
#include "sql_result.h"

#include <stdio.h>

/*
 * INSERT AST를 실행해 CSV에 한 행 append.
 * @return 0 성공, -1 실패
 */
int executor_execute_insert(const InsertStmt *stmt);

/*
 * SELECT AST를 실행해 구조화된 결과를 채운다.
 * out 은 {0} 초기화 상태여야 한다.
 * @return 0 성공, -1 실패
 */
int executor_execute_select_result(const SelectStmt *stmt, SqlExecutionResult *out);

/*
 * SELECT 결과를 CLI 계약의 TSV(stdout 형식)로 출력한다.
 * @return 0 성공, -1 실패
 */
int executor_render_select_tsv(const SqlExecutionResult *result, FILE *out);

/*
 * SELECT AST를 실행해 결과를 TSV(stdout 형식)로 출력한다.
 * @return 0 성공, -1 실패
 */
int executor_execute_select(const SelectStmt *stmt, FILE *out);

#endif
