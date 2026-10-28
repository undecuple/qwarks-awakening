/* Ratchet & Clank port (from Toro a46f947 port/tlp/rc_log.h): a stdio stand-in for KZ3's file logger (KZ3 78bf574 port/kz3/kz3_log.h), so the boot code
 * copied from port/kz3/main.cpp keeps its log calls. ENHANCEMENT candidate: bring the real logger over when the port
 * needs log files and crash files (patches 2030/2032 call into it only with KZ3_LOG_REDIRECT_STDIO, not defined here). */
#pragma once
#include <stdio.h>
#define RC_LOGE(tag, ...) (fprintf(stderr, "[E][%s] ", tag), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define RC_LOGI(tag, ...) (fprintf(stderr, "[I][%s] ", tag), fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
static inline void rc_log_shutdown(void) { fflush(stdout); fflush(stderr); }
static inline void rc_log_set_thread_label(const char* label) { (void)label; }
