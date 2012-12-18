/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _CPR_STDLIB_H_
#define _CPR_STDLIB_H_

#include <stdlib.h>
#include <string.h>

#ifdef SIP_OS_WINDOWS
#include <crtdbg.h>
#include <errno.h>
#endif

//#include "mozilla/mozalloc.h"

#define cpr_malloc(a) malloc(a)
#define cpr_calloc(a, b) calloc(a, b)
#define cpr_realloc(a, b) realloc(a, b)
#define cpr_free(a) free(a)

#endif


