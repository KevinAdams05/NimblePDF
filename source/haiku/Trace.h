#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>

#if defined(TRACE_LEVEL) && TRACE_LEVEL > 0
#define TRACE(level, args) \
	{                      \
		printf args;       \
		fflush(stdout);    \
	}
#else
#define TRACE(level, args)
#endif

#endif
