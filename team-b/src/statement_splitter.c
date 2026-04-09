#include "statement_splitter.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*
 * Step 1의 statement splitter는 SQL 문법을 해석하지 않는다.
 * 오직 ';'를 문장 끝으로 보고, 입력 문자열을 "문장 문자열 여러 개"로 잘라내는 역할만 한다.
 */

static void initialize_statement_list(StatementList *list)
{
    /*
     * StatementList 안의 items는 "char * 배열"이다.
     * 즉 items[0], items[1], ... 에 문장 문자열 주소가 들어가게 된다.
     * 시작할 때는 아직 아무 문장도 없으므로 NULL / 0으로 초기화한다.
     */
    list->items = NULL;
    list->count = 0;
}

static int append_statement(
    StatementList *list,
    size_t *capacity,
    const char *start,
    size_t length)
{
    char **new_items;
    char *statement;
    size_t new_capacity;

    /*
     * list->items는 "문장 포인터들을 담는 동적 배열"이다.
     * capacity는 현재 배열이 몇 칸까지 저장 가능한지를 뜻한다.
     * count == capacity면 칸이 다 찼다는 뜻이므로 배열을 더 크게 늘린다.
     */
    if (list->count == *capacity) {
        /*
         * 처음에는 4칸으로 시작하고,
         * 이후에는 2배씩 늘려서 realloc 호출 횟수를 줄인다.
         */
        new_capacity = (*capacity == 0U) ? 4U : (*capacity * 2U);

        /*
         * realloc은 기존 배열 크기를 바꾸는 함수다.
         * 성공하면 더 큰 배열 주소를 돌려주고,
         * 실패하면 NULL을 돌려주며 기존 배열은 그대로 남는다.
         */
        new_items = (char **)realloc(list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return 1;
        }

        list->items = new_items;
        *capacity = new_capacity;
    }

    /*
     * 이번에는 "문장 내용 자체"를 저장할 새 메모리를 만든다.
     * +1은 마지막에 문자열 끝 문자 '\0'를 넣기 위한 칸이다.
     */
    statement = (char *)malloc(length + 1U);
    if (statement == NULL) {
        return 1;
    }

    /*
     * memcpy는 start 위치부터 length 바이트를 그대로 복사한다.
     * 여기서는 예를 들어 "SELECT * FROM STUDENT_CSV;" 같은 문장 한 개를 떼어내는 작업이다.
     */
    memcpy(statement, start, length);

    /* 복사한 바이트 뒤에 '\0'를 붙여서 C 문자열로 만든다. */
    statement[length] = '\0';

    /* 새로 만든 문장 문자열 주소를 리스트의 다음 칸에 저장한다. */
    list->items[list->count] = statement;
    list->count += 1U;
    return 0;
}

void free_statement_list(StatementList *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0; i < list->count; i++) {
        /* 문장 하나마다 별도 malloc을 했으므로 하나씩 free해야 한다. */
        free(list->items[i]);
    }

    /* 문장 포인터들을 담고 있던 배열 자체도 마지막에 해제한다. */
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int split_sql_statements(const char *sql_text, StatementList *out_list)
{
    StatementList list;
    size_t capacity;
    size_t statement_start;
    size_t i;

    /*
     * sql_text:
     *   SQL 파일 전체가 들어 있는 하나의 긴 문자열
     *
     * out_list:
     *   잘라낸 문장 목록을 함수 밖으로 돌려주기 위한 출력 포인터
     */
    if (sql_text == NULL || out_list == NULL) {
        return 1;
    }

    /*
     * list는 이 함수 안에서 먼저 결과를 모아 두는 임시 저장소다.
     * 함수가 끝날 때 성공하면 out_list에 넘겨준다.
     */
    initialize_statement_list(&list);
    capacity = 0U;

    /*
     * statement_start:
     *   "현재 문장이 시작한다고 보는 위치"
     *
     * i:
     *   지금 검사 중인 문자 위치
     *
     * 처음에는 문자열 맨 앞부터 시작하므로 둘 다 0이다.
     */
    statement_start = 0U;
    i = 0U;

    /*
     * 문자열을 앞에서부터 한 글자씩 읽는다.
     * 이 루프는 두 경우에 끝난다.
     * 1. '\0'를 만나서 정상 종료할 때
     * 2. 메모리 문제나 문장 형식 문제로 실패할 때
     */
    while (1) {
        if (sql_text[i] == ';') {
            size_t trimmed_start = statement_start;

            /*
             * trimmed_start는 실제 문장 내용이 시작하는 위치다.
             * 예를 들어 현재 문장이 "\n   SELECT ... ;"처럼 시작하면
             * 앞쪽 공백과 줄바꿈은 건너뛰고 SELECT부터 시작하도록 만든다.
             */
            while (trimmed_start < i &&
                   isspace((unsigned char)sql_text[trimmed_start])) {
                trimmed_start += 1U;
            }

            if (trimmed_start < i) {
                /*
                 * trimmed_start < i 라는 말은
                 * 공백만 있는 빈 조각이 아니라 실제 문장이 있었다는 뜻이다.
                 *
                 * 길이를 (i - trimmed_start) + 1 로 계산하는 이유는
                 * 현재 위치 i의 세미콜론까지 문장에 포함시키기 위해서다.
                 */
                if (append_statement(
                        &list,
                        &capacity,
                        sql_text + trimmed_start,
                        (i - trimmed_start) + 1U) != 0) {
                    free_statement_list(&list);
                    return 1;
                }
            }

            /*
             * 지금 세미콜론까지 문장 하나를 처리했으므로,
             * 다음 문장은 세미콜론 바로 다음 글자부터 시작한다고 본다.
             */
            statement_start = i + 1U;
        } else if (sql_text[i] == '\0') {
            size_t tail = statement_start;

            /*
             * 문자열 끝까지 왔다는 뜻이다.
             * 이제 마지막 세미콜론 뒤에 남은 내용이 있는지 검사한다.
             *
             * 예:
             *   "SELECT * FROM STUDENT_CSV;   "  -> 정상
             *   "SELECT * FROM STUDENT_CSV"     -> 오류
             */
            while (tail < i && isspace((unsigned char)sql_text[tail])) {
                tail += 1U;
            }

            /*
             * tail != i 이면 공백이 아닌 글자가 남아 있다는 뜻이다.
             * 즉 마지막 문장이 세미콜론 없이 끝난 경우이므로 실패 처리한다.
             */
            if (tail != i) {
                free_statement_list(&list);
                return 1;
            }

            /*
             * 여기까지 왔으면 모든 문장이 정상적으로 ';'로 끝난 것이다.
             * 이제 모아 둔 결과 리스트를 호출자에게 넘기고 성공 종료한다.
             */
            *out_list = list;
            return 0;
        }

        /* 현재 문자를 다 봤으니 다음 문자 위치로 한 칸 이동한다. */
        i += 1U;
    }
}
