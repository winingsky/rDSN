/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     What is this file about?
 *
 * Revision history:
 *     xxxx-xx-xx, author, first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

# pragma once

#if defined(_WIN32)

# include <Windows.h>

__pragma(warning(disable:4127))

# define __thread __declspec(thread)
# define __selectany __declspec(selectany) extern 

# elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)

# include <unistd.h>

# define __selectany __attribute__((weak)) extern 

# ifndef O_BINARY
# define O_BINARY 0
#endif

#else

#error "unsupported platform"
#endif

// stl headers
# include <string>
# include <memory>
# include <map>
# include <unordered_map>
# include <set>
# include <unordered_set>
# include <vector>
# include <list>
# include <algorithm>

# define __STDC_FORMAT_MACROS
// common c headers
# include <cassert>
# include <cstring>
# include <cstdlib>
# include <fcntl.h> // for file open flags
# include <cstdio>
# include <climits>
# include <cerrno>
# include <cstdint>
# include <inttypes.h>

// common utilities
# include <atomic>

// common macros and data structures
namespace dsn 
{
    enum provider_type
    {
        PROVIDER_TYPE_MAIN = 0,
        PROVIDER_TYPE_ASPECT = 1
    };
}

# ifndef FIELD_OFFSET
# define FIELD_OFFSET(s, field)  (((size_t)&((s *)(10))->field) - 10)
# endif

# ifndef CONTAINING_RECORD 
# define CONTAINING_RECORD(address, type, field) \
    ((type *)((char*)(address)-FIELD_OFFSET(type, field)))
# endif

# ifndef MAX_COMPUTERNAME_LENGTH
# define MAX_COMPUTERNAME_LENGTH 32
# endif

# ifndef ARRAYSIZE
# define ARRAYSIZE(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
# endif

# ifndef snprintf_p
# if defined(_WIN32)
# define snprintf_p sprintf_s
# else
# define snprintf_p std::snprintf
# endif
# endif

# ifdef _WIN32

// TODO(qinzuoyan) port to big-endian machine

# ifndef be16toh
# define be16toh(x) ( (x)>>8 | ( (x) &255 )<<8 )
# endif

# ifndef be32toh
# define be32toh(x) ( (be16toh((x)>>16)&65535) | (be16toh((x)&65535)<<16) )
# endif

# ifndef be64toh
# define be64toh(x) ( (be32toh((x)>>32)&0xffffffff) | ( be32toh( (x)&0xffffffff ) << 32 ) )
# endif

# endif