#pragma once
#include <stddef.h>
#include <stdint.h>

void darwin_init(void);
const char *darwin_path(const char *in, char *out, size_t outlen);

void hle_cs_install(void);
uint64_t hle_run_timers(void);
