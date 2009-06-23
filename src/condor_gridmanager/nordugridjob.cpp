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
#include "condor_attributes.h"
#include "condor_debug.h"
#include "condor_string.h"	// for strnewp and friends
#include "condor_daemon_core.h"
#include "basename.h"
#include "condor_ckpt_name.h"
#include "nullfile.h"
#include "filename_tools.h"
#include "condor_url.h"

#include "globus_utils.h"
#include "gridmanager.h"
#include "nordugridjob.h"
#include "condor_config.h"
#include "globusjob.h" // for rsl_stringify()


// GridManager job states
#define GM_INIT					0
#define GM_UNSUBMITTED			1
#define GM_SUBMIT				2
#define GM_SUBMIT_SAVE			3
#define GM_SUBMITTED			4
#define GM_DONE_SAVE			5
#define GM_DONE_COMMIT			6
#define GM_CANCEL				7
#define GM_FAILED				8
#define GM_DELETE				9
#define GM_CLEAR_REQUEST		10
#define GM_HOLD					11
#define GM_PROBE_JOB			12
#define GM_STAGE_IN				13
#define GM_STAGE_OUT			14
#define GM_EXIT_INFO			15
#define GM_RECOVER_QUERY		16
#define GM_START				17

static const char *GMStateNames[] = {
	"GM_INIT",
	"GM_UNSUBMITTED",
	"GM_SUBMIT",
	"GM_SUBMIT_SAVE",
	"GM_SUBMITTED",
	"GM_DONE_SAVE",
	"GM_DONE_COMMIT",
	"GM_CANCEL",
	"GM_FAILED",
	"GM_DELETE",
	"GM_CLEAR_REQUEST",
	"GM_HOLD",
	"GM_PROBE_JOB",
	"GM_STAGE_IN",
	"GM_STAGE_OUT",
	"GM_EXIT_INFO",
	"GM_RECOVER_QUERY",
	"GM_START"
};

#define REMOTE_STATE_ACCEPTING		"ACCEPTING"
#define REMOTE_STATE_ACCEPTED		"ACCEPTED"
#define REMOTE_STATE_PREPARING		"PREPARING"
#define REMOTE_STATE_PREPARED		"PREPARED"
#define REMOTE_STATE_SUBMITTING		"SUBMITTING"
#define REMOTE_STATE_INLRMS_R		"INLRMS: R"
#define REMOTE_STATE_INLRMS_Q		"INLRMS: Q"
#define REMOTE_STATE_INLRMS_S		"INLRMS: S"
#define REMOTE_STATE_INLRMS_E		"INLRMS: E"
#define REMOTE_STATE_INLRMS_O		"INLRMS: O"
#define REMOTE_STATE_KILLING		"KILLING"
#define REMOTE_STATE_EXECUTED		"EXECUTED"
#define REMOTE_STATE_FINISHING		"FINISHING"
#define REMOTE_STATE_FINISHED		"FINISHED"
#define REMOTE_STATE_FAILED			"FAILED"
#define REMOTE_STATE_KILLED			"KILLED"
#define REMOTE_STATE_DELETED		"DELETED"

#define REMOTE_STDOUT_NAME	"_condor_stdout"
#define REMOTE_STDERR_NAME	"_condor_stderr"

// Filenames are case insensitive on Win32, but case sensitive on Unix
#ifdef WIN32
#	define file_strcmp _stricmp
#	define file_contains contains_anycase
#else
#	define file_strcmp strcmp
#	define file_contains contains
#endif

#define NORDUGRID_LOG_DIR ".nordugrid_log"

// TODO: Let the maximum submit attempts be set in the job ad or, better yet,
// evalute PeriodicHold expression in job ad.
#define MAX_SUBMIT_ATTEMPTS	1

void NordugridJobInit()
{
}

void NordugridJobReconfig()
{
	int tmp_int;

	tmp_int = param_integer( "GRIDMANAGER_JOB_PROBE_INTERVAL", 5 * 60 );
	NordugridJob::setProbeInterval( tmp_int );

	tmp_int = param_integer( "GRIDMANAGER_GAHP_CALL_TIMEOUT", 5 * 60 );
	NordugridJob::setGahpCallTimeout( tmp_int );

	// Tell all the resource objects to deal with their new config values
	NordugridResource *next_resource;

	NordugridResource::ResourcesByName.startIterations();

	while ( NordugridResource::ResourcesByName.iterate( next_resource ) != 0 ) {
		next_resource->Reconfig();
	}
}

bool NordugridJobAdMatch( const ClassAd *job_ad ) {
	int universe;
	MyString resource;
	if ( job_ad->LookupInteger( ATTR_JOB_UNIVERSE, universe ) &&
		 universe == CONDOR_UNIVERSE_GRID &&
		 job_ad->LookupString( ATTR_GRID_RESOURCE, resource ) &&
		 strncasecmp( resource.Value(), "nordugrid ", 10 ) == 0 ) {

		return true;
	}
	return false;
}

BaseJob *NordugridJobCreate( ClassAd *jobad )
{
	return (BaseJob *)new NordugridJob( jobad );
}

int NordugridJob::probeInterval = 300;	// default value
int NordugridJob::submitInterval = 300;	// default value
int NordugridJob::gahpCallTimeout = 300;	// default value
int NordugridJob::maxConnectFailures = 3;	// default value

NordugridJob::NordugridJob( ClassAd *classad )
	: BaseJob( classad )
{
	char buff[4096];
	MyString error_string = "";
	char *gahp_path = NULL;

	remoteJobId = NULL;
	remoteJobState = "";
	gmState = GM_INIT;
	lastProbeTime = 0;
	probeNow = false;
	enteredCurrentGmState = time(NULL);
	lastSubmitAttempt = 0;
	numSubmitAttempts = 0;
	resourceManagerString = NULL;
	jobProxy = NULL;
	myResource = NULL;
	gahp = NULL;
	RSL = NULL;
	stageList = NULL;
	stageLocalList = NULL;

	// In GM_HOLD, we assume HoldReason to be set only if we set it, so make
	// sure it's unset when we start (unless the job is already held).
	if ( condorState != HELD &&
		 jobAd->LookupString( ATTR_HOLD_REASON, NULL, 0 ) != 0 ) {

		jobAd->AssignExpr( ATTR_HOLD_REASON, "Undefined" );
	}

	jobProxy = AcquireProxy( jobAd, error_string, evaluateStateTid );
	if ( jobProxy == NULL ) {
		if ( error_string == "" ) {
			error_string.sprintf( "%s is not set in the job ad",
								  ATTR_X509_USER_PROXY );
		}
		goto error_exit;
	}

	gahp_path = param( "NORDUGRID_GAHP" );
	if ( gahp_path == NULL ) {
		error_string = "NORDUGRID_GAHP not defined";
		goto error_exit;
	}
	snprintf( buff, sizeof(buff), "NORDUGRID/%s",
			  jobProxy->subject->subject_name );
	gahp = new GahpClient( buff, gahp_path );
	gahp->setNotificationTimerId( evaluateStateTid );
	gahp->setMode( GahpClient::normal );
	gahp->setTimeout( gahpCallTimeout );

	free( gahp_path );
	gahp_path = NULL;

	buff[0] = '\0';
	jobAd->LookupString( ATTR_GRID_RESOURCE, buff );
	if ( buff[0] != '\0' ) {
		const char *token;
		MyString str = buff;

		str.Tokenize();

		token = str.GetNextToken( " ", false );
		if ( !token || stricmp( token, "nordugrid" ) ) {
			error_string.sprintf( "%s not of type nordugrid",
								  ATTR_GRID_RESOURCE );
			goto error_exit;
		}

		token = str.GetNextToken( " ", false );
		if ( token && *token ) {
			resourceManagerString = strdup( token );
		} else {
			error_string.sprintf( "%s missing server name",
								  ATTR_GRID_RESOURCE );
			goto error_exit;
		}

	} else {
		error_string.sprintf( "%s is not set in the job ad",
							  ATTR_GRID_RESOURCE );
		goto error_exit;
	}

	myResource = NordugridResource::FindOrCreateResource( resourceManagerString,
														  jobProxy->subject->subject_name );
	myResource->RegisterJob( this );

	buff[0] = '\0';
	jobAd->LookupString( ATTR_GRID_JOB_ID, buff );
	if ( strrchr( buff, ' ' ) ) {
		SetRemoteJobId( strrchr( buff, ' ' ) + 1 );
		myResource->AlreadySubmitted( this );
	} else {
		SetRemoteJobId( NULL );
	}

	jobAd->LookupString( ATTR_GRID_JOB_STATUS, remoteJobState );

	return;

 error_exit:
	gmState = GM_HOLD;
	if ( !error_string.IsEmpty() ) {
		jobAd->Assign( ATTR_HOLD_REASON, error_string.Value() );
	}
	return;
}

NordugridJob::~NordugridJob()
{
	if ( jobProxy != NULL ) {
		ReleaseProxy( jobProxy, evaluateStateTid );
	}
	if ( myResource ) {
		myResource->UnregisterJob( this );
	}
	if ( remoteJobId ) {
		free( remoteJobId );
	}
	if ( gahp != NULL ) {
		delete gahp;
	}
	if ( RSL ) {
		delete RSL;
	}
	if ( stageList ) {
		delete stageList;
	}
	if ( stageLocalList ) {
		delete stageLocalList;
	}
}

void NordugridJob::Reconfig()
{
	BaseJob::Reconfig();
	gahp->setTimeout( gahpCallTimeout );
}

int NordugridJob::doEvaluateState()
{
	int old_gm_state;
	bool reevaluate_state = true;
	time_t now = time(NULL);

	bool done;
	int rc;

	daemonCore->Reset_Timer( evaluateStateTid, TIMER_NEVER );

    dprintf(D_ALWAYS,
			"(%d.%d) doEvaluateState called: gmState %s, condorState %d\n",
			procID.cluster,procID.proc,GMStateNames[gmState],condorState);

	if ( gahp ) {
		if ( !resourceStateKnown || resourcePingPending || resourceDown ) {
			gahp->setMode( GahpClient::results_only );
		} else {
			gahp->setMode( GahpClient::normal );
		}
	}

	do {
		reevaluate_state = false;
		old_gm_state = gmState;

		switch ( gmState ) {
		case GM_INIT: {
			// This is the state all jobs start in when the GlobusJob object
			// is first created. Here, we do things that we didn't want to
			// do in the constructor because they could block (the
			// constructor is called while we're connected to the schedd).
			if ( gahp->Startup() == false ) {
				dprintf( D_ALWAYS, "(%d.%d) Error starting GAHP\n",
						 procID.cluster, procID.proc );

				jobAd->Assign( ATTR_HOLD_REASON, "Failed to start GAHP" );
				gmState = GM_HOLD;
				break;
			}
			if ( gahp->Initialize( jobProxy ) == false ) {
				dprintf( D_ALWAYS, "(%d.%d) Error initializing GAHP\n",
						 procID.cluster, procID.proc );

				jobAd->Assign( ATTR_HOLD_REASON,
							   "Failed to initialize GAHP" );
				gmState = GM_HOLD;
				break;
			}

			gahp->setDelegProxy( jobProxy );

			gmState = GM_START;
			} break;
		case GM_START: {
			errorString = "";
			if ( remoteJobId == NULL ) {
				gmState = GM_CLEAR_REQUEST;
			} else {
				submitLogged = true;
				if ( condorState == RUNNING ||
					 condorState == COMPLETED ) {
					executeLogged = true;
				}

				if ( remoteJobState == "" ||
					 remoteJobState == REMOTE_STATE_ACCEPTING ||
					 remoteJobState == REMOTE_STATE_ACCEPTED ||
					 remoteJobState == REMOTE_STATE_PREPARING ) {
					gmState = GM_RECOVER_QUERY;
				} else {
					gmState = GM_SUBMITTED;
				}
			}
			} break;
		case GM_RECOVER_QUERY: {
			if ( condorState == REMOVED || condorState == HELD ) {
				gmState = GM_CANCEL;
			} else {
				if ( m_lastRemoteStatusUpdate > enteredCurrentGmState ) {
					if ( remoteJobState == REMOTE_STATE_ACCEPTING ||
						 remoteJobState == REMOTE_STATE_ACCEPTED ||
						 remoteJobState == REMOTE_STATE_PREPARING ) {
						gmState = GM_STAGE_IN;
					} else {
						gmState = GM_SUBMITTED;
					}
				} else if ( m_currentStatusUnknown ) {
					gmState = GM_CANCEL;
				}
			}
			} break;
		case GM_UNSUBMITTED: {
			if ( condorState == REMOVED ) {
				gmState = GM_DELETE;
			} else if ( condorState == HELD ) {
				gmState = GM_DELETE;
				break;
			} else {
				gmState = GM_SUBMIT;
			}
			} break;
		case GM_SUBMIT: {
			if ( condorState == REMOVED || condorState == HELD ) {
				myResource->CancelSubmit( this );
				gmState = GM_UNSUBMITTED;
				break;
			}
			if ( numSubmitAttempts >= MAX_SUBMIT_ATTEMPTS ) {
//				jobAd->Assign( ATTR_HOLD_REASON,
//							   "Attempts to submit failed" );
				gmState = GM_HOLD;
				break;
			}
			// After a submit, wait at least submitInterval before trying
			// another one.
			if ( now >= lastSubmitAttempt + submitInterval ) {

				char *job_id = NULL;

				// Once RequestSubmit() is called at least once, you must
				// CancelRequest() once you're done with the request call
				if ( myResource->RequestSubmit( this ) == false ) {
					break;
				}

				if ( RSL == NULL ) {
					RSL = buildSubmitRSL();
				}
				if ( RSL == NULL ) {
					gmState = GM_HOLD;
					break;
				}
				rc = gahp->nordugrid_submit( 
										resourceManagerString,
										RSL->Value(),
										job_id );
				if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
					 rc == GAHPCLIENT_COMMAND_PENDING ) {
					break;
				}

				lastSubmitAttempt = time(NULL);
				numSubmitAttempts++;

				if ( rc == 0 ) {
					ASSERT( job_id != NULL );
					SetRemoteJobId( job_id );
					free( job_id );
					WriteGridSubmitEventToUserLog( jobAd );
					gmState = GM_SUBMIT_SAVE;
				} else {
					errorString = gahp->getErrorString();
					dprintf(D_ALWAYS,"(%d.%d) job submit failed: %s\n",
							procID.cluster, procID.proc,
							errorString.Value() );
					myResource->CancelSubmit( this );
					gmState = GM_UNSUBMITTED;
				}

			} else {
				unsigned int delay = 0;
				if ( (lastSubmitAttempt + submitInterval) > now ) {
					delay = (lastSubmitAttempt + submitInterval) - now;
				}				
				daemonCore->Reset_Timer( evaluateStateTid, delay );
			}
			} break;
		case GM_SUBMIT_SAVE: {
			if ( condorState == REMOVED || condorState == HELD ) {
				gmState = GM_CANCEL;
			} else {
				done = requestScheddUpdate( this );
				if ( !done ) {
					break;
				}
				gmState = GM_STAGE_IN;
			}
			} break;
		case GM_STAGE_IN: {
			if ( stageList == NULL ) {
				const char *file;
				stageList = buildStageInList();
				stageList->rewind();
				while ( (file = stageList->next()) ) {
					if ( IsUrl( file ) ) {
						stageList->deleteCurrent();
					}
				}
			}
			rc = gahp->nordugrid_stage_in( resourceManagerString, remoteJobId,
										   *stageList );
			if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
				 rc == GAHPCLIENT_COMMAND_PENDING ) {
				break;
			} else if ( rc != 0 ) {
				errorString = gahp->getErrorString();
				dprintf( D_ALWAYS, "(%d.%d) file stage in failed: %s\n",
						 procID.cluster, procID.proc, errorString.Value() );
				gmState = GM_CANCEL;
			} else {
				gmState = GM_SUBMITTED;
			}
			} break;
		case GM_SUBMITTED: {
			if ( remoteJobState == REMOTE_STATE_FINISHED ||
				 remoteJobState == REMOTE_STATE_FAILED ||
				 remoteJobState == REMOTE_STATE_KILLED ||
				 remoteJobState == REMOTE_STATE_DELETED ) {
					gmState = GM_EXIT_INFO;
			} else if ( condorState == REMOVED || condorState == HELD ) {
				gmState = GM_CANCEL;
			} else {
				if ( lastProbeTime < enteredCurrentGmState ) {
					lastProbeTime = enteredCurrentGmState;
				}
				if ( probeNow ) {
					lastProbeTime = 0;
					probeNow = false;
				}
/*
				if ( now >= lastProbeTime + probeInterval ) {
					gmState = GM_PROBE_JOB;
					break;
				}
				unsigned int delay = 0;
				if ( (lastProbeTime + probeInterval) > now ) {
					delay = (lastProbeTime + probeInterval) - now;
				}				
				daemonCore->Reset_Timer( evaluateStateTid, delay );
*/
			}
			} break;
		case GM_PROBE_JOB: {
			if ( condorState == REMOVED || condorState == HELD ) {
				gmState = GM_CANCEL;
			} else {
				char *new_status = NULL;
				rc = gahp->nordugrid_status( resourceManagerString,
											 remoteJobId, new_status );
				if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
					 rc == GAHPCLIENT_COMMAND_PENDING ) {
					break;
				} else if ( rc != 0 ) {
					// What to do about failure?
					errorString = gahp->getErrorString();
					dprintf( D_ALWAYS, "(%d.%d) job probe failed: %s\n",
							 procID.cluster, procID.proc,
							 errorString.Value() );
				} else {
					remoteJobState = new_status;
					SetRemoteJobStatus( new_status );
				}
				if ( new_status ) {
					free( new_status );
				}
				lastProbeTime = now;
				gmState = GM_SUBMITTED;
			}
			} break;
		case GM_EXIT_INFO: {
			MyString filter;
			StringList reply;
			filter.sprintf( "nordugrid-job-globalid=gsiftp://%s:2811/jobs/%s",
							resourceManagerString, remoteJobId );

			rc = gahp->nordugrid_ldap_query( resourceManagerString, "mds-vo-name=local,o=grid", filter.Value(), "nordugrid-job-usedcputime,nordugrid-job-usedwalltime,nordugrid-job-exitcode", reply );
			if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
				 rc == GAHPCLIENT_COMMAND_PENDING ) {
				break;
			} else if ( rc != 0 ) {
				errorString = gahp->getErrorString();
				dprintf( D_ALWAYS, "(%d.%d) exit info gathering failed: %s\n",
						 procID.cluster, procID.proc, errorString.Value() );
				gmState = GM_CANCEL;
			} else {
				int exit_code = -1;
				int wallclock = -1;
				int cpu = -1;
				const char *entry;
				reply.rewind();
				while ( (entry = reply.next()) ) {
					if ( !strncmp( entry, "nordugrid-job-usedcputime: ", 27 ) ) {
						entry = strchr( entry, ' ' ) + 1;
						cpu = atoi( entry );
					} else if ( !strncmp( entry, "nordugrid-job-usedwalltime: ", 28 ) ) {
						entry = strchr( entry, ' ' ) + 1;
						wallclock = atoi( entry );
					} else if ( !strncmp( entry, "nordugrid-job-exitcode: ", 24 ) ) {
						entry = strchr( entry, ' ' ) + 1;
						exit_code = atoi( entry );
					}
				}
				if ( exit_code < 0 || wallclock < 0 || cpu < 0 ) {
					dprintf( D_ALWAYS, "(%d.%d) exit info missing\n",
							 procID.cluster, procID.proc );
					gmState = GM_CANCEL;
					break;
				}
				if ( exit_code > 128 ) {
					jobAd->Assign( ATTR_ON_EXIT_BY_SIGNAL, true );
					jobAd->Assign( ATTR_ON_EXIT_SIGNAL, exit_code - 128 );
				} else {
					jobAd->Assign( ATTR_ON_EXIT_BY_SIGNAL, false );
					jobAd->Assign( ATTR_ON_EXIT_CODE, exit_code );
				}
				jobAd->Assign( ATTR_JOB_REMOTE_WALL_CLOCK, wallclock * 60 );
				jobAd->Assign( ATTR_JOB_REMOTE_USER_CPU, cpu * 60 );
				gmState = GM_STAGE_OUT;
			}
			} break;
		case GM_STAGE_OUT: {
			if ( stageList == NULL ) {
				stageList = buildStageOutList();
			}
			if ( stageLocalList == NULL ) {
				const char *file;
				stageLocalList = buildStageOutLocalList( stageList );
				stageList->rewind();
				stageLocalList->rewind();
				while ( (file = stageLocalList->next()) ) {
					ASSERT( stageList->next() );
					if ( IsUrl( file ) ) {
						stageList->deleteCurrent();
						stageLocalList->deleteCurrent();
				}
				}
			}
			rc = gahp->nordugrid_stage_out2( resourceManagerString,
											 remoteJobId,
											 *stageList, *stageLocalList );
			if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
				 rc == GAHPCLIENT_COMMAND_PENDING ) {
				break;
			} else if ( rc != 0 ) {
				errorString = gahp->getErrorString();
				dprintf( D_ALWAYS, "(%d.%d) file stage out failed: %s\n",
						 procID.cluster, procID.proc, errorString.Value() );
				gmState = GM_CANCEL;
			} else {
				gmState = GM_DONE_SAVE;
			}
			} break;
		case GM_DONE_SAVE: {
			if ( condorState != HELD && condorState != REMOVED ) {
				JobTerminated();
				if ( condorState == COMPLETED ) {
					done = requestScheddUpdate( this );
					if ( !done ) {
						break;
					}
				}
			}
			gmState = GM_DONE_COMMIT;
			} break;
		case GM_DONE_COMMIT: {
			rc = gahp->nordugrid_cancel( resourceManagerString, remoteJobId );
			if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
				 rc == GAHPCLIENT_COMMAND_PENDING ) {
				break;
			} else if ( rc != 0 ) {
				errorString = gahp->getErrorString();
				dprintf( D_ALWAYS, "(%d.%d) job cleanup failed: %s\n",
						 procID.cluster, procID.proc, errorString.Value() );
				gmState = GM_HOLD;
				break;
			}
			myResource->CancelSubmit( this );
			if ( condorState == COMPLETED || condorState == REMOVED ) {
				gmState = GM_DELETE;
			} else {
				// Clear the contact string here because it may not get
				// cleared in GM_CLEAR_REQUEST (it might go to GM_HOLD first).
				if ( remoteJobId != NULL ) {
					SetRemoteJobId( NULL );
				}
				gmState = GM_CLEAR_REQUEST;
			}
			} break;
		case GM_CANCEL: {
			rc = gahp->nordugrid_cancel( resourceManagerString, remoteJobId );
			if ( rc == GAHPCLIENT_COMMAND_NOT_SUBMITTED ||
				 rc == GAHPCLIENT_COMMAND_PENDING ) {
				break;
			} else if ( rc == 0 ) {
				gmState = GM_FAILED;
			} else {
				// What to do about a failed cancel?
				errorString = gahp->getErrorString();
				dprintf( D_ALWAYS, "(%d.%d) job cancel failed: %s\n",
						 procID.cluster, procID.proc, errorString.Value() );
				gmState = GM_FAILED;
			}
			} break;
		case GM_FAILED: {
			myResource->CancelSubmit( this );
			SetRemoteJobId( NULL );

			if ( condorState == REMOVED ) {
				gmState = GM_DELETE;
			} else {
				gmState = GM_CLEAR_REQUEST;
			}
			} break;
		case GM_DELETE: {
			// The job has completed or been removed. Delete it from the
			// schedd.
			DoneWithJob();
			// This object will be deleted when the update occurs
			} break;
		case GM_CLEAR_REQUEST: {
			// Remove all knowledge of any previous or present job
			// submission, in both the gridmanager and the schedd.

			// If we are doing a rematch, we are simply waiting around
			// for the schedd to be updated and subsequently this globus job
			// object to be destroyed.  So there is nothing to do.
			if ( wantRematch ) {
				break;
			}

			// For now, put problem jobs on hold instead of
			// forgetting about current submission and trying again.
			// TODO: Let our action here be dictated by the user preference
			// expressed in the job ad.
			if ( remoteJobId != NULL
				     && condorState != REMOVED 
					 && wantResubmit == 0 
					 && doResubmit == 0 ) {
				gmState = GM_HOLD;
				break;
			}
			// Only allow a rematch *if* we are also going to perform a resubmit
			if ( wantResubmit || doResubmit ) {
				jobAd->EvalBool(ATTR_REMATCH_CHECK,NULL,wantRematch);
			}
			if ( wantResubmit ) {
				wantResubmit = 0;
				dprintf(D_ALWAYS,
						"(%d.%d) Resubmitting to Globus because %s==TRUE\n",
						procID.cluster, procID.proc, ATTR_GLOBUS_RESUBMIT_CHECK );
			}
			if ( doResubmit ) {
				doResubmit = 0;
				dprintf(D_ALWAYS,
					"(%d.%d) Resubmitting to Globus (last submit failed)\n",
						procID.cluster, procID.proc );
			}
			errorString = "";
			if ( remoteJobId != NULL ) {
				SetRemoteJobId( NULL );
			}
			JobIdle();
			if ( submitLogged ) {
				JobEvicted();
				if ( !evictLogged ) {
					WriteEvictEventToUserLog( jobAd );
					evictLogged = true;
				}
			}
			myResource->CancelSubmit( this );
			
			if ( wantRematch ) {
				dprintf(D_ALWAYS,
						"(%d.%d) Requesting schedd to rematch job because %s==TRUE\n",
						procID.cluster, procID.proc, ATTR_REMATCH_CHECK );

				// Set ad attributes so the schedd finds a new match.
				int dummy;
				if ( jobAd->LookupBool( ATTR_JOB_MATCHED, dummy ) != 0 ) {
					jobAd->Assign( ATTR_JOB_MATCHED, false );
					jobAd->Assign( ATTR_CURRENT_HOSTS, 0 );
				}

				// If we are rematching, we need to forget about this job
				// cuz we wanna pull a fresh new job ad, with a fresh new match,
				// from the all-singing schedd.
				gmState = GM_DELETE;
				break;
			}
			
			// If there are no updates to be done when we first enter this
			// state, requestScheddUpdate will return done immediately
			// and not waste time with a needless connection to the
			// schedd. If updates need to be made, they won't show up in
			// schedd_actions after the first pass through this state
			// because we modified our local variables the first time
			// through. However, since we registered update events the
			// first time, requestScheddUpdate won't return done until
			// they've been committed to the schedd.
			done = requestScheddUpdate( this );
			if ( !done ) {
				break;
			}
			if ( remoteJobState != "" ) {
				remoteJobState = "";
				SetRemoteJobStatus( NULL );
			}
			submitLogged = false;
			executeLogged = false;
			submitFailedLogged = false;
			terminateLogged = false;
			abortLogged = false;
			evictLogged = false;
			gmState = GM_UNSUBMITTED;
			} break;
		case GM_HOLD: {
			// Put the job on hold in the schedd.
			// TODO: what happens if we learn here that the job is removed?
			// If the condor state is already HELD, then someone already
			// HELD it, so don't update anything else.
			if ( condorState != HELD ) {

				// Set the hold reason as best we can
				// TODO: set the hold reason in a more robust way.
				char holdReason[1024];
				holdReason[0] = '\0';
				holdReason[sizeof(holdReason)-1] = '\0';
				jobAd->LookupString( ATTR_HOLD_REASON, holdReason,
									 sizeof(holdReason) - 1 );
				if ( holdReason[0] == '\0' && errorString != "" ) {
					strncpy( holdReason, errorString.Value(),
							 sizeof(holdReason) - 1 );
				}
				if ( holdReason[0] == '\0' ) {
					strncpy( holdReason, "Unspecified gridmanager error",
							 sizeof(holdReason) - 1 );
				}

				JobHeld( holdReason );
			}
			gmState = GM_DELETE;
			} break;
		default:
			EXCEPT( "(%d.%d) Unknown gmState %d!", procID.cluster,procID.proc,
					gmState );
		}

		if ( gmState != old_gm_state ) {
			reevaluate_state = true;
			dprintf(D_FULLDEBUG, "(%d.%d) gm state change: %s -> %s\n",
					procID.cluster, procID.proc, GMStateNames[old_gm_state],
					GMStateNames[gmState]);
			enteredCurrentGmState = time(NULL);

			// If we were calling a gahp call that used RSL, we're done
			// with it now, so free it.
			if ( RSL ) {
				delete RSL;
				RSL = NULL;
			}
			if ( stageList ) {
				delete stageList;
				stageList = NULL;
			}
			if ( stageLocalList ) {
				delete stageLocalList;
				stageLocalList = NULL;
			}
		}

	} while ( reevaluate_state );

	return TRUE;
}

BaseResource *NordugridJob::GetResource()
{
	return (BaseResource *)myResource;
}

void NordugridJob::SetRemoteJobId( const char *job_id )
{
	free( remoteJobId );
	if ( job_id ) {
		remoteJobId = strdup( job_id );
	} else {
		remoteJobId = NULL;
	}

	MyString full_job_id;
	if ( job_id ) {
		full_job_id.sprintf( "nordugrid %s %s", resourceManagerString,
							 job_id );
	}
	BaseJob::SetRemoteJobId( full_job_id.Value() );
}

MyString *NordugridJob::buildSubmitRSL()
{
	int transfer_exec = TRUE;
	MyString *rsl = new MyString;
	MyString buff;
	StringList *stage_list = NULL;
	StringList *stage_local_list = NULL;
	char *attr_value = NULL;
	MyString rsl_suffix;
	MyString iwd;
	MyString executable;

	if ( jobAd->LookupString( ATTR_NORDUGRID_RSL, rsl_suffix ) &&
						   rsl_suffix[0] == '&' ) {
		*rsl = rsl_suffix;
		return rsl;
	}

	if ( jobAd->LookupString( ATTR_JOB_IWD, iwd ) != 1 ) {
		errorString = "ATTR_JOB_IWD not defined";
		delete rsl;
		return NULL;
	}

	//Start off the RSL
	attr_value = param( "FULL_HOSTNAME" );
	rsl->sprintf( "&(savestate=yes)(action=request)(hostname=%s)", attr_value );
	free( attr_value );
	attr_value = NULL;

	//We're assuming all job clasads have a command attribute
	jobAd->LookupString( ATTR_JOB_CMD, executable );
	jobAd->LookupBool( ATTR_TRANSFER_EXECUTABLE, transfer_exec );

	*rsl += "(executable=";
	// If we're transferring the executable, strip off the path for the
	// remote machine, since it refers to the submit machine.
	if ( transfer_exec ) {
		*rsl += condor_basename( executable.Value() );
	} else {
		*rsl += executable;
	}

	{
		ArgList args;
		MyString arg_errors;
		MyString rsl_args;
		if(!args.AppendArgsFromClassAd(jobAd,&arg_errors)) {
			dprintf(D_ALWAYS,"(%d.%d) Failed to read job arguments: %s\n",
					procID.cluster, procID.proc, arg_errors.Value());
			errorString.sprintf("Failed to read job arguments: %s\n",
					arg_errors.Value());
			delete rsl;
			return NULL;
		}
		if(args.Count() != 0) {
			if(args.InputWasV1()) {
					// In V1 syntax, the user's input _is_ RSL
				if(!args.GetArgsStringV1Raw(&rsl_args,&arg_errors)) {
					dprintf(D_ALWAYS,
							"(%d.%d) Failed to get job arguments: %s\n",
							procID.cluster,procID.proc,arg_errors.Value());
					errorString.sprintf("Failed to get job arguments: %s\n",
							arg_errors.Value());
					delete rsl;
					return NULL;
				}
			}
			else {
					// In V2 syntax, we convert the ArgList to RSL
				for(int i=0;i<args.Count();i++) {
					if(i) {
						rsl_args += ' ';
					}
					rsl_args += rsl_stringify(args.GetArg(i));
				}
			}
			*rsl += ")(arguments=";
			*rsl += rsl_args;
		}
	}

	// If we're transferring the executable, tell Nordugrid to set the
	// execute bit on the transferred executable.
	if ( transfer_exec ) {
		*rsl += ")(executables=";
		*rsl += condor_basename( executable.Value() );
	}

	if ( jobAd->LookupString( ATTR_JOB_INPUT, &attr_value ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(attr_value) ) {
			*rsl += ")(stdin=";
			*rsl += condor_basename(attr_value);
		}
		free( attr_value );
		attr_value = NULL;
	}

	stage_list = buildStageInList();

	if ( stage_list->isEmpty() == false ) {
		char *file;
		stage_list->rewind();

		*rsl += ")(inputfiles=";

		while ( (file = stage_list->next()) != NULL ) {
			*rsl += "(";
			*rsl += condor_basename(file);
			if ( IsUrl( file ) ) {
				rsl->sprintf_cat( " \"%s\")", file );
			} else {
				*rsl += " \"\")";
			}
		}
	}

	delete stage_list;
	stage_list = NULL;

	if ( jobAd->LookupString( ATTR_JOB_OUTPUT, &attr_value ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(attr_value) ) {
			*rsl += ")(stdout=" REMOTE_STDOUT_NAME;
		}
		free( attr_value );
		attr_value = NULL;
	}

	if ( jobAd->LookupString( ATTR_JOB_ERROR, &attr_value ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(attr_value) ) {
			*rsl += ")(stderr=" REMOTE_STDERR_NAME;
		}
		free( attr_value );
	}

	stage_list = buildStageOutList();
	stage_local_list = buildStageOutLocalList( stage_list );

	if ( stage_list->isEmpty() == false ) {
		char *file;
		char *local_file;
		stage_list->rewind();
		stage_local_list->rewind();

		*rsl += ")(outputfiles=";

		while ( (file = stage_list->next()) != NULL ) {
			local_file = stage_local_list->next();
			*rsl += "(";
			*rsl += condor_basename(file);
			if ( IsUrl( local_file ) ) {
				rsl->sprintf_cat( " \"%s\")", local_file );
			} else {
				*rsl += " \"\")";
			}
		}
	}

	delete stage_list;
	stage_list = NULL;
	delete stage_local_list;
	stage_local_list = NULL;

	*rsl += ')';

	if ( !rsl_suffix.IsEmpty() ) {
		*rsl += rsl_suffix;
	}

dprintf(D_FULLDEBUG,"*** RSL='%s'\n",rsl->Value());
	return rsl;
}

StringList *NordugridJob::buildStageInList()
{
	StringList *tmp_list = NULL;
	StringList *stage_list = NULL;
	char *filename = NULL;
	MyString buf;
	MyString iwd;
	int transfer = TRUE;

	if ( jobAd->LookupString( ATTR_JOB_IWD, iwd ) ) {
		if ( iwd.Length() > 1 && iwd[iwd.Length() - 1] != '/' ) {
			iwd += '/';
		}
	}

	jobAd->LookupString( ATTR_TRANSFER_INPUT_FILES, buf );
	tmp_list = new StringList( buf.Value(), "," );

	jobAd->LookupBool( ATTR_TRANSFER_EXECUTABLE, transfer );
	if ( transfer ) {
		jobAd->LookupString( ATTR_JOB_CMD, buf );
		if ( !tmp_list->file_contains( buf.Value() ) ) {
			tmp_list->append( buf.Value() );
		}
	}

	transfer = TRUE;
	jobAd->LookupBool( ATTR_TRANSFER_INPUT, transfer );
	if ( transfer && jobAd->LookupString( ATTR_JOB_INPUT, buf ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(buf.Value()) ) {
			if ( !tmp_list->file_contains( buf.Value() ) ) {
				tmp_list->append( buf.Value() );
			}
		}
	}

	stage_list = new StringList;

	tmp_list->rewind();
	while ( ( filename = tmp_list->next() ) ) {
		if ( filename[0] == '/' || IsUrl( filename ) ) {
			buf.sprintf( "%s", filename );
		} else {
			buf.sprintf( "%s%s", iwd.Value(), filename );
		}
		stage_list->append( buf.Value() );
	}

	delete tmp_list;

	return stage_list;
}

StringList *NordugridJob::buildStageOutList()
{
	StringList *stage_list = NULL;
	MyString buf;
	MyString iwd = "/";
	bool transfer = TRUE;

	if ( jobAd->LookupString( ATTR_JOB_IWD, iwd ) ) {
		if ( iwd.Length() > 1 && iwd[iwd.Length() - 1] != '/' ) {
			iwd += '/';
		}
	}

	jobAd->LookupString( ATTR_TRANSFER_OUTPUT_FILES, buf );
	stage_list = new StringList( buf.Value(), "," );

	jobAd->LookupBool( ATTR_TRANSFER_OUTPUT, transfer );
	if ( transfer && jobAd->LookupString( ATTR_JOB_OUTPUT, buf ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(buf.Value()) ) {
			if ( !stage_list->file_contains( REMOTE_STDOUT_NAME ) ) {
				stage_list->append( REMOTE_STDOUT_NAME );
			}
		}
	}

	transfer = TRUE;
	jobAd->LookupBool( ATTR_TRANSFER_ERROR, transfer );
	if ( transfer && jobAd->LookupString( ATTR_JOB_ERROR, buf ) == 1) {
		// only add to list if not NULL_FILE (i.e. /dev/null)
		if ( ! nullFile(buf.Value()) ) {
			if ( !stage_list->file_contains( REMOTE_STDERR_NAME ) ) {
				stage_list->append( REMOTE_STDERR_NAME );
			}
		}
	}

	return stage_list;
}

StringList *NordugridJob::buildStageOutLocalList( StringList *stage_list )
{
	StringList *stage_local_list;
	char *remaps = NULL;
	MyString local_name;
	char *remote_name;
	MyString stdout_name = "";
	MyString stderr_name = "";
	MyString buff;
	MyString iwd = "/";

	if ( jobAd->LookupString( ATTR_JOB_IWD, iwd ) ) {
		if ( iwd.Length() > 1 && iwd[iwd.Length() - 1] != '/' ) {
			iwd += '/';
		}
	}

	jobAd->LookupString( ATTR_JOB_OUTPUT, stdout_name );
	jobAd->LookupString( ATTR_JOB_ERROR, stderr_name );

	stage_local_list = new StringList;

	jobAd->LookupString( ATTR_TRANSFER_OUTPUT_REMAPS, &remaps );

	stage_list->rewind();
	while ( (remote_name = stage_list->next()) ) {
			// stdout and stderr don't get remapped, and their paths
			// are evaluated locally
		if ( strcmp( REMOTE_STDOUT_NAME, remote_name ) == 0 ) {
			local_name = stdout_name.Value();
		} else if ( strcmp( REMOTE_STDERR_NAME, remote_name ) == 0 ) {
			local_name = stderr_name.Value();
		} else if( remaps && filename_remap_find( remaps, remote_name,
												  local_name ) ) {
				// file is remapped
		} else {
			local_name = condor_basename( remote_name );
		}

		if ( (local_name.Length() && local_name[0] == '/')
			 || IsUrl( local_name.Value() ) ) {
			buff = local_name;
		} else {
			buff.sprintf( "%s%s", iwd.Value(), local_name.Value() );
		}
		stage_local_list->append( buff.Value() );
	}

	if ( remaps ) {
		free( remaps );
	}

	return stage_local_list;
}

void NordugridJob::NotifyNewRemoteStatus( const char *status )
{
	if ( SetRemoteJobStatus( status ) ) {
		remoteJobState = status;
		SetEvaluateState();

		if ( condorState == IDLE &&
			 ( remoteJobState == REMOTE_STATE_INLRMS_R ||
			   remoteJobState == REMOTE_STATE_INLRMS_E ||
			   remoteJobState == REMOTE_STATE_EXECUTED ||
			   remoteJobState == REMOTE_STATE_FINISHING ||
			   remoteJobState == REMOTE_STATE_FINISHED ||
			   remoteJobState == REMOTE_STATE_FAILED ) ) {
			JobRunning();
		} else if ( condorState == RUNNING &&
					( remoteJobState == REMOTE_STATE_INLRMS_Q ||
					  remoteJobState == REMOTE_STATE_INLRMS_S ) ) {
			JobIdle();
		}
	}
	if ( gmState == GM_RECOVER_QUERY ) {
		SetEvaluateState();
	}
}
