/*
 * NimblePDF: A native Haiku PDF reader.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#ifndef LOGGING_H
#define LOGGING_H


#include <OS.h>
#include <SupportDefs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>


// Project-wide logging helper. Writes to the Haiku syslog
// (viewable via the `syslog` command or the Debugger pane).
//
// Format string is printf-style. A trailing newline is stripped
// (syslog adds line breaks per message).
//
// Use the standard syslog priority constants from <syslog.h>:
//
//     LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR, LOG_CRIT
//
// Example:
//
//     Trace(LOG_INFO,  "loaded %s, %d pages", path, pageCount);
//     Trace(LOG_ERR,   "failed to render page %d: %s", page, strerror(errno));
//     Trace(LOG_DEBUG, "annotation rect: %f %f %f %f", r.x, r.y, r.w, r.h);
//
// The syslog tag is "NimblePDF[t=<thread-id>]".
//
// This header is intentionally a header-only static inline so any
// translation unit can `#include "Logging.h"` and call Trace without
// any link-time setup.
static inline void Trace(int level, const char* format, ...)
{
	char buffer[512];

	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	// Strip a single trailing newline — syslog adds its own line break,
	// so an embedded \n would produce a blank line in the log.
	size_t length = strlen(buffer);
	if (length > 0 && buffer[length - 1] == '\n')
		buffer[length - 1] = '\0';

	syslog(level, "NimblePDF[t=%" B_PRId32 "]: %s", find_thread(NULL), buffer);
}


#endif // LOGGING_H
