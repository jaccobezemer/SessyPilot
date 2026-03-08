#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include <time.h>

#define SESSION_MAX 30

typedef struct {
    time_t start;
    time_t stop;   // 0 = still active (charging in progress)
} ev_session_t;

void session_log_init(void);
void session_log_start(void);
void session_log_stop(void);

/* Returns up to `max` sessions, newest first. Returns count. */
int  session_log_get(ev_session_t *buf, int max);

/* Remove all sessions from memory and disk. */
void session_log_clear(void);

#endif
