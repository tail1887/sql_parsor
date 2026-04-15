/* WEEK7: B+ tree / primary-key index (see docs/weeks/WEEK7/) */
#ifndef BPLUS_TREE_H // BPLUS_TREE_H가 정의되어 있지 않다면 정의한다. BPLUS_TREE_H는 B+ 트리 헤더 파일의 이름이다.
#define BPLUS_TREE_H // BPLUS_TREE_H를 정의한다.

#include <stddef.h> // stddef.h는 표준 라이브러리 헤더 파일로, 표준 데이터 타입과 메모리 관련 함수를 정의한다.
#include <stdint.h> // stdint.h는 표준 라이브러리 헤더 파일로, 표준 정수 타입을 정의한다.

typedef struct BPlusTree BPlusTree; // BPlusTree 구조체 정의

BPlusTree *bplus_create(void); // BPlusTree 생성 함수
void bplus_destroy(BPlusTree *t); // BPlusTree 소멸 함수 왜 삭제가 필요한가? 왜냐하면 메모리 누수를 방지하기 위해서이다.

/* Insert key -> payload (0-based data row index). Duplicate key returns -1. OOM returns -2. */
int bplus_insert(BPlusTree *t, int64_t key, size_t payload); // BPlusTree에 키와 페이로드를 삽입하는 함수

/* 0 if found and *payload set; -1 if not found. */
int bplus_search(const BPlusTree *t, int64_t key, size_t *payload); // BPlusTree에서 키를 검색하는 함수

/* Load from CSV: same key updates payload (last row wins). */
int bplus_insert_or_replace(BPlusTree *t, int64_t key, size_t payload); // BPlusTree에 키와 페이로드를 삽입하는 함수
// 동일 키 재삽입 시 payload(행 번호)만 갱신 — **CSV에 같은 `id`가 여러 행**일 때 “마지막 행”이 인덱스에 남아야 하는 이유. 왜냐하면 마지막 행이 가장 최신 데이터이기 때문이다.

#endif
