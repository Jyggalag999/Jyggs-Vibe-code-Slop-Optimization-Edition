#pragma once
// Forced include for the menu preview (replaces the game's Framework.h): only what the menu sources assume.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>

typedef unsigned char	UInt8;
typedef unsigned int	UInt32;

namespace Logger {
	void Log(const char* fmt, ...);	// prints to the console (main.cpp)
}
