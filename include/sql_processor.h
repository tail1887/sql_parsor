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

#endif
