/***************************************************************
 *
 * Copyright (C) 1990-2011, Condor Team, Computer Sciences Department,
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
#include "condor_config.h"
#include "condor_debug.h"
#include "util_lib_proto.h"
#include "condor_auth_ssl.h"
#include "DelegationInterface.h"

#if defined(DLOPEN_GSI_LIBS)
#include <dlfcn.h>
#endif

#include "globus_utils.h"

#if defined(HAVE_EXT_GLOBUS)
// Note: this is from OpenSSL, but should be present if Globus is.
// Only used if HAVE_EXT_GLOBUS.
#     include "openssl/x509v3.h"
#endif

#define DEFAULT_MIN_TIME_LEFT 8*60*60;

static std::string _globus_error_message;

#if defined(HAVE_EXT_GLOBUS)

// This symbol is in libglobus_gssapi_gsi, but it's not exposed in any
// public header file.
extern gss_OID_desc *gss_nt_host_ip;

// Symbols from libglobus_common
int (*globus_module_activate_ptr)(
	globus_module_descriptor_t *) = NULL;
int (*globus_thread_set_model_ptr)(
	const char *) = NULL;
globus_object_t *(*globus_error_peek_ptr)(
	globus_result_t) = NULL;
char *(*globus_error_print_friendly_ptr)(
	globus_object_t *) = NULL;
// Symbols from libglobus_gsi_sysconfig
globus_result_t (*globus_gsi_sysconfig_get_proxy_filename_unix_ptr)(
	char **, globus_gsi_proxy_file_type_t) = NULL;
// Symbols from libglobus_gsi_credential
globus_result_t (*globus_gsi_cred_get_cert_ptr)(
	globus_gsi_cred_handle_t, X509 **) = NULL;
globus_result_t (*globus_gsi_cred_get_cert_chain_ptr)(
	globus_gsi_cred_handle_t, STACK_OF(X509) **) = NULL;
globus_result_t (*globus_gsi_cred_get_cert_type_ptr)(
	globus_gsi_cred_handle_t, globus_gsi_cert_utils_cert_type_t *) = NULL;
globus_result_t (*globus_gsi_cred_get_identity_name_ptr)(
	globus_gsi_cred_handle_t, char **) = NULL;
globus_result_t (*globus_gsi_cred_get_lifetime_ptr)(
	globus_gsi_cred_handle_t, time_t *) = NULL;
globus_result_t (*globus_gsi_cred_get_subject_name_ptr)(
	globus_gsi_cred_handle_t, char **) = NULL;
globus_result_t (*globus_gsi_cred_handle_attrs_destroy_ptr)(
	globus_gsi_cred_handle_attrs_t) = NULL;
globus_result_t (*globus_gsi_cred_handle_attrs_init_ptr)(
	globus_gsi_cred_handle_attrs_t *) = NULL;
globus_result_t (*globus_gsi_cred_handle_destroy_ptr)(
	globus_gsi_cred_handle_t) = NULL;
globus_result_t (*globus_gsi_cred_handle_init_ptr)(
	globus_gsi_cred_handle_t *, globus_gsi_cred_handle_attrs_t) = NULL;
globus_result_t (*globus_gsi_cred_read_proxy_ptr)(
	globus_gsi_cred_handle_t, const char *) = NULL;
globus_result_t (*globus_gsi_cred_write_proxy_ptr)(
	globus_gsi_cred_handle_t, char *) = NULL;
// Symbols for libglobus_gsi_proxy_core
globus_result_t (*globus_gsi_proxy_assemble_cred_ptr)(
	globus_gsi_proxy_handle_t, globus_gsi_cred_handle_t *, BIO *) = NULL;
globus_result_t (*globus_gsi_proxy_create_req_ptr)(
	globus_gsi_proxy_handle_t, BIO *) = NULL;
globus_result_t (*globus_gsi_proxy_handle_attrs_destroy_ptr)(
	globus_gsi_proxy_handle_attrs_t) = NULL;
globus_result_t (*globus_gsi_proxy_handle_attrs_get_keybits_ptr)(
	globus_gsi_proxy_handle_attrs_t, int *) = NULL;
globus_result_t (*globus_gsi_proxy_handle_attrs_init_ptr)(
	globus_gsi_proxy_handle_attrs_t *) = NULL;
globus_result_t (*globus_gsi_proxy_handle_attrs_set_clock_skew_allowable_ptr)(
	globus_gsi_proxy_handle_attrs_t, int) = NULL;
globus_result_t (*globus_gsi_proxy_handle_attrs_set_keybits_ptr)(
	globus_gsi_proxy_handle_attrs_t, int) = NULL;
globus_result_t (*globus_gsi_proxy_handle_destroy_ptr)(
	globus_gsi_proxy_handle_t) = NULL;
globus_result_t (*globus_gsi_proxy_handle_init_ptr)(
	globus_gsi_proxy_handle_t *, globus_gsi_proxy_handle_attrs_t) = NULL;
globus_result_t (*globus_gsi_proxy_handle_set_is_limited_ptr)(
	globus_gsi_proxy_handle_t, globus_bool_t) = NULL;
globus_result_t (*globus_gsi_proxy_handle_set_time_valid_ptr)(
	globus_gsi_proxy_handle_t, int) = NULL;
globus_result_t (*globus_gsi_proxy_handle_set_type_ptr)(
	globus_gsi_proxy_handle_t, globus_gsi_cert_utils_cert_type_t) = NULL;
globus_result_t (*globus_gsi_proxy_inquire_req_ptr)(
	globus_gsi_proxy_handle_t, BIO *) = NULL;
globus_result_t (*globus_gsi_proxy_sign_req_ptr)(
	globus_gsi_proxy_handle_t, globus_gsi_cred_handle_t, BIO *) = NULL;
// Symbols from libglobus_gssapi_gsi
OM_uint32 (*gss_accept_sec_context_ptr)(
	OM_uint32 *, gss_ctx_id_t *, const gss_cred_id_t, const gss_buffer_t,
	const gss_channel_bindings_t, gss_name_t *, gss_OID *, gss_buffer_t,
	OM_uint32 *, OM_uint32 *, gss_cred_id_t *) = NULL;
OM_uint32 (*gss_compare_name_ptr)(
	OM_uint32 *, const gss_name_t, const gss_name_t, int *) = NULL;
OM_uint32 (*gss_context_time_ptr)(
	OM_uint32 *, const gss_ctx_id_t, OM_uint32 *) = NULL;
OM_uint32 (*gss_delete_sec_context_ptr)(
	OM_uint32 *, gss_ctx_id_t *, gss_buffer_t) = NULL;
OM_uint32 (*gss_display_name_ptr)(
	OM_uint32 *, const gss_name_t, gss_buffer_t, gss_OID *) = NULL;
OM_uint32 (*gss_import_cred_ptr)(
	OM_uint32 *, gss_cred_id_t *, const gss_OID, OM_uint32,
	const gss_buffer_t, OM_uint32, OM_uint32 *) = NULL;
OM_uint32 (*gss_import_name_ptr)(
	OM_uint32 *, const gss_buffer_t, const gss_OID, gss_name_t *) = NULL;
OM_uint32 (*gss_inquire_context_ptr)(
	OM_uint32 *, const gss_ctx_id_t, gss_name_t *, gss_name_t *,
	OM_uint32 *, gss_OID *, OM_uint32 *, int *, int *) = NULL;
OM_uint32 (*gss_release_buffer_ptr)(
	OM_uint32 *, gss_buffer_t) = NULL;
OM_uint32 (*gss_release_cred_ptr)(
	OM_uint32 *, gss_cred_id_t *) = NULL;
OM_uint32 (*gss_release_name_ptr)(
	OM_uint32 *, gss_name_t *) = NULL;
OM_uint32 (*gss_unwrap_ptr)(
	OM_uint32 *, const gss_ctx_id_t, const gss_buffer_t, gss_buffer_t, int *,
	gss_qop_t *) = NULL;
OM_uint32 (*gss_wrap_ptr)(
	OM_uint32 *, const gss_ctx_id_t, int, gss_qop_t, const gss_buffer_t,
	int *, gss_buffer_t) = NULL;
gss_OID_desc **gss_nt_host_ip_ptr = NULL;
// Symbols from libglobus_gss_assist
OM_uint32 (*globus_gss_assist_display_status_str_ptr)(
	char **, char *, OM_uint32, OM_uint32, int) = NULL;
globus_result_t (*globus_gss_assist_map_and_authorize_ptr)(
	gss_ctx_id_t, char *, char *, char *, unsigned int) = NULL;
OM_uint32 (*globus_gss_assist_acquire_cred_ptr)(
	OM_uint32 *, gss_cred_usage_t, gss_cred_id_t *) = NULL;
OM_uint32 (*globus_gss_assist_init_sec_context_ptr)(
	OM_uint32 *, const gss_cred_id_t, gss_ctx_id_t *, char *, OM_uint32,
	OM_uint32 *, int *, int (*)(void *, void **, size_t *), void *,
	int (*)(void *, void *, size_t), void *) = NULL;
globus_module_descriptor_t *globus_i_gsi_gss_assist_module_ptr = NULL;
// Symbols from libvomsapi
#if defined(HAVE_EXT_VOMS)
void (*VOMS_Destroy_ptr)(
	struct vomsdata *) = NULL;
char *(*VOMS_ErrorMessage_ptr)(
	struct vomsdata *, int, char *, int) = NULL;
struct vomsdata *(*VOMS_Init_ptr)(
	char *, char *) = NULL;
int (*VOMS_Retrieve_ptr)(
	X509 *, STACK_OF(X509) *, int, struct vomsdata *, int *) = NULL;
int (*VOMS_SetVerificationType_ptr)(
	int, struct vomsdata *, int *) = NULL;
#endif /* defined(HAVE_EXT_VOMS) */

#endif /* defined(HAVE_EXT_GLOBUS) */

#define GRAM_STATUS_STR_LEN		8

#define NOT_SUPPORTED_MSG "This version of Condor doesn't support GSI security"

const char *
GlobusJobStatusName( int status )
{
	static char buf[GRAM_STATUS_STR_LEN];
#if defined(HAVE_EXT_GLOBUS)
	switch ( status ) {
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_PENDING:
		return "PENDING";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_ACTIVE:
		return "ACTIVE";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_FAILED:
		return "FAILED";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_DONE:
		return "DONE";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_SUSPENDED:
		return "SUSPENDED";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNSUBMITTED:
		return "UNSUBMITTED";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_IN:
		return "STAGE_IN";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_STAGE_OUT:
		return "STAGE_OUT";
	case GLOBUS_GRAM_PROTOCOL_JOB_STATE_UNKNOWN:
		return "UNKNOWN";
	default:
		snprintf( buf, GRAM_STATUS_STR_LEN, "%d", status );
		return buf;
	}
#else
	snprintf( buf, GRAM_STATUS_STR_LEN, "%d", status );
	return buf;
#endif
}

const char *
x509_error_string( void )
{
	return _globus_error_message.c_str();
}

static
void
set_error_string( const char *message )
{
	_globus_error_message = message;
}

#if defined(HAVE_EXT_GLOBUS)
static
bool
set_error_string( globus_result_t result )
{
	globus_object_t *err_obj = (*globus_error_peek_ptr)( result );
	char *msg = NULL;
	if ( err_obj && (msg = (*globus_error_print_friendly_ptr)( err_obj )) ) {
		_globus_error_message = msg;
		free( msg );
		return true;
	}
	return false;
}
#endif

/* Activate the globus gsi modules for use by functions in this file.
 * Returns zero if the modules were successfully activated. Returns -1 if
 * something went wrong.
 */

int
activate_globus_gsi( void )
{
#if !defined(HAVE_EXT_GLOBUS)
	set_error_string( NOT_SUPPORTED_MSG );
	return -1;
#else
	static bool globus_gsi_activated = false;
	static bool activation_failed = false;

	if ( globus_gsi_activated ) {
		return 0;
	}
	if ( activation_failed ) {
		return -1;
	}

	if ( Condor_Auth_SSL::Initialize() == false ) {
		// Error in the dlopen/sym calls for libssl, return failure.
		_globus_error_message = "Failed to open SSL library";
		activation_failed = true;
		return -1;
	}

#if defined(DLOPEN_GSI_LIBS)
	void *dl_hdl;

	if ( (dl_hdl = dlopen(LIBLTDL_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_COMMON_SO, RTLD_LAZY)) == NULL ||
		 !(globus_module_activate_ptr = (int (*)(globus_module_descriptor_t*))dlsym(dl_hdl, "globus_module_activate")) ||
		 !(globus_thread_set_model_ptr = (int (*)(const char*))dlsym(dl_hdl, "globus_thread_set_model")) ||
		 !(globus_error_peek_ptr = (globus_object_t* (*)(globus_result_t))dlsym(dl_hdl, "globus_error_peek")) ||
		 !(globus_error_print_friendly_ptr = (char* (*)(globus_object_t*))dlsym(dl_hdl, "globus_error_print_friendly")) ||
		 (dl_hdl = dlopen(LIBGLOBUS_CALLOUT_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_PROXY_SSL_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_OPENSSL_ERROR_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_OPENSSL_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSI_CERT_UTILS_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSI_SYSCONFIG_SO, RTLD_LAZY)) == NULL ||
		 !(globus_gsi_sysconfig_get_proxy_filename_unix_ptr = (globus_result_t (*)(char**, globus_gsi_proxy_file_type_t))dlsym(dl_hdl, "globus_gsi_sysconfig_get_proxy_filename_unix")) ||
		 (dl_hdl = dlopen(LIBGLOBUS_OLDGAA_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSI_CALLBACK_SO, RTLD_LAZY)) == NULL ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSI_CREDENTIAL_SO, RTLD_LAZY))== NULL ||
		 !(globus_gsi_cred_get_cert_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, X509**))dlsym(dl_hdl, "globus_gsi_cred_get_cert")) ||
		 !(globus_gsi_cred_get_cert_chain_ptr = (globus_result_t (*)(globus_gsi_cred_handle_t, STACK_OF(X509)**))dlsym(dl_hdl, "globus_gsi_cred_get_cert_chain")) ||
		 !(globus_gsi_cred_get_cert_type_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, globus_gsi_cert_utils_cert_type_t*))dlsym(dl_hdl, "globus_gsi_cred_get_cert_type")) ||
		 !(globus_gsi_cred_get_identity_name_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, char**))dlsym(dl_hdl, "globus_gsi_cred_get_identity_name")) ||
		 !(globus_gsi_cred_get_lifetime_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, time_t*))dlsym(dl_hdl, "globus_gsi_cred_get_lifetime")) ||
		 !(globus_gsi_cred_get_subject_name_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, char**))dlsym(dl_hdl, "globus_gsi_cred_get_subject_name")) ||
		 !(globus_gsi_cred_handle_attrs_destroy_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_attrs_s*))dlsym(dl_hdl, "globus_gsi_cred_handle_attrs_destroy")) ||
		 !(globus_gsi_cred_handle_attrs_init_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_attrs_s**))dlsym(dl_hdl, "globus_gsi_cred_handle_attrs_init")) ||
		 !(globus_gsi_cred_handle_destroy_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*))dlsym(dl_hdl, "globus_gsi_cred_handle_destroy")) ||
		 !(globus_gsi_cred_handle_init_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s**, globus_l_gsi_cred_handle_attrs_s*))dlsym(dl_hdl, "globus_gsi_cred_handle_init")) ||
		 !(globus_gsi_cred_read_proxy_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, const char*))dlsym(dl_hdl, "globus_gsi_cred_read_proxy")) ||
		 !(globus_gsi_cred_write_proxy_ptr = (globus_result_t (*)(globus_l_gsi_cred_handle_s*, char*))dlsym(dl_hdl, "globus_gsi_cred_write_proxy")) ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSI_PROXY_CORE_SO, RTLD_LAZY)) == NULL ||
		 !(globus_gsi_proxy_assemble_cred_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, globus_l_gsi_cred_handle_s**, BIO*))dlsym(dl_hdl, "globus_gsi_proxy_assemble_cred")) ||
		 !(globus_gsi_proxy_create_req_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, BIO*))dlsym(dl_hdl, "globus_gsi_proxy_create_req")) ||
		 !(globus_gsi_proxy_handle_attrs_destroy_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_attrs_s*))dlsym(dl_hdl, "globus_gsi_proxy_handle_attrs_destroy")) ||
		 !(globus_gsi_proxy_handle_attrs_get_keybits_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_attrs_s*, int*))dlsym(dl_hdl, "globus_gsi_proxy_handle_attrs_get_keybits")) ||
		 !(globus_gsi_proxy_handle_attrs_init_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_attrs_s**))dlsym(dl_hdl, "globus_gsi_proxy_handle_attrs_init")) ||
		 !(globus_gsi_proxy_handle_attrs_set_clock_skew_allowable_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_attrs_s*, int))dlsym(dl_hdl, "globus_gsi_proxy_handle_attrs_set_clock_skew_allowable")) ||
		 !(globus_gsi_proxy_handle_attrs_set_keybits_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_attrs_s*, int))dlsym(dl_hdl, "globus_gsi_proxy_handle_attrs_set_keybits")) ||
		 !(globus_gsi_proxy_handle_destroy_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*))dlsym(dl_hdl, "globus_gsi_proxy_handle_destroy")) ||
		 !(globus_gsi_proxy_handle_init_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s**, globus_l_gsi_proxy_handle_attrs_s*))dlsym(dl_hdl, "globus_gsi_proxy_handle_init")) ||
		 !(globus_gsi_proxy_handle_set_is_limited_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, globus_bool_t))dlsym(dl_hdl, "globus_gsi_proxy_handle_set_is_limited")) ||
		 !(globus_gsi_proxy_handle_set_time_valid_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, int))dlsym(dl_hdl, "globus_gsi_proxy_handle_set_time_valid")) ||
		 !(globus_gsi_proxy_handle_set_type_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, globus_gsi_cert_utils_cert_type_t))dlsym(dl_hdl, "globus_gsi_proxy_handle_set_type")) ||
		 !(globus_gsi_proxy_inquire_req_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, BIO*))dlsym(dl_hdl, "globus_gsi_proxy_inquire_req")) ||
		 !(globus_gsi_proxy_sign_req_ptr = (globus_result_t (*)(globus_l_gsi_proxy_handle_s*, globus_l_gsi_cred_handle_s*, BIO*))dlsym(dl_hdl, "globus_gsi_proxy_sign_req")) ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSSAPI_GSI_SO, RTLD_LAZY)) == NULL ||
		 !(gss_accept_sec_context_ptr = (OM_uint32 (*)(OM_uint32 *, gss_ctx_id_t *, const gss_cred_id_t, const gss_buffer_t, const gss_channel_bindings_t, gss_name_t *, gss_OID *, gss_buffer_t, OM_uint32 *, OM_uint32 *, gss_cred_id_t *))dlsym(dl_hdl, "gss_accept_sec_context")) ||
		 !(gss_compare_name_ptr = (OM_uint32 (*)(OM_uint32*, const gss_name_t, const gss_name_t, int*))dlsym(dl_hdl, "gss_compare_name")) ||
		 !(gss_context_time_ptr = (OM_uint32 (*)(OM_uint32*, const gss_ctx_id_t, OM_uint32*))dlsym(dl_hdl, "gss_context_time")) ||
		 !(gss_delete_sec_context_ptr = (OM_uint32 (*)(OM_uint32*, gss_ctx_id_t*, gss_buffer_t))dlsym(dl_hdl, "gss_delete_sec_context")) ||
		 !(gss_display_name_ptr = (OM_uint32 (*)( OM_uint32*, const gss_name_t, gss_buffer_t, gss_OID*))dlsym(dl_hdl, "gss_display_name")) ||
		 !(gss_import_cred_ptr = (OM_uint32 (*)(OM_uint32*, gss_cred_id_desc_struct**, gss_OID_desc_struct*, OM_uint32, gss_buffer_desc_struct*, OM_uint32, OM_uint32*))dlsym(dl_hdl, "gss_import_cred")) ||
		 !(gss_import_name_ptr = (OM_uint32 (*)(OM_uint32*, const gss_buffer_t, const gss_OID, gss_name_t*))dlsym(dl_hdl, "gss_import_name")) ||
		 !(gss_inquire_context_ptr = (OM_uint32 (*)(OM_uint32*, const gss_ctx_id_t, gss_name_t*, gss_name_t*, OM_uint32*, gss_OID*, OM_uint32*, int*, int*))dlsym(dl_hdl, "gss_inquire_context")) ||
		 !(gss_release_buffer_ptr = (OM_uint32 (*)(OM_uint32*, gss_buffer_t))dlsym(dl_hdl, "gss_release_buffer")) ||
		 !(gss_release_cred_ptr = (OM_uint32 (*)(OM_uint32*, gss_cred_id_desc_struct**))dlsym(dl_hdl, "gss_release_cred")) ||
		 !(gss_release_name_ptr = (OM_uint32 (*)(OM_uint32*, gss_name_t*))dlsym(dl_hdl, "gss_release_name")) ||
		 !(gss_unwrap_ptr = (OM_uint32 (*)(OM_uint32*, const gss_ctx_id_t, const gss_buffer_t, gss_buffer_t, int*, gss_qop_t*))dlsym(dl_hdl, "gss_unwrap")) ||
		 !(gss_wrap_ptr = (OM_uint32 (*)(OM_uint32*, const gss_ctx_id_t, int, gss_qop_t, const gss_buffer_t, int*, gss_buffer_t))dlsym(dl_hdl, "gss_wrap")) ||
		 !(gss_nt_host_ip_ptr = (gss_OID_desc **)dlsym(dl_hdl, "gss_nt_host_ip")) ||
		 (dl_hdl = dlopen(LIBGLOBUS_GSS_ASSIST_SO, RTLD_LAZY)) == NULL ||
		 !(globus_gss_assist_display_status_str_ptr = (OM_uint32 (*)(char**, char*, OM_uint32, OM_uint32, int))dlsym(dl_hdl, "globus_gss_assist_display_status_str")) ||
		 !(globus_gss_assist_map_and_authorize_ptr = (globus_result_t (*)(gss_ctx_id_t, char*, char*, char*, unsigned int))dlsym(dl_hdl, "globus_gss_assist_map_and_authorize")) ||
		 !(globus_gss_assist_acquire_cred_ptr = (OM_uint32 (*)(OM_uint32*, gss_cred_usage_t, gss_cred_id_t*))dlsym(dl_hdl, "globus_gss_assist_acquire_cred")) ||
		 !(globus_gss_assist_init_sec_context_ptr = (OM_uint32 (*)(OM_uint32*, const gss_cred_id_t, gss_ctx_id_t*, char*, OM_uint32, OM_uint32*, int*, int (*)(void*, void**, size_t*), void*, int (*)(void*, void*, size_t), void*))dlsym(dl_hdl, "globus_gss_assist_init_sec_context")) ||
		 !(globus_i_gsi_gss_assist_module_ptr = (globus_module_descriptor_t*)dlsym(dl_hdl, "globus_i_gsi_gss_assist_module")) ||
#if defined(HAVE_EXT_VOMS)
		 (dl_hdl = dlopen(LIBVOMSAPI_SO, RTLD_LAZY)) == NULL ||
		 !(VOMS_Destroy_ptr = (void (*)(vomsdata*))dlsym(dl_hdl, "VOMS_Destroy")) ||
		 !(VOMS_ErrorMessage_ptr = (char* (*)(vomsdata*, int, char*, int))dlsym(dl_hdl, "VOMS_ErrorMessage")) ||
		 !(VOMS_Init_ptr = (vomsdata* (*)(char*, char*))dlsym(dl_hdl, "VOMS_Init")) ||
		 !(VOMS_Retrieve_ptr = (int (*)(X509*, STACK_OF(X509)*, int, struct vomsdata*, int*))dlsym(dl_hdl, "VOMS_Retrieve")) ||
		 !(VOMS_SetVerificationType_ptr = (int (*)(int, vomsdata*, int*))dlsym(dl_hdl, "VOMS_SetVerificationType"))
#else
		 false
#endif
		 ) {
			 // Error in the dlopen/sym calls, return failure.
		const char *err = dlerror();
		formatstr( _globus_error_message, "Failed to open GSI libraries: %s", err ? err : "Unknown error" );
		activation_failed = true;
		return -1;
	}
#else
	globus_module_activate_ptr = globus_module_activate;
	globus_thread_set_model_ptr = globus_thread_set_model;
	globus_error_peek_ptr = globus_error_peek;
	globus_error_print_friendly_ptr = globus_error_print_friendly;
	globus_gsi_sysconfig_get_proxy_filename_unix_ptr = globus_gsi_sysconfig_get_proxy_filename_unix;
	globus_gsi_cred_get_cert_ptr = globus_gsi_cred_get_cert;
	globus_gsi_cred_get_cert_chain_ptr = globus_gsi_cred_get_cert_chain;
	globus_gsi_cred_get_cert_type_ptr = globus_gsi_cred_get_cert_type;
	globus_gsi_cred_get_identity_name_ptr = globus_gsi_cred_get_identity_name;
	globus_gsi_cred_get_lifetime_ptr = globus_gsi_cred_get_lifetime;
	globus_gsi_cred_get_subject_name_ptr = globus_gsi_cred_get_subject_name;
	globus_gsi_cred_handle_attrs_destroy_ptr = globus_gsi_cred_handle_attrs_destroy;
	globus_gsi_cred_handle_attrs_init_ptr = globus_gsi_cred_handle_attrs_init;
	globus_gsi_cred_handle_destroy_ptr = globus_gsi_cred_handle_destroy;
	globus_gsi_cred_handle_init_ptr = globus_gsi_cred_handle_init;
	globus_gsi_cred_read_proxy_ptr = globus_gsi_cred_read_proxy;
	globus_gsi_cred_write_proxy_ptr = reinterpret_cast<globus_result_t (*)(globus_l_gsi_cred_handle_s*, char*)>(globus_gsi_cred_write_proxy);
	globus_gsi_proxy_assemble_cred_ptr = globus_gsi_proxy_assemble_cred;
	globus_gsi_proxy_create_req_ptr = globus_gsi_proxy_create_req;
	globus_gsi_proxy_handle_attrs_destroy_ptr = globus_gsi_proxy_handle_attrs_destroy;
	globus_gsi_proxy_handle_attrs_get_keybits_ptr = globus_gsi_proxy_handle_attrs_get_keybits;
	globus_gsi_proxy_handle_attrs_init_ptr = globus_gsi_proxy_handle_attrs_init;
	globus_gsi_proxy_handle_attrs_set_clock_skew_allowable_ptr = globus_gsi_proxy_handle_attrs_set_clock_skew_allowable;
	globus_gsi_proxy_handle_attrs_set_keybits_ptr = globus_gsi_proxy_handle_attrs_set_keybits;
	globus_gsi_proxy_handle_destroy_ptr = globus_gsi_proxy_handle_destroy;
	globus_gsi_proxy_handle_init_ptr = globus_gsi_proxy_handle_init;
	globus_gsi_proxy_handle_set_is_limited_ptr = globus_gsi_proxy_handle_set_is_limited;
	globus_gsi_proxy_handle_set_time_valid_ptr = globus_gsi_proxy_handle_set_time_valid;
	globus_gsi_proxy_handle_set_type_ptr = globus_gsi_proxy_handle_set_type;
	globus_gsi_proxy_inquire_req_ptr = globus_gsi_proxy_inquire_req;
	globus_gsi_proxy_sign_req_ptr = globus_gsi_proxy_sign_req;
	gss_accept_sec_context_ptr = gss_accept_sec_context;
	gss_compare_name_ptr = gss_compare_name;
	gss_context_time_ptr = gss_context_time;
	gss_delete_sec_context_ptr = gss_delete_sec_context;
	gss_display_name_ptr = gss_display_name;
	gss_import_cred_ptr = gss_import_cred;
	gss_import_name_ptr = gss_import_name;
	gss_inquire_context_ptr = gss_inquire_context;
	gss_release_buffer_ptr = gss_release_buffer;
	gss_release_cred_ptr = gss_release_cred;
	gss_release_name_ptr = gss_release_name;
	gss_unwrap_ptr = gss_unwrap;
	gss_wrap_ptr = gss_wrap;
	gss_nt_host_ip_ptr = &gss_nt_host_ip;
	globus_gss_assist_display_status_str_ptr = globus_gss_assist_display_status_str;
	globus_gss_assist_map_and_authorize_ptr = globus_gss_assist_map_and_authorize;
	globus_gss_assist_acquire_cred_ptr = globus_gss_assist_acquire_cred;
	globus_gss_assist_init_sec_context_ptr = globus_gss_assist_init_sec_context;
	globus_i_gsi_gss_assist_module_ptr = &globus_i_gsi_gss_assist_module;
#if defined(HAVE_EXT_VOMS)
	VOMS_Destroy_ptr = VOMS_Destroy;
	VOMS_ErrorMessage_ptr = VOMS_ErrorMessage;
	VOMS_Init_ptr = VOMS_Init;
	VOMS_Retrieve_ptr = VOMS_Retrieve;
	VOMS_SetVerificationType_ptr = VOMS_SetVerificationType;
#endif /* defined(HAVE_EXT_VOMS) */
#endif

	// If this fails, it means something already configured a threaded
	// model. That won't harm us, so ignore it.
	(*globus_thread_set_model_ptr)( GLOBUS_THREAD_MODEL_NONE );

	if ( (*globus_module_activate_ptr)(globus_i_gsi_gss_assist_module_ptr) ) {
		set_error_string( "couldn't activate globus gsi gss assist module" );
		activation_failed = true;
		return -1;
	}

	globus_gsi_activated = true;
	return 0;
#endif
}

/* Return the path to the X509 proxy file as determined by GSI/SSL.
 * Returns NULL if the filename can't be determined. Otherwise, the
 * string returned must be freed with free().
 */
char *
get_x509_proxy_filename( void )
{
	char *proxy_file = NULL;
#if defined(HAVE_EXT_GLOBUS)
	globus_gsi_proxy_file_type_t     file_type    = GLOBUS_PROXY_FILE_INPUT;

	if ( activate_globus_gsi() != 0 ) {
		return NULL;
	}

	if ( (*globus_gsi_sysconfig_get_proxy_filename_unix_ptr)(&proxy_file, file_type) !=
		 GLOBUS_SUCCESS ) {
		set_error_string( "unable to locate proxy file" );
	}
#endif
	return proxy_file;
}


#if defined(HAVE_EXT_VOMS)
// caller must free result
static
char* trim_quotes( char* instr ) {
	char * result;
	int  instr_len;

	if (instr == NULL) {
		return NULL;
	}

	instr_len = strlen(instr);
	// to trim, must be minimum three characters with a double quote first and last
	if ((instr_len > 2) && (instr[0] == '"') && (instr[instr_len-1]) == '"') {
		// alloc a shorter buffer, copy everything in, and null terminate
		result = (char*)malloc(instr_len - 1); // minus two quotes, plus one NULL terminator.
		strncpy(result, &(instr[1]), instr_len-2);
		result[instr_len-2] = 0;
	} else {
		result = strdup(instr);
	}

	return result;
}

// caller responsible for freeing
static
char*
quote_x509_string( char* instr) {
	char * result_string = 0;
	int    result_string_len = 0;

	char * x509_fqan_escape = 0;
	char * x509_fqan_escape_sub = 0;
	char * x509_fqan_delimiter = 0;
	char * x509_fqan_delimiter_sub = 0;

	int x509_fqan_escape_sub_len = 0;
	int x509_fqan_delimiter_sub_len = 0;

	char * tmp_scan_ptr;

	// NULL in, NULL out
	if (!instr) {
		return NULL;
	}

	// we look at first char only.  default '&'.
    if (!(x509_fqan_escape = param("X509_FQAN_ESCAPE"))) {
		x509_fqan_escape = strdup("&");
	}
	// can be multiple chars
    if (!(x509_fqan_escape_sub = param("X509_FQAN_ESCAPE_SUB"))) {
		x509_fqan_escape_sub = strdup("&amp;");
	}

	// we look at first char only.  default ','.
    if (!(x509_fqan_delimiter = param("X509_FQAN_DELIMITER"))) {
		x509_fqan_delimiter = strdup(",");
	}
	// can be multiple chars
    if (!(x509_fqan_delimiter_sub = param("X509_FQAN_DELIMITER_SUB"))) {
		x509_fqan_delimiter_sub = strdup("&comma;");
	}


	// phase 0, trim quotes off if needed
	// use tmp_scan_ptr to temporarily hold trimmed strings while being reassigned.
	tmp_scan_ptr = trim_quotes(x509_fqan_escape);
	free (x509_fqan_escape);
	x509_fqan_escape = tmp_scan_ptr;

	tmp_scan_ptr = trim_quotes(x509_fqan_escape_sub);
	free (x509_fqan_escape_sub);
	x509_fqan_escape_sub = tmp_scan_ptr;
	x509_fqan_escape_sub_len = strlen(x509_fqan_escape_sub);

	tmp_scan_ptr = trim_quotes(x509_fqan_delimiter);
	free (x509_fqan_delimiter);
	x509_fqan_delimiter = tmp_scan_ptr;

	tmp_scan_ptr = trim_quotes(x509_fqan_delimiter_sub);
	free (x509_fqan_delimiter_sub);
	x509_fqan_delimiter_sub = tmp_scan_ptr;
	x509_fqan_delimiter_sub_len = strlen(x509_fqan_delimiter_sub);


	// phase 1, scan the string to compute the new length
	result_string_len = 0;
	for (tmp_scan_ptr = instr; *tmp_scan_ptr; tmp_scan_ptr++) {
		if( (*tmp_scan_ptr)==x509_fqan_escape[0] ) {
			result_string_len += x509_fqan_escape_sub_len;
		} else if( (*tmp_scan_ptr)==x509_fqan_delimiter[0] ) {
			result_string_len += x509_fqan_delimiter_sub_len;
		} else {
			result_string_len++;
		}
	}

	// phase 2, process the string into the result buffer

	// malloc new string (with NULL terminator)
	result_string = (char*) malloc (result_string_len + 1);
	ASSERT( result_string );
	*result_string = 0;
	result_string_len = 0;

	for (tmp_scan_ptr = instr; *tmp_scan_ptr; tmp_scan_ptr++) {
		if( (*tmp_scan_ptr)==x509_fqan_escape[0] ) {
			strcat(&(result_string[result_string_len]), x509_fqan_escape_sub);
			result_string_len += x509_fqan_escape_sub_len;
		} else if( (*tmp_scan_ptr)==x509_fqan_delimiter[0] ) {
			strcat(&(result_string[result_string_len]), x509_fqan_delimiter_sub);
			result_string_len += x509_fqan_delimiter_sub_len;
		} else {
			result_string[result_string_len] = *tmp_scan_ptr;
			result_string_len++;
		}
		result_string[result_string_len] = 0;
	}

	// clean up
	free(x509_fqan_escape);
	free(x509_fqan_escape_sub);
	free(x509_fqan_delimiter);
	free(x509_fqan_delimiter_sub);

	return result_string;
}
#endif /* defined(HAVE_EXT_VOMS) */

#if defined(HAVE_EXT_GLOBUS)

globus_gsi_cred_handle_t x509_proxy_read( const char *proxy_file )
{
	globus_gsi_cred_handle_t         handle       = NULL;
	globus_gsi_cred_handle_attrs_t   handle_attrs = NULL;
	char *my_proxy_file = NULL;
	bool error = false;

	if ( activate_globus_gsi() != 0 ) {
		return NULL;
	}

	if ((*globus_gsi_cred_handle_attrs_init_ptr)(&handle_attrs)) {
		set_error_string( "problem during internal initialization1" );
		error = true;
		goto cleanup;
	}

	if ((*globus_gsi_cred_handle_init_ptr)(&handle, handle_attrs)) {
		set_error_string( "problem during internal initialization2" );
		error = true;
		goto cleanup;
	}

	/* Check for proxy file */
	if (proxy_file == NULL) {
		my_proxy_file = get_x509_proxy_filename();
		if (my_proxy_file == NULL) {
			goto cleanup;
		}
		proxy_file = my_proxy_file;
	}

	// We should have a proxy file, now, try to read it
	if ((*globus_gsi_cred_read_proxy_ptr)(handle, proxy_file)) {
		set_error_string( "unable to read proxy file" );
		error = true;
		goto cleanup;
	}

 cleanup:
	if (my_proxy_file) {
		free(my_proxy_file);
	}

	if (handle_attrs) {
		(*globus_gsi_cred_handle_attrs_destroy_ptr)(handle_attrs);
	}

	if (error && handle) {
		(*globus_gsi_cred_handle_destroy_ptr)(handle);
		handle = NULL;
	}

	return handle;
}

void x509_proxy_free( globus_gsi_cred_handle_t handle )
{
	if ( activate_globus_gsi() != 0 ) {
		return;
	}
	if (handle) {
		(*globus_gsi_cred_handle_destroy_ptr)(handle);
	}
}

time_t x509_proxy_expiration_time( globus_gsi_cred_handle_t handle )
{
	time_t time_left;

	if ( activate_globus_gsi() != 0 ) {
		return -1;
	}

	if ((*globus_gsi_cred_get_lifetime_ptr)(handle, &time_left)) {
		set_error_string( "unable to extract expiration time" );
		return -1;
    }

	return time(NULL) + time_left;
}

// This is a slightly modified verson of globus_gsi_cert_utils_make_time()
// from the Grid Community Toolkit. It turns an ASN1_UTCTIME into a time_t.
// libressl and older openssl don't provide a way to do this conversion.
static
time_t my_globus_gsi_cert_utils_make_time(const ASN1_UTCTIME *ctm)
{
	char *str;
	time_t offset;
	char buff1[24];
	char *p;
	int i;
	struct tm tm;
	time_t newtime = -1;

	p = buff1;
	i = ctm->length;
	str = (char *)ctm->data;
	if ((i < 11) || (i > 17)) {
		newtime = 0;
	}
	memcpy(p,str,10);
	p += 10;
	str += 10;

	if ((*str == 'Z') || (*str == '-') || (*str == '+')) {
		*(p++)='0'; *(p++)='0';
	} else {
		*(p++)= *(str++); *(p++)= *(str++);
	}
	*(p++)='Z';
	*(p++)='\0';

	if (*str == 'Z') {
		offset=0;
	} else {
		if ((*str != '+') && (str[5] != '-')) {
			newtime = 0;
		}
		offset=((str[1]-'0')*10+(str[2]-'0'))*60;
		offset+=(str[3]-'0')*10+(str[4]-'0');
		if (*str == '-') {
			offset=-offset;
		}
	}

	tm.tm_isdst = 0;
	tm.tm_year = (buff1[0]-'0')*10+(buff1[1]-'0');

	if (tm.tm_year < 70) {
		tm.tm_year+=100;
	}

	tm.tm_mon   = (buff1[2]-'0')*10+(buff1[3]-'0')-1;
	tm.tm_mday  = (buff1[4]-'0')*10+(buff1[5]-'0');
	tm.tm_hour  = (buff1[6]-'0')*10+(buff1[7]-'0');
	tm.tm_min   = (buff1[8]-'0')*10+(buff1[9]-'0');
	tm.tm_sec   = (buff1[10]-'0')*10+(buff1[11]-'0');

	newtime = (timegm(&tm) + offset*60*60);

	return newtime;
}

time_t x509_proxy_expiration_time( X509 *cert, STACK_OF(X509)* chain )
{
	time_t expiration_time = -1;
	X509 *curr_cert = cert;
	int cert_cnt = 0;
	if ( chain ) {
		cert_cnt = sk_X509_num(chain);
	}

	while ( curr_cert ) {
		time_t curr_expire = 0;
#if defined(ASN1_TIME_diff)
		int diff_days = 0;
		int diff_secs = 0;
		if ( ! ASN1_TIME_diff(&diff_days, &diff_secs, NULL, X509_get_notAfter(curr_cert)) ) {
			_globus_error_message = "Failed to calculate expration time";
			expiration_time = -1;
			break;
		} else {
			expiration_time = time(NULL) + diff_secs + 24*3600*diff_days;
		}
#else
		curr_expire = my_globus_gsi_cert_utils_make_time(X509_get_notAfter(curr_cert));
#endif
		if ( expiration_time == -1 || curr_expire < expiration_time ) {
			expiration_time = curr_expire;
		}
		if ( chain && cert_cnt ) {
			cert_cnt--;
			curr_cert = sk_X509_value(chain, cert_cnt);
		}
	}
	return expiration_time;
}

char* x509_proxy_email( globus_gsi_cred_handle_t handle )
{
	X509_NAME *email_orig = NULL;
        STACK_OF(X509) *cert_chain = NULL;
	GENERAL_NAME *gen;
	GENERAL_NAMES *gens;
        X509 *cert = NULL;
	char *email = NULL, *email2 = NULL;
	int i, j;

	if ( activate_globus_gsi() != 0 ) {
		return NULL;
	}

	if ((*globus_gsi_cred_get_cert_chain_ptr)(handle, &cert_chain)) {
		cert = NULL;
		set_error_string( "unable to find certificate in proxy" );
		goto cleanup;
	}

	for(i = 0; i < sk_X509_num(cert_chain) && email == NULL; ++i) {
		if((cert = sk_X509_value(cert_chain, i)) == NULL) {
			continue;
		}
		if ((email_orig = (X509_NAME *)X509_get_ext_d2i(cert, NID_pkcs9_emailAddress, 0, 0)) != NULL) {
			if ((email2 = X509_NAME_oneline(email_orig, NULL, 0)) == NULL) {
				continue;
			} else {
				// Return something that we can free().
				email = strdup(email2);
				OPENSSL_free(email2);
				break;
			}
		}
		gens = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0);
		if (gens) {
			for (j = 0; j < sk_GENERAL_NAME_num(gens); ++j) {
				if ((gen = sk_GENERAL_NAME_value(gens, j)) == NULL) {
					continue;
				}
				if (gen->type != GEN_EMAIL) {
					continue;
				}
				ASN1_IA5STRING *email_ia5 = gen->d.ia5;
				// Sanity checks.
				if (email_ia5->type != V_ASN1_IA5STRING) goto cleanup;
				if (!email_ia5->data || !email_ia5->length) goto cleanup;
				email2 = BUF_strdup((char *)email_ia5->data);
				// We want to return something we can free(), so make another copy.
				if (email2) {
					email = strdup(email2);
					OPENSSL_free(email2);
				}
				break;
			}
			sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
		}
	}

	if (email == NULL) {
		set_error_string( "unable to extract email" );
		goto cleanup;
	}

 cleanup:
	if (cert_chain) {
		sk_X509_pop_free(cert_chain, X509_free);
	}

	if (email_orig) {
		X509_NAME_free(email_orig);
	}

	return email;
}

char* x509_proxy_subject_name( globus_gsi_cred_handle_t handle )
{
	char *subject_name = NULL;

	if ( activate_globus_gsi() != 0 ) {
		return NULL;
	}

	if ((*globus_gsi_cred_get_subject_name_ptr)(handle, &subject_name)) {
		set_error_string( "unable to extract subject name" );
		return NULL;
	}

	return subject_name;
}

char* x509_proxy_identity_name( globus_gsi_cred_handle_t handle )
{
	char *subject_name = NULL;

	if ( activate_globus_gsi() != 0 ) {
		return NULL;
	}

	if ((*globus_gsi_cred_get_identity_name_ptr)(handle, &subject_name)) {
		set_error_string( "unable to extract identity name" );
		return NULL;
	}

	return subject_name;
}

int x509_proxy_seconds_until_expire( globus_gsi_cred_handle_t handle )
{
	time_t time_now;
	time_t time_expire;
	time_t time_diff;

	time_now = time(NULL);
	time_expire = x509_proxy_expiration_time( handle );

	if ( time_expire == -1 ) {
		return -1;
	}

	time_diff = time_expire - time_now;

	if ( time_diff < 0 ) {
		time_diff = 0;
	}

	return (int)time_diff;
}

#if !defined(HAVE_EXT_VOMS)
int
extract_VOMS_info( globus_gsi_cred_handle_t, int, char **, char **, char **)
{
	return 1;
}
#else
int
extract_VOMS_info( globus_gsi_cred_handle_t cred_handle, int verify_type, char **voname, char **firstfqan, char **quoted_DN_and_FQAN)
{


	int ret;
	struct vomsdata *voms_data = NULL;
	struct voms *voms_cert  = NULL;
	char *subject_name = NULL;
	char **fqan = NULL;
	int voms_err;
	int fqan_len = 0;
	char *retfqan = NULL;
	char *tmp_scan_ptr = NULL;

	STACK_OF(X509) *chain = NULL;
	X509 *cert = NULL;

	char* x509_fqan_delimiter = NULL;

	if ( activate_globus_gsi() != 0 ) {
		return 1;
	}

	// calling this function on something that doesn't have VOMS attributes
	// should return error 1.  when the config knob disables VOMS, behave the
	// same way.
	if (!param_boolean_int("USE_VOMS_ATTRIBUTES", 1)) {
		return 1;
	}

	ret = (*globus_gsi_cred_get_cert_chain_ptr)(cred_handle, &chain);
	if(ret != GLOBUS_SUCCESS) {
		ret = 10;
		goto end;
	}

	ret = (*globus_gsi_cred_get_cert_ptr)(cred_handle, &cert);
	if(ret != GLOBUS_SUCCESS) {
		ret = 11;
		goto end;
	}

	if ((*globus_gsi_cred_get_identity_name_ptr)(cred_handle, &subject_name)) {
		set_error_string( "unable to extract subject name" );
		ret = 12;
		goto end;
	}

	voms_data = (*VOMS_Init_ptr)(NULL, NULL);
	if (voms_data == NULL) {
		ret = 13;
		goto end;
	}

	if (verify_type == 0) {
		ret = (*VOMS_SetVerificationType_ptr)( VERIFY_NONE, voms_data, &voms_err );
		if (ret == 0) {
			(*VOMS_ErrorMessage_ptr)(voms_data, voms_err, NULL, 0);
			ret = voms_err;
			goto end;
		}
	}

	ret = (*VOMS_Retrieve_ptr)(cert, chain, RECURSE_CHAIN,
						voms_data, &voms_err);

	// If verification was requested and no extensions were returned,
	// try again without verification. If we get extensions that time,
	// then verification failed. In that case, issue a warning, then
	// act as if there were no extensions.
	if (ret == 0 && verify_type != 0 ) {
		ret = (*VOMS_SetVerificationType_ptr)( VERIFY_NONE, voms_data, &voms_err );
		if (ret == 0) {
			(*VOMS_ErrorMessage_ptr)(voms_data, voms_err, NULL, 0);
			ret = voms_err;
			goto end;
		}

		ret = (*VOMS_Retrieve_ptr)(cert, chain, RECURSE_CHAIN,
						voms_data, &voms_err);
		if (ret != 0) {
			dprintf(D_ALWAYS, "WARNING! X.509 certificate '%s' has VOMS "
					"extensions that can't be verified. Ignoring them. "
					"(To silence this warning, set USE_VOMS_ATTRIBUTES=False)\n",
					subject_name);
		}
		// Report no (verified) VOMS extensions
		ret = 1;
		goto end;
	}
	if (ret == 0) {
		if (voms_err == VERR_NOEXT) {
			// No VOMS extensions present
			ret = 1;
			goto end;
		} else {
			(*VOMS_ErrorMessage_ptr)(voms_data, voms_err, NULL, 0);
			ret = voms_err;
			goto end;
		}
	}

	// we only support one cert for now.  serializing and encoding all the
	// attributes is bad enough, i don't want to deal with doing this to
	// multiple certs.
	voms_cert = voms_data->data[0];

	if (voms_cert == NULL) {
		// No VOMS certs?? Treat like VOMS_Retrieve() returned VERR_NOEXT.
		ret = 1;
		goto end;
	}

	// fill in the unquoted versions of things
	if(voname) {
		*voname = strdup(voms_cert->voname ? voms_cert->voname : "");
	}

	if(firstfqan) {
		*firstfqan = strdup(voms_cert->fqan[0] ? voms_cert->fqan[0] : "");
	}

	// only construct the quoted_DN_and_FQAN if needed
	if (quoted_DN_and_FQAN) {
		// get our delimiter and trim it
		if (!(x509_fqan_delimiter = param("X509_FQAN_DELIMITER"))) {
			x509_fqan_delimiter = strdup(",");
		}
		tmp_scan_ptr = trim_quotes(x509_fqan_delimiter);
		free(x509_fqan_delimiter);
		x509_fqan_delimiter = tmp_scan_ptr;

		// calculate the length
		fqan_len = 0;

		// start with the length of the quoted DN
		tmp_scan_ptr = quote_x509_string( subject_name );
		fqan_len += strlen( tmp_scan_ptr );
		free(tmp_scan_ptr);

		// add the length of delimiter plus each voms attribute
		for (fqan = voms_cert->fqan; fqan && *fqan; fqan++) {
			// delimiter
			fqan_len += strlen(x509_fqan_delimiter);

			tmp_scan_ptr = quote_x509_string( *fqan );
			fqan_len += strlen( tmp_scan_ptr );
			free(tmp_scan_ptr);
		}

		// now malloc enough room for the quoted DN, quoted attrs, delimiters, and
		// NULL terminator
		retfqan = (char*) malloc (fqan_len+1);
		*retfqan = 0;  // set null terminiator

		// reset length counter -- we use this for efficient appending.
		fqan_len = 0;

		// start with the quoted DN
		tmp_scan_ptr = quote_x509_string( subject_name );
		strcat(retfqan, tmp_scan_ptr);
		fqan_len += strlen( tmp_scan_ptr );
		free(tmp_scan_ptr);

		// add the delimiter plus each voms attribute
		for (fqan = voms_cert->fqan; fqan && *fqan; fqan++) {
			// delimiter
			strcat(&(retfqan[fqan_len]), x509_fqan_delimiter);
			fqan_len += strlen(x509_fqan_delimiter);

			tmp_scan_ptr = quote_x509_string( *fqan );
			strcat(&(retfqan[fqan_len]), tmp_scan_ptr);
			fqan_len += strlen( tmp_scan_ptr );
			free(tmp_scan_ptr);
		}

		*quoted_DN_and_FQAN = retfqan;
	}

	ret = 0;

end:
	free(subject_name);
	free(x509_fqan_delimiter);
	if (voms_data)
		(*VOMS_Destroy_ptr)(voms_data);
	if (cert)
		X509_free(cert);
	if(chain)
		sk_X509_pop_free(chain, X509_free);

	return ret;
}
#endif


int
extract_VOMS_info_from_file( const char* proxy_file, int verify_type, char **voname, char **firstfqan, char **quoted_DN_and_FQAN)
{

	globus_gsi_cred_handle_t         handle       = NULL;
	globus_gsi_cred_handle_attrs_t   handle_attrs = NULL;
	char *my_proxy_file = NULL;
	int error = 0;

	if ( activate_globus_gsi() != 0 ) {
		return 2;
	}

	if ((*globus_gsi_cred_handle_attrs_init_ptr)(&handle_attrs)) {
		set_error_string( "problem during internal initialization1" );
		error = 3;
		goto cleanup;
	}

	if ((*globus_gsi_cred_handle_init_ptr)(&handle, handle_attrs)) {
		set_error_string( "problem during internal initialization2" );
		error = 4;
		goto cleanup;
	}

	/* Check for proxy file */
	if (proxy_file == NULL) {
		my_proxy_file = get_x509_proxy_filename();
		if (my_proxy_file == NULL) {
			error = 5;
			goto cleanup;
		}
		proxy_file = my_proxy_file;
	}

	// We should have a proxy file, now, try to read it
	if ((*globus_gsi_cred_read_proxy_ptr)(handle, proxy_file)) {
		set_error_string( "unable to read proxy file" );
		error = 6;
		goto cleanup;
	}

	error = extract_VOMS_info( handle, verify_type, voname, firstfqan, quoted_DN_and_FQAN );


 cleanup:
	if (my_proxy_file) {
		free(my_proxy_file);
	}

	if (handle_attrs) {
		(*globus_gsi_cred_handle_attrs_destroy_ptr)(handle_attrs);
	}

	if (handle) {
		(*globus_gsi_cred_handle_destroy_ptr)(handle);
	}

	return error; // success

}
#endif /* defined(HAVE_EXT_GLOBUS) */

/* Return the email of a given proxy cert. 
  On error, return NULL.
  On success, return a pointer to a null-terminated string.
  IT IS THE CALLER'S RESPONSBILITY TO DE-ALLOCATE THE STIRNG
  WITH free().  
 */             
char *
x509_proxy_email( const char *proxy_file )
{
#if !defined(HAVE_EXT_GLOBUS)
	(void) proxy_file;
	set_error_string( NOT_SUPPORTED_MSG );
	return NULL;
#else

	char *email = NULL;
	globus_gsi_cred_handle_t proxy_handle = x509_proxy_read( proxy_file );

	if ( proxy_handle == NULL ) {
		return NULL;
	}

	email = x509_proxy_email( proxy_handle );

	x509_proxy_free( proxy_handle );

	return email;
#endif /* !defined(HAVE_EXT_GLOBUS) */
}

/* Return the subject name of a given proxy cert. 
  On error, return NULL.
  On success, return a pointer to a null-terminated string.
  IT IS THE CALLER'S RESPONSBILITY TO DE-ALLOCATE THE STIRNG
  WITH free().
 */
char *
x509_proxy_subject_name( const char *proxy_file )
{
#if !defined(HAVE_EXT_GLOBUS)
	(void) proxy_file;
	set_error_string( NOT_SUPPORTED_MSG );
	return NULL;
#else
	char *subject_name = NULL;
	globus_gsi_cred_handle_t proxy_handle = x509_proxy_read( proxy_file );

	if ( proxy_handle == NULL ) {
		return NULL;
	}

	subject_name = x509_proxy_subject_name( proxy_handle );

	x509_proxy_free( proxy_handle );

	return subject_name;

#endif /* !defined(HAVE_EXT_GLOBUS) */
}


/* Return the identity name of a given X509 cert. For proxy certs, this
  will return the identity that the proxy can act on behalf of, rather than
  the subject of the proxy cert itself. Normally, this will have the
  practical effect of stripping off any "/CN=proxy" strings from the subject
  name.
  On error, return NULL.
  On success, return a pointer to a null-terminated string.
  IT IS THE CALLER'S RESPONSBILITY TO DE-ALLOCATE THE STIRNG
  WITH free().
 */
char *
x509_proxy_identity_name( const char *proxy_file )
{
#if !defined(HAVE_EXT_GLOBUS)
	(void) proxy_file;
	set_error_string( NOT_SUPPORTED_MSG );
	return NULL;
#else

	char *subject_name = NULL;
	globus_gsi_cred_handle_t proxy_handle = x509_proxy_read( proxy_file );

	if ( proxy_handle == NULL ) {
		return NULL;
	}

	subject_name = x509_proxy_identity_name( proxy_handle );

	x509_proxy_free( proxy_handle );

	return subject_name;

#endif /* !defined(GSS_AUTHENTICATION) */
}

/* Return the time at which the proxy expires. On error, return -1.
 */
time_t
x509_proxy_expiration_time( const char *proxy_file )
{
#if !defined(HAVE_EXT_GLOBUS)
	(void) proxy_file;
	set_error_string( NOT_SUPPORTED_MSG );
	return -1;
#else

	time_t expiration_time = -1;
	globus_gsi_cred_handle_t proxy_handle = x509_proxy_read( proxy_file );

	if ( proxy_handle == NULL ) {
		return -1;
	}

	expiration_time = x509_proxy_expiration_time( proxy_handle );

	x509_proxy_free( proxy_handle );

	return expiration_time;

#endif /* !defined(GSS_AUTHENTICATION) */
}

/* Return the number of seconds until the supplied proxy
 * file will expire.  
 * On error, return -1.    - Todd <tannenba@cs.wisc.edu>
 */
int
x509_proxy_seconds_until_expire( const char *proxy_file )
{
#if !defined(HAVE_EXT_GLOBUS)
	(void) proxy_file;
	set_error_string( NOT_SUPPORTED_MSG );
	return -1;
#else

	time_t time_now;
	time_t time_expire;
	time_t time_diff;

	time_now = time(NULL);
	time_expire = x509_proxy_expiration_time( proxy_file );

	if ( time_expire == -1 ) {
		return -1;
	}

	time_diff = time_expire - time_now;

	if ( time_diff < 0 ) {
		time_diff = 0;
	}

	return (int)time_diff;

#endif /* !defined(GSS_AUTHENTICATION) */
}

static int
buffer_to_bio( char *buffer, size_t buffer_len, BIO **bio )
{
	if ( buffer == NULL ) {
		return FALSE;
	}

	*bio = BIO_new( BIO_s_mem() );
	if ( *bio == NULL ) {
		return FALSE;
	}

	if ( BIO_write( *bio, buffer, buffer_len ) < (int)buffer_len ) {
		BIO_free( *bio );
		return FALSE;
	}

	return TRUE;
}

static int
bio_to_buffer( BIO *bio, char **buffer, size_t *buffer_len )
{
	if ( bio == NULL ) {
		return FALSE;
	}

	*buffer_len = BIO_pending( bio );

	*buffer = (char *)malloc( *buffer_len );
	if ( *buffer == NULL ) {
		return FALSE;
	}

	if ( BIO_read( bio, *buffer, *buffer_len ) < (int)*buffer_len ) {
		free( *buffer );
		return FALSE;
	}

	return TRUE;
}

int
x509_send_delegation( const char *source_file,
					  time_t expiration_time,
					  time_t *result_expiration_time,
					  int (*recv_data_func)(void *, void **, size_t *), 
					  void *recv_data_ptr,
					  int (*send_data_func)(void *, void *, size_t),
					  void *send_data_ptr )
{
	int rc = -1;
	char *buffer = nullptr;
	size_t buffer_len = 0;
	BIO *in_bio = nullptr;
	BIO *out_bio = nullptr;
	X509 *src_cert = nullptr;
	STACK_OF(X509) *src_chain = nullptr;
	bool did_recv = false;
	bool did_send = false;
	DelegationRestrictions restrict;
	DelegationProvider deleg_provider(source_file, "");

	did_recv = true;
	if ( recv_data_func( recv_data_ptr, (void **)&buffer, &buffer_len ) != 0 || buffer == NULL ) {
		_globus_error_message = "Failed to receive delegation request";
		goto cleanup;
	}

	if ( buffer_to_bio( buffer, buffer_len, &in_bio ) == FALSE ) {
		_globus_error_message = "buffer_to_bio() failed";
		goto cleanup;
	}

	free(buffer);
	buffer = nullptr;

	if ( ! param_boolean("DELEGATE_FULL_JOB_GSI_CREDENTIALS", false) ) {
		restrict["policyLimited"] = "true";
	}

	src_cert = deleg_provider.GetSrcCert();
	src_chain = deleg_provider.GetSrcChain();
	if ( ! src_cert ) {
		_globus_error_message = "Failed to read proxy file";
		goto cleanup;
	}

	if ( expiration_time || result_expiration_time ) {
		time_t orig_expiration_time = x509_proxy_expiration_time(src_cert, src_chain);

		if( expiration_time && orig_expiration_time > expiration_time ) {
			restrict["validityEnd"] = std::to_string(expiration_time);
		}

		if( result_expiration_time ) {
			*result_expiration_time = expiration_time;
		}
	}

	out_bio = deleg_provider.Delegate(in_bio, restrict);
	if ( ! out_bio ) {
		_globus_error_message = "DelegationProvider::Delegate() failed";
		goto cleanup;
	}

	if ( bio_to_buffer( out_bio, &buffer, &buffer_len ) == FALSE ) {
		_globus_error_message = "bio_to_buffer() failed";
		goto cleanup;
	}

	did_send = true;
	if ( send_data_func( send_data_ptr, buffer, buffer_len ) != 0 ) {
		_globus_error_message = "Failed to send delegated proxy";
		goto cleanup;
	}

	rc = 0;

 cleanup:
	if ( ! did_recv ) {
		recv_data_func( recv_data_ptr, (void **)&buffer, &buffer_len );
	}
	if ( ! did_send ) {
		send_data_func( send_data_ptr, NULL, 0 );
	}
	if ( buffer ) {
		free( buffer );
	}
	if ( in_bio ) {
		BIO_free( in_bio );
	}
	if ( out_bio ) {
		BIO_free( out_bio );
	}

	return rc;
}


struct x509_delegation_state
{
	std::string m_dest;
	DelegationConsumer m_deleg_consumer;
};


int
x509_receive_delegation( const char *destination_file,
						 int (*recv_data_func)(void *, void **, size_t *), 
						 void *recv_data_ptr,
						 int (*send_data_func)(void *, void *, size_t),
						 void *send_data_ptr,
						 void ** state_ptr )
{
	int rc = -1;
	x509_delegation_state *st = new x509_delegation_state();
	st->m_dest = destination_file;
	char *buffer = NULL;
	size_t buffer_len = 0;
	BIO *bio = NULL;
	bool did_send = false;

		// make cert request
	bio = BIO_new( BIO_s_mem() );
	if ( bio == nullptr ) {
		_globus_error_message = "BIO_new() failed";
		goto cleanup;
	}

	if ( ! st->m_deleg_consumer.Request(bio) ) {
		_globus_error_message = "DelegationConsumer::Request() failed";
		goto cleanup;
	}

	if ( bio_to_buffer( bio, &buffer, &buffer_len ) == FALSE ) {
		_globus_error_message = "bio_to_buffer() failed";
		goto cleanup;
	}

	did_send = true;
	if ( send_data_func( send_data_ptr, buffer, buffer_len ) != 0 ) {
		_globus_error_message = "Failed to send delegation request";
		goto cleanup;
	}

	rc = 0;

 cleanup:
	if ( !did_send ) {
		send_data_func( send_data_ptr, NULL, 0 );
	}
	if ( bio ) {
		BIO_free( bio );
	}
	if ( buffer ) {
		free( buffer );
	}

	// Error!  Cleanup memory immediately and return.
	if ( rc ) {
		delete st;
		return rc;
	}

	// We were given a state pointer - caller will take care of monitoring the
	// socket for more data and call delegation_finish later.
	if (state_ptr != NULL) {
		*state_ptr = st;
		return 2;
	}

	// Else, we block and finish up immediately.
	return x509_receive_delegation_finish(recv_data_func, recv_data_ptr, st);
}


// Finish up the delegation operation, waiting for data on the socket if necessary.
// NOTE: get_x509_delegation_finish will take ownership of state_ptr and free its
// memory.
int x509_receive_delegation_finish(int (*recv_data_func)(void *, void **, size_t *),
                               void *recv_data_ptr,
                               void *state_ptr_raw)
{
	int rc = -1;
	x509_delegation_state *state_ptr = static_cast<x509_delegation_state*>(state_ptr_raw);
	char *buffer = nullptr;
	size_t buffer_len = 0;
	BIO *bio = nullptr;
	std::string proxy_contents;
	std::string proxy_subject;
	int fd = -1;

	if ( recv_data_func( recv_data_ptr, (void **)&buffer, &buffer_len ) != 0 || buffer == NULL ) {
		_globus_error_message = "Failed to receive delegated proxy";
		goto cleanup;
	}

	if ( buffer_to_bio( buffer, buffer_len, &bio ) == FALSE ) {
		_globus_error_message = "buffer_to_bio() failed";
		goto cleanup;
	}

	if ( ! state_ptr->m_deleg_consumer.Acquire(bio, proxy_contents, proxy_subject) ) {
		_globus_error_message = "DelegationConsumer::Acquire() failed";
		goto cleanup;
	}

		// write proxy file
		// safe_open O_WRONLY|O_EXCL|O_CREAT, S_IRUSR|S_IWUSR
	fd = safe_open_wrapper_follow(state_ptr->m_dest.c_str(), O_WRONLY|O_EXCL|O_CREAT, S_IRUSR|S_IWUSR);
	if ( fd < 0 ) {
		_globus_error_message = "Failed to open proxy file";
		goto cleanup;
	}

	if ( write(fd, proxy_contents.c_str(), proxy_contents.size()) < (ssize_t)proxy_contents.size() ) {
		_globus_error_message = "Failed to write proxy file";
		goto cleanup;
	}

	rc = 0;

 cleanup:
	if ( bio ) {
		BIO_free( bio );
	}
	if ( buffer ) {
		free( buffer );
	}
	if ( state_ptr ) {
		delete state_ptr;
	}
	if ( fd >= 0 ) {
		close(fd);
	}

	return rc;
}

void parse_resource_manager_string( const char *string, char **host,
									char **port, char **service,
									char **subject )
{
	char *p;
	char *q;
	size_t len = strlen( string );

	char *my_host = (char *)calloc( len+1, sizeof(char) );
	char *my_port = (char *)calloc( len+1, sizeof(char) );
	char *my_service = (char *)calloc( len+1, sizeof(char) );
	char *my_subject = (char *)calloc( len+1, sizeof(char) );
	ASSERT( my_host && my_port && my_service && my_subject );

	p = my_host;
	q = my_host;

	while ( *string != '\0' ) {
		if ( *string == ':' ) {
			if ( q == my_host ) {
				p = my_port;
				q = my_port;
				string++;
			} else if ( q == my_port || q == my_service ) {
				p = my_subject;
				q = my_subject;
				string++;
			} else {
				*(p++) = *(string++);
			}
		} else if ( *string == '/' ) {
			if ( q == my_host || q == my_port ) {
				p = my_service;
				q = my_service;
				string++;
			} else {
				*(p++) = *(string++);
			}
		} else {
			*(p++) = *(string++);
		}
	}

	if ( host != NULL ) {
		*host = my_host;
	} else {
		free( my_host );
	}

	if ( port != NULL ) {
		*port = my_port;
	} else {
		free( my_port );
	}

	if ( service != NULL ) {
		*service = my_service;
	} else {
		free( my_service );
	}

	if ( subject != NULL ) {
		*subject = my_subject;
	} else {
		free( my_subject );
	}
}

/* Returns true (non-0) if path looks like an URL that Globus
   (specifically, globus-url-copy) can handle

   Expected use: is the input/stdout file actually a Globus URL
   that we can just hand off to Globus instead of a local file
   that we need to rewrite as a Globus URL.

   Probably doesn't make sense to use if universe != globus
*/
int
is_globus_friendly_url(const char * path)
{
	if(path == 0)
		return 0;
	// Should this be more aggressive and allow anything with ://?
	return 
		strstr(path, "http://") == path ||
		strstr(path, "https://") == path ||
		strstr(path, "ftp://") == path ||
		strstr(path, "gsiftp://") == path ||
		0;
}
