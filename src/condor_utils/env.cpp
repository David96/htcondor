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


#include "condor_common.h"
#include "condor_classad.h"
#include "condor_attributes.h"

#include "env.h"
#include "HashTable.h"
#include "setenv.h"

// Since ';' is the PATH delimiter in Windows, we use a different
// delimiter for V1 environment entries.
static const char unix_env_delim = ';';
static const char windows_env_delim = '|';

#ifndef WIN32
static const char env_delimiter = ';';
#else
static const char env_delimiter = '|';
#endif

static bool is_permitted_delim(char ch) { return strchr("!#$%&*+,-/:;<>?@^`|~\x1F", ch) != nullptr; }

Env::Env()
{
	input_was_v1 = false;
	_envTable = new HashTable<MyString, MyString>
		( &hashFunction );
	ASSERT( _envTable );
}

Env::~Env()
{
	delete _envTable;
}

int
Env::Count() const
{
	return _envTable->getNumElements();
}

void
Env::Clear()
{
	_envTable->clear();
#if defined(WIN32)
	m_sorted_varnames.clear();
#endif
}

bool
Env::MergeFrom( const ClassAd *ad, std::string & error_msg )
{
	if(!ad) return true;

	std::string env;
	bool merge_success = false;

	if( ad->LookupString(ATTR_JOB_ENVIRONMENT, env) == 1 ) {
		merge_success = MergeFromV2Raw(env.c_str(),&error_msg);
	}
	else if( ad->LookupString(ATTR_JOB_ENV_V1, env) == 1 ) {
		char delim = 0;
		std::string delim_str;
		if (ad->LookupString(ATTR_JOB_ENV_V1_DELIM,delim_str) && ! delim_str.empty()) {
			delim = delim_str[0];
		}
		merge_success = MergeFromV1AutoDelim(env.c_str(), error_msg, delim);
		input_was_v1 = true;
	}
	else {
			// this shouldn't be considered an error... maybe the job
			// just doesn't define an environment.  condor_submit always 
			// adds something, but we should't rely on that.
		merge_success = true;
			// leave input_was_v1 untouched... (at dan's recommendation)
	}
	return merge_success;
}

bool
Env::CondorVersionRequiresV1(CondorVersionInfo const &condor_version)
{
		// Is it earlier than 6.7.15?
	return !condor_version.built_since_version(6,7,15);
}

// write this class into the V2 environment attribute of the job
bool
Env::InsertEnvIntoClassAd(ClassAd & ad) const
{
	std::string env;
	if ( ! getDelimitedStringV2Raw(env)) {
		// the above function will never return false, so we can't end up here...
		return false;
	}
	ad.Assign(ATTR_JOB_ENVIRONMENT,env);
	return true;
}

// Write this class into the V1 enviroment attribute of the job
// If not all of the enviroment can be expressed in that format, an error is returned and the job is unchanged
bool
Env::InsertEnvV1IntoClassAd( ClassAd & ad, std::string & error_msg, char delim /*=0*/) const
{
	std::string delim_str;
	if (delim) {
		// nothing to do
	} else if (ad.LookupString(ATTR_JOB_ENV_V1_DELIM,delim_str) && ! delim_str.empty()) {
		// Use delimiter that was used previously in this ad.
		delim = delim_str[0];
	}
	else {
		// Use delimiter for the opsys we are currently running under.
		delim = env_delimiter;
	}

	MyString env1;
	if(getDelimitedStringV1Raw(&env1,&error_msg,delim)) {
		ad.Assign(ATTR_JOB_ENV_V1,env1.c_str());

		if(delim_str.empty()) {
			// Save the delimiter that we have chosen, in case the ad
			// is read by somebody on a platform that is not the same
			// as opsys.  Example: we are writing the expanded ad in
			// the schedd for a starter on a different opsys, but we
			// want shadows to be able to still parse the environment.

			delim_str.push_back(delim);
			ad.Assign(ATTR_JOB_ENV_V1_DELIM,delim_str);
		}
		return true;
	}

	return false;
}

// write this class into job as either V1 or V2 environment, using V1 if the job already has V1 only
// and the contents of this class can be expressed as V1, otherwise use V2
// The error message string will be set when the job formerly had  V1 enviroment but the insert was forced to switch to V2
bool
Env::InsertEnvIntoClassAd(ClassAd & ad, std::string & error_msg) const
{
	// if the ad currently has a V1 attribute and NO V2 attribute, attempt to write
	// to the V1 attribute, but if we can't.  just delete the V1 and write to V2 instead
	// we know that all versions of the condor_starter will prefer V2 anyway.
	// the attempt to use V1 is mostly to avoid confusing the user who submitted the job
	if (ad.LookupExpr(ATTR_JOB_ENV_V1) && ! ad.LookupExpr(ATTR_JOB_ENVIRONMENT)) {
		if (InsertEnvV1IntoClassAd(ad, error_msg)) {
			return true;
		}
		ad.Delete(ATTR_JOB_ENV_V1);
		// fall through to write the V2 attribute
	}

	return InsertEnvIntoClassAd(ad);
}


bool
Env::MergeFrom( char const * const *stringArray )
{
	if( !stringArray ) {
		return false;
	}
	int i;
	bool all_ok = true;
	for( i = 0; stringArray[i] && stringArray[i][0] != '\0'; i++ ) {
		if( !SetEnv( stringArray[i] ) ) {
				// Keep going so that we behave like getenv() in
				// our treatment of invalid entries in the
				// environment.  However, this function still
				// returns error, unlike Import().
			all_ok = false;
		}
	}
	return all_ok;
}

bool
Env::MergeFrom( char const * env_str )
{
	if ( !env_str ) {
		return false;
	}
	const char *ptr = env_str;
	while ( *ptr != '\0' ) {
		// a Windows environment block typically contains stuff like
		// '=::=::\' and '=C:=C:\some\path'; SetEnv will return an error
		// for strings like these, so we'll just silently ignore errors
		// from SetEnv and insert what we can
		//
		SetEnv( ptr );
		ptr += strlen(ptr) + 1;
	}
	return true;
}

void
Env::MergeFrom( Env const &env )
{
	MyString var,val;

	env._envTable->startIterations();
	while(env._envTable->iterate(var,val)) {
		ASSERT(SetEnv(var,val));
	}
}

bool
Env::IsSafeEnvV1Value(char const *str,char delim)
{
	// This is used to detect whether the given environment value
	// is unexpressable in the old environment syntax (before environment2).

	if(!str) return false;
	if(!delim) delim = env_delimiter;

	char specials[] = {'|','\n','\0'};
	// Some compilers do not like env_delimiter to be in the
	// initialization constant.
	specials[0] = delim;

	size_t safe_length = strcspn(str,specials);

	// If safe_length goes to the end of the string, we are okay.
	return !str[safe_length];
}

bool
Env::IsSafeEnvV2Value(char const *str)
{
	// This is used to detect whether the given environment value
	// is unexpressable in the environment2 syntax.

	if(!str) return false;

	// Newline is an unsafe character, because the schedd (as of
	// 6.8.3) cannot handle newlines in ClassAd attributes.

	char specials[] = {'\n','\0'};

	size_t safe_length = strcspn(str,specials);

	// If safe_length goes to the end of the string, we are okay.
	return !str[safe_length];
}

void
Env::WriteToDelimitedString(char const *input,MyString &output) {
	// Append input to output.
	// Would be nice to escape special characters here, but the
	// existing syntax does not support it, so we leave the
	// "specials" strings blank.

	char const inner_specials[] = {'\0'};
	char const first_specials[] = {'\0'};

	char const *specials = first_specials;
	char const *end;
	bool ret;

	if(!input) return;

	while(*input) {
		end = input + strcspn(input,specials);
		ret = output.formatstr_cat("%.*s", (int)(end-input), input);
		ASSERT(ret);
		input = end;

		if(*input != '\0') {
			// Escape this special character.
			// Escaping is not yet implemented, so we will never get here.
			ret = output.formatstr_cat("%c",*input);
			ASSERT(ret);
			input++;
		}

		// Switch out of first-character escaping mode.
		specials = inner_specials;
	}
}

bool
Env::ReadFromDelimitedString(char const *&input, char *output, char delim) {
	// output buffer must be big enough to hold next environment entry
	// (to be safe, it should be same size as input buffer)

	// strip leading (non-escaped) whitespace
	while( *input==' ' || *input=='\t' || *input=='\n'  || *input=='\r') {
		input++;
	}

	while( *input ) {
		if(*input == '\n' || *input == delim) {
			// for backwards compatibility with old env parsing in environ.C,
			// we also treat '\n' as a valid delimiter
			input++;
			break;
		}
		else {
			// all other characters are copied verbatim
			*(output++) = *(input++);
		}
	}

	*output = '\0';

	return true;
}

bool
Env::IsV2QuotedString(char const *str)
{
	return ArgList::IsV2QuotedString(str);
}

bool
Env::V2QuotedToV2Raw(char const *v1_quoted,MyString *v2_raw,MyString *errmsg)
{
	return ArgList::V2QuotedToV2Raw(v1_quoted,v2_raw,errmsg);
}

bool
Env::MergeFromV1RawOrV2Quoted( const char *delimitedString, std::string & error_msg )
{
	if(!delimitedString) return true;
	if(IsV2QuotedString(delimitedString)) {
		return MergeFromV2Quoted(delimitedString,error_msg);
	}
	else {
		return MergeFromV1AutoDelim(delimitedString,error_msg);
	}
}

bool
Env::MergeFromV2Quoted( const char *delimitedString, std::string & error_msg )
{
	if(!delimitedString) return true;
	if(IsV2QuotedString(delimitedString)) {
		MyString v2, msg;
		if(!V2QuotedToV2Raw(delimitedString,&v2,&msg)) {
			if ( ! msg.empty()) { AddErrorMessage(msg.c_str(), error_msg); }
			return false;
		}
		return MergeFromV2Raw(v2.c_str(),&error_msg);
	}
	else {
		AddErrorMessage("Expecting a double-quoted environment string (V2 format).",error_msg);
		return false;
	}
}

bool
Env::MergeFromV2Raw( const char *delimitedString, std::string* error_msg )
{
	SimpleList<MyString> env_list;

	if(!delimitedString) return true;

	if(!split_args(delimitedString,&env_list,error_msg)) {
		return false;
	}

	SimpleListIterator<MyString> it(env_list);
	MyString *env_entry;
	while(it.Next(env_entry)) {
		if(!SetEnvWithErrorMessage(env_entry->c_str(),error_msg)) {
			return false;
		}
	}
	return true;
}

bool
Env::MergeFromV1Raw( const char *delimitedString, char delim, std::string* error_msg )
{
	char const *input;
	char *output;
	int outputlen;
	bool retval = true;

	input_was_v1 = true;
	if(!delimitedString) return true;

	// create a buffer big enough to hold any of the individual env expressions
	outputlen = strlen(delimitedString)+1;
	output = new char[outputlen];
	ASSERT(output);

	input = delimitedString;
	while( *input ) {
		retval = ReadFromDelimitedString(input,output,delim);

		if(!retval) {
			break; //failed to parse environment string
		}

		if(*output) {
			retval = SetEnvWithErrorMessage(output,error_msg);
			if(!retval) {
				break; //failed to add environment expression
			}
		}
	}
	delete[] output;
	return retval;
}

bool Env::MergeFromV1AutoDelim( const char *delimitedString, std::string & error_msg, char delim /*=0*/ )
{
	// an empty string or null pointer is trival success
	if(!delimitedString || ! *delimitedString) return true;

	if ( ! delim) { delim = env_delimiter; }
	char ch = *delimitedString;
	if (ch == delim) { ++delimitedString; }
	else if (ch && is_permitted_delim(ch)) {
		// if the string starts with a permitted delim character, assume that indicates the delim
		delim = ch;
		++delimitedString;
	}
	return MergeFromV1Raw(delimitedString,delim,&error_msg);
}

// It is not possible for raw V1 environment strings with a leading space
// to be specified in submit files, so we can use this to mark
// V2 strings when we need to pack V1 and V2 through the same
// channel (e.g. shadow-starter communication).
const char RAW_V2_ENV_MARKER = ' ';

bool
Env::MergeFromV1or2Raw( const char *delimitedString, std::string &error_msg )
{
	if(!delimitedString) return true;
	if(*delimitedString == RAW_V2_ENV_MARKER) {
		return MergeFromV2Raw(delimitedString,&error_msg);
	}
	return MergeFromV1AutoDelim(delimitedString,error_msg);
}

// The following is a modest hack for when we find
// a $$() expression in the environment and we just want to
// pass it through verbatim, with no appended equal sign.
char const *NO_ENVIRONMENT_VALUE = "\01\02\03\04\05\06";

bool
Env::SetEnvWithErrorMessage( const char *nameValueExpr, std::string* error_msg )
{
	char *expr, *delim;
	int retval;

	if( nameValueExpr == NULL || nameValueExpr[0] == '\0' ) {
		return false;
	}

	// make a copy of nameValueExpr for modifying
	expr = strdup( nameValueExpr );
	ASSERT( expr );

	// find the delimiter
	delim = strchr( expr, '=' );

	if(delim == NULL && strstr(expr,"$$")) {
		// This environment entry is an unexpanded $$() macro.
		// We just want to keep it in the environment verbatim.
		SetEnv(expr,NO_ENVIRONMENT_VALUE);
		free(expr);
		return true;
	}

	// fail if either name or delim is missing
	if( expr == delim || delim == NULL ) {
		if(error_msg) {
			std::string msg;
			if(delim == NULL) {
				formatstr(
				  msg,
				  "ERROR: Missing '=' after environment variable '%s'.",
				  nameValueExpr);
			}
			else {
				formatstr(msg, "ERROR: missing variable in '%s'.", expr);
			}
			AddErrorMessage(msg.c_str(),*error_msg);
		}
		free(expr);
		return false;
	}

	// overwrite delim with '\0' so we have two valid strings
	*delim = '\0';

	// do the deed
	retval = SetEnv( expr, delim + 1 );
	free(expr);
	return retval;
}

bool
Env::SetEnv( const char* var, const char* val )
{
	MyString myVar = var;
	MyString myVal = val;
	return SetEnv( myVar, myVal );
}

bool
Env::SetEnv( const MyString & var, const MyString & val )
{
	if( var.length() == 0 ) {
		return false;
	}
	bool ret = (_envTable->insert( var, val, true ) == 0);
	ASSERT( ret );
#if defined(WIN32)
	m_sorted_varnames.erase(var.Value());
	m_sorted_varnames.insert(var.Value());
#endif
	return true;
}

bool
Env::SetEnv( const std::string & var, const std::string & val )
{
	if( var.length() == 0 ) {
		return false;
	}
	bool ret = (_envTable->insert( var, val, true ) == 0);
	ASSERT( ret );
#if defined(WIN32)
	m_sorted_varnames.erase(var.c_str());
	m_sorted_varnames.insert(var.c_str());
#endif
	return true;
}

bool
Env::DeleteEnv(const std::string & name)
{
	if (!name.size()) { return false; }

	bool ret = (_envTable->remove(name.c_str()) == 0);

#if defined(WIN32)
	m_sorted_varnames.erase(name.c_str());
#endif
	return ret;
}

bool
Env::getDelimitedStringV2Quoted(MyString *result,MyString *error_msg) const
{
	MyString v2_raw;
	if(!getDelimitedStringV2Raw(&v2_raw)) {
		return false;
	}
	ArgList::V2RawToV2Quoted(v2_raw,result);
	return true;
}

bool
Env::getDelimitedStringV2Raw(std::string & result, bool mark_v2) const {
    MyString ms;
    bool rv = getDelimitedStringV2Raw( & ms, mark_v2 );
    if(! ms.empty()) { result = ms; }
    return rv;
}

bool
Env::getDelimitedStringV2Raw(MyString *result, bool mark_v2) const
{
	MyString var, val;
	SimpleList<MyString> env_list;

	ASSERT(result);

	_envTable->startIterations();
	while( _envTable->iterate( var, val ) ) {
		if(val == NO_ENVIRONMENT_VALUE) {
			env_list.Append(var);
		}
		else {
			MyString var_val;
			var_val.formatstr("%s=%s",var.c_str(),val.c_str());
			env_list.Append(var_val);
		}
	}

	if(mark_v2) {
		(*result) += RAW_V2_ENV_MARKER;
	}
	join_args(env_list,result);
	return true;
}

void
Env::getDelimitedStringForDisplay(std::string & result) const
{
	getDelimitedStringV2Raw(result);
}

char
Env::GetEnvV1Delimiter(char const *opsys)
{
	if(!opsys) {
		return env_delimiter;
	}
	else if(!strncmp(opsys,"WIN",3)) { // match "WINDOWS" or "WINNTnn" or "WIN32"
		return windows_env_delim;
	}
	else {
		return unix_env_delim;
	}
}

char
Env::GetEnvV1Delimiter(const ClassAd& ad)
{
	std::string delim_str;
	char delim = env_delimiter;
	if (ad.LookupString(ATTR_JOB_ENV_V1_DELIM,delim_str) && ! delim_str.empty()) {
		delim = delim_str[0];
	}
	return delim;
}


bool
Env::getDelimitedStringV1Raw(MyString *result,std::string * error_msg,char delim) const
{
	MyString var, val;

	if(!delim) delim = env_delimiter;

	ASSERT(result);

	_envTable->startIterations();
	while( _envTable->iterate( var, val ) ) {
		if(!IsSafeEnvV1Value(var.c_str(),delim) ||
		   !IsSafeEnvV1Value(val.c_str(),delim)) {

			if(error_msg) {
				std::string msg;
				formatstr(msg, "Environment entry is not compatible with V1 syntax: %s=%s",var.c_str(),val.c_str());
				AddErrorMessage(msg.c_str(),*error_msg);
			}
			return false;
		}
		// only insert the delimiter if there's already an entry...
		if( ! result->empty()) { (*result) += delim; }
		WriteToDelimitedString(var.c_str(),*result);
		if(val != NO_ENVIRONMENT_VALUE) {
			WriteToDelimitedString("=",*result);
			WriteToDelimitedString(val.c_str(),*result);
		}
	}
	return true;
}

#if defined(WIN32)
char*
Env::getWindowsEnvironmentString() const
{
	// this returns an environment string in the format needed
	// for passing to the Windows CreateProcess function. this
	// means it is null-delimited, null-terminated, and sorted
	// (igoring case - all variable names must be converted to
	// uppercase before comparing)

	std::string output;
	typedef std::set<std::string, toupper_string_less> set_t;
	for (set_t::const_iterator i = m_sorted_varnames.begin();
	     i != m_sorted_varnames.end();
	     i++)
	{
		MyString val;
		int ret = _envTable->lookup(i->c_str(), val);
		ASSERT(ret != -1);
		output += *i;
		output += '=';
		output += val.Value();
		output += '\0';
	}
	output += '\0';
	char* ret = new char[output.size()];
	ASSERT(ret != NULL);
	#pragma warning(suppress: 4996) // suppress next warning: std::string.copy may be unsafe.
	output.copy(ret, output.size());
	return ret;
}
#endif

char **
Env::getStringArray() const {
	char **array = NULL;
	int numVars = _envTable->getNumElements();
	int i;

	array = (char **)malloc((numVars+1)*sizeof(char*));
	ASSERT( array );

    MyString var, val;

    _envTable->startIterations();
	for( i = 0; _envTable->iterate( var, val ); i++ ) {
		ASSERT( i < numVars );
		ASSERT( var.length() > 0 );
		array[i] = (char *)malloc(var.length() + val.length() + 2);
		ASSERT( array[i] );
		strcpy( array[i], var.c_str() );
		if(val != NO_ENVIRONMENT_VALUE) {
			strcat( array[i], "=" );
			strcat( array[i], val.c_str() );
		}
	}
	array[i] = NULL;
	return array;
}

void Env::Walk(bool (*walk_func)(void* pv, const MyString &var, MyString &val), void* pv)
{
	const MyString *var;
	MyString *val;

	_envTable->startIterations();
	while (_envTable->iterate_nocopy(&var, &val)) {
		if ( ! walk_func(pv, *var, *val))
			break;
	}
}

void Env::Walk(bool (*walk_func)(void* pv, const MyString &var, const MyString &val), void* pv) const
{
	const MyString *var;
	MyString *val;

	_envTable->startIterations();
	while (_envTable->iterate_nocopy(&var, &val)) {
		if ( ! walk_func(pv, *var, *val))
			break;
	}
}

//
// This makes me sad.  Consider replacing this (in
// condor_starter.V6/singulariy.cpp, at any rate), with a function which,
// once this class's internals are std::string, directly returns the desired
// std::list<std::string>... or maybe std::vector<std::string>?
//
void
Env::Walk(bool (*walk_func)(void* pv, const std::string &var, const std::string &val), void* pv) const
{
	const MyString *var;
	MyString *val;

	_envTable->startIterations();
	while (_envTable->iterate_nocopy(&var, &val)) {
	    std::string s(var->c_str());
	    std::string t(val->c_str());
		if ( ! walk_func(pv, s, t))
			break;
	}

}

bool
Env::GetEnv(MyString const &var,MyString &val) const
{
	// lookup returns 0 on success
	return _envTable->lookup(var,val) == 0;
}

bool
Env::GetEnv(const std::string &var, std::string &val) const
{
	// lookup returns 0 on success
	MyString mystr;
	if (_envTable->lookup(var,mystr) == 0) {
		val = mystr.c_str();
		return true;
	}
	return false;
}

void
Env::Import( void )
{
	char **my_environ = GetEnviron();
	for (int i=0; my_environ[i]; i++) {
		const char	*p = my_environ[i];

		int			j;
		MyString	varname = "";
		MyString	value = "";
		for (j=0;  ( p[j] != '\0' ) && ( p[j] != '=' );  j++) {
			varname += p[j];
		}
		if ( p[j] == '\0' ) {
				// ignore entries in the environment that do not
				// contain an assignment
			continue;
		}
		if ( varname.empty() ) {
				// ignore entries in the environment that contain
				// an empty variable name
			continue;
		}
		ASSERT( p[j] == '=' );
		value = p+j+1;

		// Allow the application to filter the import
		if ( ImportFilter( varname, value ) ) {
			bool ret = SetEnv( varname, value );
			ASSERT( ret ); // should never fail
		}
	}
}
