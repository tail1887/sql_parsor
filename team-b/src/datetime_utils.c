#include "datetime_utils.h"

#include <stdio.h>
#include <string.h>

static int is_leap_year(int year)
{
    if (year % 400 == 0) {
        return 1;
    }

    if (year % 100 == 0) {
        return 0;
    }

    return year % 4 == 0;
}

static int days_in_month(int year, int month)
{
    static const int month_lengths[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) {
        return 0;
    }

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }

    return month_lengths[month - 1];
}

static int parse_fixed_width_int(
    const char *text,
    size_t start_index,
    size_t length,
    int *out_value)
{
    size_t i;
    int value;

    if (text == NULL || out_value == NULL) {
        return 1;
    }

    value = 0;
    for (i = 0U; i < length; i++) {
        char current;

        current = text[start_index + i];
        if (current < '0' || current > '9') {
            return 1;
        }

        value = (value * 10) + (current - '0');
    }

    *out_value = value;
    return 0;
}

static int datetime_parts_are_valid(const DateTimeParts *parts)
{
    int max_day;

    if (parts == NULL) {
        return 0;
    }

    if (parts->year < 0 || parts->year > 9999) {
        return 0;
    }

    max_day = days_in_month(parts->year, parts->month);
    if (max_day == 0) {
        return 0;
    }

    if (parts->day < 1 || parts->day > max_day) {
        return 0;
    }

    if (parts->hour < 0 || parts->hour > 23) {
        return 0;
    }

    if (parts->minute < 0 || parts->minute > 59) {
        return 0;
    }

    if (parts->second < 0 || parts->second > 59) {
        return 0;
    }

    return 1;
}

static int64_t days_from_civil(int year, int month, int day)
{
    int adjusted_year;
    int era;
    unsigned int year_of_era;
    unsigned int month_index;
    unsigned int day_of_year;
    unsigned int day_of_era;

    /*
     * "년-월-일"을 1970-01-01부터 지난 총 일수로 바꾸는 보조 함수다.
     * 타임존 API에 의존하지 않아 테스트 환경이 달라도 같은 결과를 낸다.
     */
    adjusted_year = year - (month <= 2);
    era = (adjusted_year >= 0) ? (adjusted_year / 400) : ((adjusted_year - 399) / 400);
    year_of_era = (unsigned int)(adjusted_year - (era * 400));
    month_index = (unsigned int)(month + ((month > 2) ? -3 : 9));
    day_of_year = ((153U * month_index) + 2U) / 5U + (unsigned int)day - 1U;
    day_of_era = (year_of_era * 365U) + (year_of_era / 4U) - (year_of_era / 100U) + day_of_year;

    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static void civil_from_days(
    int64_t days_since_epoch,
    int *out_year,
    int *out_month,
    int *out_day)
{
    int64_t shifted_days;
    int64_t era;
    unsigned int day_of_era;
    unsigned int year_of_era;
    int year;
    unsigned int day_of_year;
    unsigned int month_index;
    unsigned int month;
    unsigned int day;

    /*
     * 위의 days_from_civil 반대 방향 계산이다.
     * timestamp를 다시 사람이 읽는 날짜로 출력할 때 사용한다.
     */
    shifted_days = days_since_epoch + 719468LL;
    era = (shifted_days >= 0) ?
        (shifted_days / 146097LL) :
        ((shifted_days - 146096LL) / 146097LL);
    day_of_era = (unsigned int)(shifted_days - (era * 146097LL));
    year_of_era = (day_of_era - (day_of_era / 1460U) + (day_of_era / 36524U) -
        (day_of_era / 146096U)) / 365U;
    year = (int)year_of_era + (int)(era * 400LL);
    day_of_year = day_of_era -
        (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    month_index = (5U * day_of_year + 2U) / 153U;
    day = day_of_year - ((153U * month_index + 2U) / 5U) + 1U;
    month = month_index < 10U ? month_index + 3U : month_index - 9U;
    year += month <= 2U;

    *out_year = year;
    *out_month = (int)month;
    *out_day = (int)day;
}

int parse_datetime_string(const char *text, DateTimeParts *out_parts)
{
    DateTimeParts parts;

    if (text == NULL || out_parts == NULL) {
        return 1;
    }

    if (strlen(text) != DATETIME_TEXT_LENGTH) {
        return 1;
    }

    /* 입력 형식은 정확히 YYYY-MM-DD HH:MM:SS 하나만 허용한다. */
    if (text[4] != '-' ||
        text[7] != '-' ||
        text[10] != ' ' ||
        text[13] != ':' ||
        text[16] != ':') {
        return 1;
    }

    if (parse_fixed_width_int(text, 0U, 4U, &parts.year) != 0 ||
        parse_fixed_width_int(text, 5U, 2U, &parts.month) != 0 ||
        parse_fixed_width_int(text, 8U, 2U, &parts.day) != 0 ||
        parse_fixed_width_int(text, 11U, 2U, &parts.hour) != 0 ||
        parse_fixed_width_int(text, 14U, 2U, &parts.minute) != 0 ||
        parse_fixed_width_int(text, 17U, 2U, &parts.second) != 0) {
        return 1;
    }

    if (!datetime_parts_are_valid(&parts)) {
        return 1;
    }

    *out_parts = parts;
    return 0;
}

int datetime_parts_to_unix_timestamp(
    const DateTimeParts *parts,
    int64_t *out_timestamp)
{
    int64_t days_since_epoch;
    int64_t seconds_in_day;

    if (parts == NULL || out_timestamp == NULL) {
        return 1;
    }

    if (!datetime_parts_are_valid(parts)) {
        return 1;
    }

    /* 날짜 부분과 시각 부분을 따로 계산한 뒤 초 단위 timestamp로 합친다. */
    days_since_epoch = days_from_civil(parts->year, parts->month, parts->day);
    seconds_in_day = (int64_t)parts->hour * 3600LL +
        (int64_t)parts->minute * 60LL +
        (int64_t)parts->second;

    *out_timestamp = days_since_epoch * 86400LL + seconds_in_day;
    return 0;
}

int format_unix_timestamp(
    int64_t timestamp,
    char *buffer,
    size_t buffer_size)
{
    int64_t days_since_epoch;
    int64_t seconds_in_day;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;

    if (buffer == NULL || buffer_size < DATETIME_BUFFER_SIZE) {
        return 1;
    }

    days_since_epoch = timestamp / 86400LL;
    seconds_in_day = timestamp % 86400LL;

    /*
     * 음수 timestamp에서도 시/분/초가 0~86399 범위를 유지하도록
     * 하루 단위와 남은 초를 다시 정규화한다.
     */
    if (seconds_in_day < 0) {
        seconds_in_day += 86400LL;
        days_since_epoch -= 1LL;
    }

    civil_from_days(days_since_epoch, &year, &month, &day);
    if (year < 0 || year > 9999) {
        return 1;
    }

    hour = (int)(seconds_in_day / 3600LL);
    minute = (int)((seconds_in_day % 3600LL) / 60LL);
    second = (int)(seconds_in_day % 60LL);

    if (snprintf(
            buffer,
            buffer_size,
            "%04d-%02d-%02d %02d:%02d:%02d",
            year,
            month,
            day,
            hour,
            minute,
            second) != (int)DATETIME_TEXT_LENGTH) {
        return 1;
    }

    return 0;
}
