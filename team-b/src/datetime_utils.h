#ifndef DATETIME_UTILS_H
#define DATETIME_UTILS_H

#include <stddef.h>
#include <stdint.h>

#define DATETIME_TEXT_LENGTH 19U
#define DATETIME_BUFFER_SIZE 20U

/*
 * datetime 문자열은 입력/출력 형식이 프로젝트 전체에서 고정이다.
 * 따라서 필요한 필드만 담는 가장 작은 구조체를 사용한다.
 */
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} DateTimeParts;

int parse_datetime_string(const char *text, DateTimeParts *out_parts);
int datetime_parts_to_unix_timestamp(
    const DateTimeParts *parts,
    int64_t *out_timestamp);
int format_unix_timestamp(
    int64_t timestamp,
    char *buffer,
    size_t buffer_size);

#endif
