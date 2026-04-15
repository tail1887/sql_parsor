/* WEEK7: primary-key index + auto id (see docs/weeks/WEEK7/) */
#ifndef WEEK7_INDEX_H
#define WEEK7_INDEX_H

#include "ast.h"

#include <stddef.h>
#include <stdint.h>

/* 테스트용: 테이블별 인덱스·next_id 초기화 */
void week7_reset(void);

/*
 * CSV 헤더를 읽고 id PK 여부·기존 행으로 B+ 트리를 채운다(멱등).
 * @return 0 성공, -1 실패
 */
int week7_ensure_loaded(const char *table);

/* 첫 컬럼이 id 인 테이블이고 로드 완료되면 1 */
int week7_table_has_id_pk(const char *table);

/*
 * id PK 테이블용 INSERT 값 준비.
 * @return 1 — id PK 아님; 원본 stmt 그대로 append 하면 됨
 * @return 0 — *out_vals / *out_n 사용(호출측 week7_free_prepared), *out_assigned_id 에 부여될 id
 * @return -1 — 오류
 */
int week7_prepare_insert_values(const InsertStmt *stmt, SqlValue **out_vals, size_t *out_n,
                                int64_t *out_assigned_id);

void week7_free_prepared(SqlValue *vals, size_t n);

/* append 성공 후: row_index 는 0-based 데이터 행 인덱스 */
int week7_on_append_success(const char *table, int64_t assigned_id, size_t row_index);

/* SELECT: id 로 row_index 조회. 없으면 -1 */
int week7_lookup_row(const char *table, int64_t id, size_t *out_row_index);

#endif
