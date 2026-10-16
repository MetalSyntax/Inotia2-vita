#pragma once

void log_open(const char *path);
void l_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void l_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal_error(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

#define l_warn(...) l_info(__VA_ARGS__)

#ifdef DEBUG_LOADER
#define l_debug(...) l_info(__VA_ARGS__)
#else
#define l_debug(...) ((void)0)
#endif
