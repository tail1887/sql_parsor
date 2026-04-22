#ifndef SQL_PROCESSOR_H
#define SQL_PROCESSOR_H

#include "sql_result.h"

#include <stdio.h>

/*
 * SQL 스크립트 파일을 실행한다.
 * 반환 코드는 docs/03-api-reference.md 의 종료 코드 계약을 따른다.
 * - 0: 성공
 * - 2: 구문 오류
 * - 3: 실행 오류/입출력 오류
 */
int sql_processor_run_file(const char *path, FILE *out, FILE *err);

/*
 * 단일 SQL 문장을 직접 실행하고 구조화된 결과를 반환한다.
 * out 은 {0} 으로 초기화한 뒤 사용하고, 사용 후 sql_processor_free_result 로 해제한다.
 */
int sql_processor_run_text(const char *sql, SqlExecutionResult *out, FILE *err);

/* sql_processor_run_text 가 채운 메모리를 해제한다. */
void sql_processor_free_result(SqlExecutionResult *out);

/*
 * SQL 스크립트를 실행하면서 단계별 trace(JSON lines)를 기록한다.
 * - trace: 각 단계 이벤트를 기록할 출력 스트림
 */
int sql_processor_run_file_trace(const char *path, FILE *out, FILE *err, FILE *trace);

#endif
