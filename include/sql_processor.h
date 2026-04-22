#ifndef SQL_PROCESSOR_H
#define SQL_PROCESSOR_H

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
 * SQL 텍스트를 직접 실행한다. (WEEK8 engine bridge용)
 * 반환 코드는 docs/03-api-reference.md 의 종료 코드 계약을 따른다.
 */
int sql_processor_run_text(const char *sql, FILE *out, FILE *err);

/*
 * SQL 스크립트를 실행하면서 단계별 trace(JSON lines)를 기록한다.
 * - trace: 각 단계 이벤트를 기록할 출력 스트림
 */
int sql_processor_run_file_trace(const char *path, FILE *out, FILE *err, FILE *trace);

#endif
