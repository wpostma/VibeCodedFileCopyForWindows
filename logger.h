#pragma once
#include <wchar.h>   /* wchar_t for C callers */

#ifdef __cplusplus
extern "C" {
#endif

// Write a formatted string to OutputDebugString (visible in VS Output window / DebugView).
void Log(const wchar_t* fmt, ...);

#ifdef __cplusplus
}
#endif
