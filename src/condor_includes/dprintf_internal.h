/***************************************************************
 *
 * Copyright (C) 1990-2012, Condor Team, Computer Sciences Department,
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

// This #define doesn't actually do anything. This value needs to be
// defined before any system header files are included in the source file
// to have any effect.
#include <string>
#include <map>
#if _MSC_VER && (_MSC_VER < 1600)
typedef _Longlong int64_t;
#else
#include <stdint.h>
#endif

struct DebugFileInfo;

typedef void (*DprintfFuncPtr)(int, int, time_t, struct tm*, const char*, DebugFileInfo*);

enum DebugOutput
{
	FILE_OUT,
	STD_OUT,
	STD_ERR,
	OUTPUT_DEBUG_STR
};

/* future
class DebugOutputChoice
{
public:
	unsigned int flags; // one or more of D_xxx flags (but NOT category) values
	unsigned char level[D_CATEGORY_COUNT]; // indexed by D_CATEGORY enum
	DebugOutputChoice(unsigned int val);
	DebugOutputChoice::DebugOutputChoice(unsigned int val)
	{
		flags = val & ~D_CATEGORY_RESERVED_MASK;
		memset(level, 0, sizeof(level));
		unsigned int catflags = val & D_CATEGORY_MASK;
		for (int ix = 0; catflags && ix < sizeof(level); ++ix, catflags/=2)
			level[ix] += (catflags&1);
	}
};
*/
struct dprintf_output_settings;

struct DebugFileInfo
{
	DebugOutput outputTarget;
	FILE *debugFP;
	DebugOutputChoice choice;
	std::string logPath;
	int64_t maxLog;
	int maxLogNum;
	bool want_truncate;
	bool accepts_all;
	bool dont_panic;
	DebugFileInfo() :
			outputTarget(FILE_OUT),
			debugFP(0),
			choice(0),
			maxLog(0),
			maxLogNum(0),
			want_truncate(false),
			accepts_all(false),
			dont_panic(false),
			dprintfFunc(NULL)
			{}
	DebugFileInfo(const DebugFileInfo &dfi) : outputTarget(dfi.outputTarget), debugFP(NULL), choice(dfi.choice),
		logPath(dfi.logPath), maxLog(dfi.maxLog), maxLogNum(dfi.maxLogNum), want_truncate(dfi.want_truncate),
		accepts_all(dfi.accepts_all), dont_panic(dfi.dont_panic), dprintfFunc(dfi.dprintfFunc) {}
	DebugFileInfo(const dprintf_output_settings&);
	~DebugFileInfo();
	bool MatchesCatAndFlags(int cat_and_flags) const;
	DprintfFuncPtr dprintfFunc;
};

struct dprintf_output_settings
{
	DebugOutputChoice choice;
	std::string logPath;
	off_t maxLog;
	int maxLogNum;
	bool want_truncate;
	bool accepts_all;
	unsigned int HeaderOpts;  // temporary, should get folded into choice
	unsigned int VerboseCats; // temporary, should get folded into choice

	dprintf_output_settings() : choice(0), maxLog(0), maxLogNum(0), want_truncate(false), accepts_all(false), HeaderOpts(0), VerboseCats(0) {}
};

void dprintf_set_outputs(const struct dprintf_output_settings *p_info, int c_info);

const char* _format_global_header(int cat_and_flags, int hdr_flags, time_t clock_now, struct tm *tm);
//Global dprint functions meant as fallbacks.
void _dprintf_global_func(int cat_and_flags, int hdr_flags, time_t clock_now, struct tm *tm, const char* message, DebugFileInfo* dbgInfo);

#ifdef WIN32
//Output to dbg string
void dprintf_to_outdbgstr(int cat_and_flags, int hdr_flags, time_t clock_now, struct tm *tm, const char* message, DebugFileInfo* dbgInfo);
#endif