//==============================================================================
// Copyright (c) 2022 Aleksey [Mystery Keeper] Lyashin (mystkeeper@gmail.com)
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//==============================================================================

#ifndef COMMON_H
#define COMMON_H

#pragma warning(push)
#pragma warning( disable : 26812 )
#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>
#pragma warning(pop)

#ifdef _WIN32
	#include <Windows.h>
	using FSChar = wchar_t;
#else
	#include <dlfcn.h>
	using FSChar = char;
	using HMODULE = void*;
#endif

#define UNUSED(x) (void)(x)

//==============================================================================

inline HMODULE loadLibrary(const FSChar* a_name)
{
#ifdef _WIN32
	return LoadLibraryW(a_name);
#else
	return dlopen(a_name, RTLD_LAZY);
#endif
}

//==============================================================================

inline void freeLibrary(HMODULE a_lib)
{
#ifdef _WIN32
	return (void)FreeLibrary(a_lib);
#else
	return (void)dlclose(a_lib);
#endif
}

//==============================================================================

inline void * getFunctionAddress(HMODULE a_lib, const char* a_functionName)
{
#ifdef _WIN32
	return GetProcAddress(a_lib, a_functionName);
#else
	return dlsym(a_lib, a_functionName);
#endif
}

//==============================================================================

template<class T, class T2, class T3>
T clamp(const T & a_value, const T2 & a_minBound, const T3 & a_maxBound)
{
	if(a_value < a_minBound)
		return a_minBound;
	else if(a_value > a_maxBound)
		return a_maxBound;
	return a_value;
}

//==============================================================================

#endif
