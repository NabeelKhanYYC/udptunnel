/*
 * Copyright (C) 2018 Marco d'Itri
 *
 * Inspired by log.c from the cowdancer package by James Clarke.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>

#include "log.h"
#include "../utils/utils.h"

static log_level filter_level = log_info;

static void format_rfc3339_timestamp(char *buffer, size_t buffer_size)
{
    struct timespec ts;
    struct tm *tm_info;
    int milliseconds;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        snprintf(buffer, buffer_size, "1970-01-01T00:00:00.000+00:00");
        return;
    }

    tm_info = localtime(&ts.tv_sec);
    if (tm_info == NULL) {
        snprintf(buffer, buffer_size, "1970-01-01T00:00:00.000+00:00");
        return;
    }

    milliseconds = ts.tv_nsec / 1000000;

    strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", tm_info);
    
    size_t len = strlen(buffer);
    snprintf(buffer + len, buffer_size - len, ".%03d", milliseconds);
    
    len = strlen(buffer);
    strftime(buffer + len, buffer_size - len, "%z", tm_info);
    
    len = strlen(buffer);
    if (len >= 5 && buffer[len-2] != ':') {
        char tz_hour = buffer[len-4];
        char tz_min1 = buffer[len-3];
        char tz_min2 = buffer[len-2];
        char tz_last = buffer[len-1];
        buffer[len-2] = ':';
        buffer[len-1] = tz_min1;
        buffer[len] = tz_min2;
        buffer[len+1] = tz_last;
        buffer[len+2] = '\0';
    }
}

/*
 * Return the appropriate file handle (stdout vs. stderr) for the log level.
 */
static FILE *file_for_level(log_level level)
{
    if (level & log_stderr || filter_level & log_stderr)
	    return stderr;

    if ((level & LOG_LEVEL_MASK) > log_warning)
	    return stdout;
    else
	    return stderr;
}

static void log_doit(log_level level, const char *format, va_list args)
{
    static int syslog_initialized;

    if ((level & LOG_LEVEL_MASK) > (filter_level & LOG_LEVEL_MASK))
	    return;

    if (level & log_syslog || filter_level & log_syslog) {
        if (!syslog_initialized) {
            openlog(NULL, LOG_PID, LOG_DAEMON);
            syslog_initialized = 1;
        }

        if (level & log_strerror) {
            int len = strlen(format);
            char *format2;

            format2 = NOFAIL(malloc(len + 4 + 1));
            strcpy(format2, format);
            strcpy(format2 + len, ": %m");
            vsyslog(level & LOG_LEVEL_MASK, format2, args);
            free(format2);
        } else {
            vsyslog(level & LOG_LEVEL_MASK, format, args);
        }
        return;
    }

    char timestamp[32];
    format_rfc3339_timestamp(timestamp, sizeof(timestamp));
    fprintf(file_for_level(level), "[%s] ", timestamp);
    
    vfprintf(file_for_level(level), format, args);
    if (level & log_strerror)
	    fprintf(file_for_level(level), ": %s", strerror(errno));
    fprintf(file_for_level(level), "\n");
}

log_level log_get_filter_level(void)
{
    return filter_level;
}

void log_set_options(log_level filter_level_new)
{
    filter_level = filter_level_new;
}

void log_printf(log_level level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_doit(level, format, args);
    va_end(args);
}

void log_printf_exit(int status, log_level level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_doit(level, format, args);
    va_end(args);

    exit(status);
}

void log_printf_err(log_level level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_doit(level | log_strerror, format, args);
    va_end(args);
}

void log_printf_err_exit(int status, log_level level, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_doit(level | log_strerror, format, args);
    va_end(args);

    exit(status);
}

