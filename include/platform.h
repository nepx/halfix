// Platform-specific defines
// Includes things for alloca, O_BINARY, etc.
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
// This flag is required for MSVCRT to read binary files normally.
#define O_BINARY 0 
#endif

#endif