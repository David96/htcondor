/***************************************************************
 *
 * Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************/

#ifndef _STRING_FUNCS_H_
#define _STRING_FUNCS_H_

#include "condor_common.h"
#include "condor_header_features.h"

BEGIN_C_DECLS

#ifndef HAVE_STRCASESTR
  char *strcasestr( const char *string, const char *pattern );
#endif

/* Convert a string to it's "upper case first" match */
char *
getUcFirst( const char *orig );

/* Convert a string to it's "upper case first" match, converting '_+([a-z])' to
   strings to toupper($1), the rest to tolower($1).  In other words, it splits
   the string into sequences of '_' separated words, eats the underscores, and
   converts the next character to upper case, and the rest to lower case.
 */
char *
getUcFirstUnderscore( const char *orig );

END_C_DECLS

#endif
