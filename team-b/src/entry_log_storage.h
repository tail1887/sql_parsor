#ifndef ENTRY_LOG_STORAGE_H
#define ENTRY_LOG_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#define ENTRY_LOG_RECORD_SIZE 12U

typedef struct {
    int64_t entered_at;
    int32_t id;
} EntryLogRecord;

typedef struct {
    EntryLogRecord *items;
    size_t count;
} EntryLogRecordList;

typedef enum {
    ENTRY_LOG_STORAGE_OK,
    ENTRY_LOG_STORAGE_IO_ERROR,
    ENTRY_LOG_STORAGE_INVALID_FILE
} EntryLogStorageStatus;

EntryLogStorageStatus ensure_entry_log_bin_exists(const char *bin_path);
EntryLogStorageStatus append_entry_log_record(
    const char *bin_path,
    const EntryLogRecord *record);
EntryLogStorageStatus read_entry_log_records_by_id(
    const char *bin_path,
    int id,
    EntryLogRecordList *out_list);
void free_entry_log_record_list(EntryLogRecordList *list);

#endif
