#include "entry_log_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void initialize_entry_log_list(EntryLogRecordList *list)
{
    list->items = NULL;
    list->count = 0U;
}

static int ensure_parent_directory_exists(const char *file_path)
{
    const char *last_slash;
    size_t directory_length;
    char *directory_path;
    int mkdir_result;

    last_slash = strrchr(file_path, '/');
    if (last_slash == NULL) {
        return 0;
    }

    directory_length = (size_t)(last_slash - file_path);
    if (directory_length == 0U) {
        return 0;
    }

    directory_path = (char *)malloc(directory_length + 1U);
    if (directory_path == NULL) {
        return 1;
    }

    memcpy(directory_path, file_path, directory_length);
    directory_path[directory_length] = '\0';

    mkdir_result = mkdir(directory_path, 0777);
    free(directory_path);

    if (mkdir_result == 0 || errno == EEXIST) {
        return 0;
    }

    return 1;
}

static int ensure_list_capacity(EntryLogRecordList *list, size_t *capacity)
{
    EntryLogRecord *new_items;
    size_t new_capacity;

    if (list->count < *capacity) {
        return 0;
    }

    new_capacity = (*capacity == 0U) ? 8U : (*capacity * 2U);
    new_items = (EntryLogRecord *)realloc(list->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return 1;
    }

    list->items = new_items;
    *capacity = new_capacity;
    return 0;
}

static int write_int64_field(FILE *file, int64_t value)
{
    /* Step 5 제약에 맞춰 struct 전체가 아니라 필드 하나씩 직접 쓴다. */
    return fwrite(&value, sizeof(value), 1U, file) == 1U ? 0 : 1;
}

static int write_int32_field(FILE *file, int32_t value)
{
    return fwrite(&value, sizeof(value), 1U, file) == 1U ? 0 : 1;
}

static int read_exact_bytes(
    FILE *file,
    void *buffer,
    size_t byte_count,
    int *out_reached_eof)
{
    size_t bytes_read;

    /*
     * binary 포맷은 고정 길이 레코드라서
     * "지금 필요한 바이트 수를 정확히 다 읽었는가"가 중요하다.
     */
    bytes_read = fread(buffer, 1U, byte_count, file);
    if (bytes_read == byte_count) {
        *out_reached_eof = 0;
        return 0;
    }

    if (bytes_read == 0U && feof(file)) {
        *out_reached_eof = 1;
        return 0;
    }

    return 1;
}

void free_entry_log_record_list(EntryLogRecordList *list)
{
    if (list == NULL) {
        return;
    }

    free(list->items);
    list->items = NULL;
    list->count = 0U;
}

EntryLogStorageStatus ensure_entry_log_bin_exists(const char *bin_path)
{
    FILE *file;

    if (bin_path == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    file = fopen(bin_path, "rb");
    if (file != NULL) {
        fclose(file);
        return ENTRY_LOG_STORAGE_OK;
    }

    if (ensure_parent_directory_exists(bin_path) != 0) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    file = fopen(bin_path, "wb");
    if (file == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    fclose(file);
    return ENTRY_LOG_STORAGE_OK;
}

EntryLogStorageStatus append_entry_log_record(
    const char *bin_path,
    const EntryLogRecord *record)
{
    FILE *file;

    if (bin_path == NULL || record == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    if (ensure_entry_log_bin_exists(bin_path) != ENTRY_LOG_STORAGE_OK) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    file = fopen(bin_path, "ab");
    if (file == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    /* 레코드 순서는 항상 entered_at 8바이트 다음 id 4바이트다. */
    if (write_int64_field(file, record->entered_at) != 0 ||
        write_int32_field(file, record->id) != 0) {
        fclose(file);
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    fclose(file);
    return ENTRY_LOG_STORAGE_OK;
}

EntryLogStorageStatus read_entry_log_records_by_id(
    const char *bin_path,
    int id,
    EntryLogRecordList *out_list)
{
    FILE *file;
    EntryLogRecordList list;
    size_t capacity;

    if (bin_path == NULL || out_list == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    *out_list = (EntryLogRecordList){NULL, 0U};

    if (ensure_entry_log_bin_exists(bin_path) != ENTRY_LOG_STORAGE_OK) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    file = fopen(bin_path, "rb");
    if (file == NULL) {
        return ENTRY_LOG_STORAGE_IO_ERROR;
    }

    initialize_entry_log_list(&list);
    capacity = 0U;

    while (1) {
        EntryLogRecord record;
        int reached_eof;

        /*
         * 한 레코드를 12바이트 단위로 복원한다.
         * 앞 8바이트를 읽은 뒤 바로 뒤의 4바이트까지 있어야 정상 파일이다.
         */
        if (read_exact_bytes(file, &record.entered_at, sizeof(record.entered_at), &reached_eof) != 0) {
            fclose(file);
            free_entry_log_record_list(&list);
            return ENTRY_LOG_STORAGE_INVALID_FILE;
        }

        if (reached_eof) {
            break;
        }

        if (read_exact_bytes(file, &record.id, sizeof(record.id), &reached_eof) != 0 ||
            reached_eof) {
            fclose(file);
            free_entry_log_record_list(&list);
            return ENTRY_LOG_STORAGE_INVALID_FILE;
        }

        if (record.id != id) {
            continue;
        }

        /* SELECT ... WHERE id = <int> 에 맞는 row만 결과 배열에 담는다. */
        if (ensure_list_capacity(&list, &capacity) != 0) {
            fclose(file);
            free_entry_log_record_list(&list);
            return ENTRY_LOG_STORAGE_IO_ERROR;
        }

        list.items[list.count] = record;
        list.count += 1U;
    }

    fclose(file);
    *out_list = list;
    return ENTRY_LOG_STORAGE_OK;
}
