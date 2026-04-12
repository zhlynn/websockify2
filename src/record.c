/* SPDX-License-Identifier: MIT */
#include "record.h"

int ws_record_open(ws_record_t *rec, const char *path) {
    memset(rec, 0, sizeof(*rec));
    rec->fp = fopen(path, "w");
    if (!rec->fp) return -1;
    rec->start_time = ws_time_ms();

    /* Write header as JSON array start */
    fprintf(rec->fp, "[\n");
    fflush(rec->fp);
    return 0;
}

void ws_record_close(ws_record_t *rec) {
    if (rec->fp) {
        fprintf(rec->fp, "\n]\n");
        fclose(rec->fp);
        rec->fp = NULL;
    }
}

int ws_record_frame(ws_record_t *rec, char direction, const void *data, int len) {
    (void)data;
    if (!rec->fp) return -1;

    uint64_t elapsed = ws_time_ms() - rec->start_time;

    /* Write as JSON: {"t": ms, "d": "<|>", "len": N} */
    static int first = 1;
    if (!first) fprintf(rec->fp, ",\n");
    first = 0;

    fprintf(rec->fp, "{\"t\":%llu,\"d\":\"%c\",\"len\":%d}",
            (unsigned long long)elapsed, direction, len);
    fflush(rec->fp);
    return 0;
}
