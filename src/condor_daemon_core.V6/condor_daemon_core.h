////////////////////////////////////////////////////////////////////////////////
//
// This file contains the definition for class DaemonCore. This is the
// central structure for every daemon in condor. The daemon core triggers
// preregistered handlers for corresponding events. class Service is the base
// class of the classes that daemon_core can serve. In order to use a class
// with the DaemonCore, it has to be a derived class of Service.
//
//
////////////////////////////////////////////////////////////////////////////////

#ifndef _CONDOR_DAEMON_CORE_H_
#define _CONDOR_DAEMON_CORE_H_

#include "condor_common.h"

#ifndef WIN32
#if defined(Solaris)
#define __EXTENSIONS__
#endif
#include <sys/types.h>
#include <sys/time.h>
#include "condor_fdset.h"
#if defined(Solaris)
#undef __EXTENSIONS__
#endif
#endif  /* ifndef WIN32 */

#include "condor_uid.h"
#include "condor_io.h"
#include "condor_timer_manager.h"
#include "condor_commands.h"
#include "HashTable.h"
#include "list.h"

// enum for Daemon Core socket/command/signal permissions
enum DCpermission { ALLOW, READ, WRITE, NEGOTIATOR, IMMEDIATE_FAMILY };

static const int KEEP_STREAM = 100;
static char* EMPTY_DESCRIP = "<NULL>";

// typedefs for callback procedures
typedef int		(*CommandHandler)(Service*,int,Stream*);
typedef int		(Service::*CommandHandlercpp)(int,Stream*);

typedef int		(*SignalHandler)(Service*,int);
typedef int		(Service::*SignalHandlercpp)(int);

typedef int		(*SocketHandler)(Service*,Stream*);
typedef int		(Service::*SocketHandlercpp)(Stream*);

typedef int		(*ReaperHandler)(Service*,int pid,int exit_status);
typedef int		(Service::*ReaperHandlercpp)(int pid,int exit_status);

// other typedefs and macros needed on WIN32
#ifdef WIN32
typedef DWORD pid_t;
#define WIFEXITED(stat) ((int)(1))
#define WEXITSTATUS(stat) ((int)(stat))
#define WIFSIGNALED(stat) ((int)(0))
#define WTERMSIG(stat) ((int)(0))
#endif  // of ifdef WIN32

// some constants for HandleSig().
#define _DC_RAISESIGNAL 1
#define _DC_BLOCKSIGNAL 2
#define _DC_UNBLOCKSIGNAL 3

// If WANT_DC_PM is defined, it means we want DaemonCore Process Management.
// We _always_ want it on WinNT; on Unix, some daemons still do their own 
// Process Management (just until we get around to changing them to use daemon core).
#if defined(WIN32) && !defined(WANT_DC_PM)
#define WANT_DC_PM
#endif

// defines for signals; compatibility with traditional UNIX values maintained where possible.
#define	DC_SIGHUP	1	/* hangup */
#define	DC_SIGINT	2	/* interrupt (rubout) */
#define	DC_SIGQUIT	3	/* quit (ASCII FS) */
#define	DC_SIGILL	4	/* illegal instruction (not reset when caught) */
#define	DC_SIGTRAP	5	/* trace trap (not reset when caught) */
#define	DC_SIGIOT	6	/* IOT instruction */
#define	DC_SIGABRT 6	/* used by abort, replace DC_SIGIOT in the future */
#define	DC_SIGEMT	7	/* EMT instruction */
#define	DC_SIGFPE	8	/* floating point exception */
#define	DC_SIGKILL	9	/* kill (cannot be caught or ignored) */
#define	DC_SIGBUS	10	/* bus error */
#define	DC_SIGSEGV	11	/* segmentation violation */
#define	DC_SIGSYS	12	/* bad argument to system call */
#define	DC_SIGPIPE	13	/* write on a pipe with no one to read it */
#define	DC_SIGALRM	14	/* alarm clock */
#define	DC_SIGTERM	15	/* software termination signal from kill */
#define	DC_SIGUSR1	16	/* user defined signal 1 */
#define	DC_SIGUSR2	17	/* user defined signal 2 */
#define	DC_SIGCLD	18	/* child status change */
#define	DC_SIGCHLD	18	/* child status change alias (POSIX) */
#define	DC_SIGPWR	19	/* power-fail restart */
#define	DC_SIGWINCH 20	/* window size change */
#define	DC_SIGURG	21	/* urgent socket condition */
#define	DC_SIGPOLL 22	/* pollable event occured */
#define	DC_SIGIO	DC_SIGPOLL	/* socket I/O possible (DC_SIGPOLL alias) */
#define	DC_SIGSTOP 23	/* stop (cannot be caught or ignored) */
#define	DC_SIGTSTP 24	/* user stop requested from tty */
#define	DC_SIGCONT 25	/* stopped process has been continued */
#define	DC_SIGTTIN 26	/* background tty read attempted */
#define	DC_SIGTTOU 27	/* background tty write attempted */
#define	DC_SIGVTALRM 28	/* virtual timer expired */
#define	DC_SIGPROF 29	/* profiling timer expired */
#define	DC_SIGXCPU 30	/* exceeded cpu limit */
#define	DC_SIGXFSZ 31	/* exceeded file size limit */
#define	DC_SIGWAITING 32	/* process's lwps are blocked */
#define	DC_SIGLWP	33	/* special signal used by thread library */
#define	DC_SIGFREEZE 34	/* special signal used by CPR */
#define	DC_SIGTHAW 35	/* special signal used by CPR */
#define	DC_SIGCANCEL 36	/* thread cancellation signal used by libthread */


class DaemonCore : public Service
{
	friend class TimerManager;
	friend DWORD pidWatcherThread(void*);	
	friend main( int argc, char** argv );

	public:
		DaemonCore(int PidSize = 0, int ComSize = 0, int SigSize = 0, int SocSize = 0, int ReapSize = 0);
		~DaemonCore();

		void	Driver();
		
		int		Register_Command(int command, char *com_descrip, CommandHandler handler, 
					char *handler_descrip, Service* s = NULL, DCpermission perm = ALLOW);
		int		Register_Command(int command, char *com_descript, CommandHandlercpp handlercpp, 
					char *handler_descrip, Service* s, DCpermission perm = ALLOW);
		int		Cancel_Command( int command );
		int		InfoCommandPort();
		char*	InfoCommandSinfulString();

		int		Register_Signal(int sig, char *sig_descrip, SignalHandler handler, 
					char *handler_descrip, Service* s = NULL, DCpermission perm = ALLOW);
		int		Register_Signal(int sig, char *sig_descript, SignalHandlercpp handlercpp, 
					char *handler_descrip, Service* s, DCpermission perm = ALLOW);
		int		Cancel_Signal( int sig );
		int		Block_Signal(int sig) { return(HandleSig(_DC_BLOCKSIGNAL,sig)); }
		int		Unblock_Signal(int sig) { return(HandleSig(_DC_UNBLOCKSIGNAL,sig)); }

		int		Register_Reaper(char *reap_descrip, ReaperHandler handler, 
					char *handler_descrip, Service* s = NULL);
		int		Register_Reaper(char *reap_descript, ReaperHandlercpp handlercpp, 
					char *handler_descrip, Service* s);
		int		Reset_Reaper(int rid, char *reap_descrip, ReaperHandler handler, 
					char *handler_descrip, Service* s = NULL);
		int		Reset_Reaper(int rid, char *reap_descript, ReaperHandlercpp handlercpp, 
					char *handler_descrip, Service* s);
		int		Cancel_Reaper( int rid );
		
		int		Register_Socket(Stream* iosock, char *iosock_descrip, SocketHandler handler,
					char *handler_descrip, Service* s = NULL, DCpermission perm = ALLOW);
		int		Register_Socket(Stream* iosock, char *iosock_descrip, SocketHandlercpp handlercpp,
					char *handler_descrip, Service* s, DCpermission perm = ALLOW);
		int		Register_Command_Socket( Stream* iosock, char *descrip = NULL ) {
					return(Register_Socket(iosock,descrip,(SocketHandler)NULL,(SocketHandlercpp)NULL,"DC Command Handler",NULL,ALLOW,0)); 
				}
		int		Cancel_Socket( Stream* );

		int		Register_Timer(unsigned deltawhen, Event event, char *event_descrip, 
					Service* s = NULL, int id = -1);
		int		Register_Timer(unsigned deltawhen, unsigned period, Event event, 
					char *event_descrip, Service* s = NULL, int id = -1);
		int		Register_Timer(unsigned deltawhen, Eventcpp event, char *event_descrip, 
					Service* s, int id = -1);
		int		Register_Timer(unsigned deltawhen, unsigned period, Eventcpp event, 
					char *event_descrip, Service* s, int id = -1);
		int		Cancel_Timer( int id );
		int		Reset_Timer( int id, unsigned when, unsigned period = 0 );

		void	Dump(int, char* = NULL );

		inline pid_t getpid() { return mypid; };
		inline pid_t getppid() { return ppid; };

		int		Send_Signal(pid_t pid, int sig);

		int		SetDataPtr( void * );
		int		Register_DataPtr( void * );
		void	*GetDataPtr();
		
		int		Create_Process(
			char		*name,
			char		*args,
			priv_state	condor_priv = PRIV_UNKNOWN,
			int			reaper_id = 1,
			int			want_commanand_port = TRUE,
			char		*env = NULL,
			char		*cwd = NULL,
		//	unsigned int std[3] = { 0, 0, 0 },
			int			new_process_group = FALSE,
			Stream		*sock_inherit_list[] = NULL 			
			);

#ifdef FUTURE		
		int		Create_Thread()
		int		Kill_Process()
		int		Kill_Thread()
#endif


		
	private:
		int		HandleSigCommand(int command, Stream* stream);
		void	HandleReq(int socki);
		int		HandleSig(int command, int sig);
		void	Inherit( ReliSock* &rsock, SafeSock* &ssock );  // called in main()
		int		HandleProcessExitCommand(int command, Stream* stream);
		int		HandleProcessExit(pid_t pid, int exit_status);

		int		Register_Command(int command, char *com_descip, CommandHandler handler, 
					CommandHandlercpp handlercpp, char *handler_descrip, Service* s, 
					DCpermission perm, int is_cpp);
		int		Register_Signal(int sig, char *sig_descip, SignalHandler handler, 
					SignalHandlercpp handlercpp, char *handler_descrip, Service* s, 
					DCpermission perm, int is_cpp);
		int		Register_Socket(Stream* iosock, char *iosock_descrip, SocketHandler handler, 
					SocketHandlercpp handlercpp, char *handler_descrip, Service* s, 
					DCpermission perm, int is_cpp);
		int		Register_Reaper(int rid, char *reap_descip, ReaperHandler handler, 
					ReaperHandlercpp handlercpp, char *handler_descrip, Service* s, 
					int is_cpp);


		struct CommandEnt
		{
		    int				num;
		    CommandHandler	handler;
			CommandHandlercpp	handlercpp;
			int				is_cpp;
			DCpermission	perm;
			Service*		service; 
			char*			command_descrip;
			char*			handler_descrip;
			void*			data_ptr;
		};
		void				DumpCommandTable(int, const char* = NULL);
		int					maxCommand;		// max number of command handlers
		int					nCommand;		// number of command handlers used
		CommandEnt*			comTable;		// command table

		struct SignalEnt 
		{
			int				num;
		    SignalHandler	handler;
			SignalHandlercpp	handlercpp;
			int				is_cpp;
			DCpermission	perm;
			Service*		service; 
			int				is_blocked;
			// Note: is_pending must be volatile because it could be set inside
			// of a Unix asynchronous signal handler (such as SIGCHLD).
			volatile int	is_pending;	
			char*			sig_descrip;
			char*			handler_descrip;
			void*			data_ptr;
		};
		void				DumpSigTable(int, const char* = NULL);
		int					maxSig;		// max number of signal handlers
		int					nSig;		// number of signal handlers used
		SignalEnt*			sigTable;		// signal table
		int					sent_signal;	// TRUE if a signal handler sends a signal

		struct SockEnt
		{
		    Stream*			iosock;
			SOCKET			sockd;
		    SocketHandler	handler;
			SocketHandlercpp	handlercpp;
			int				is_cpp;
			DCpermission	perm;
			Service*		service; 
			char*			iosock_descrip;
			char*			handler_descrip;
			void*			data_ptr;
		};
		void				DumpSocketTable(int, const char* = NULL);
		int					maxSocket;		// max number of socket handlers
		int					nSock;		// number of socket handlers used
		SockEnt*			sockTable;		// socket table
		int					initial_command_sock;  

		struct ReapEnt
		{
		    int				num;
		    ReaperHandler	handler;
			ReaperHandlercpp	handlercpp;
			int				is_cpp;
			Service*		service; 
			char*			reap_descrip;
			char*			handler_descrip;
			void*			data_ptr;
		};
		void				DumpReapTable(int, const char* = NULL);
		int					maxReap;		// max number of reaper handlers
		int					nReap;			// number of reaper handlers used
		ReapEnt*			reapTable;		// reaper table

		struct PidEntry
		{
			pid_t pid;
#ifdef WIN32
			HANDLE hProcess;
			HANDLE hThread;
#endif
			char sinful_string[28];
			char parent_sinful_string[28];
			int is_local;
			int parent_is_local;
			int	reaper_id;
		};
		typedef HashTable <pid_t, PidEntry *> PidHashTable;
		PidHashTable* pidTable;
		pid_t mypid;
		pid_t ppid;

#ifdef WIN32
		// note: as of WinNT 4.0, MAXIMUM_WAIT_OBJECTS == 64
		struct PidWatcherEntry
		{
			PidEntry* pidentries[MAXIMUM_WAIT_OBJECTS - 1];
			HANDLE event;
			HANDLE hThread;
			CRITICAL_SECTION crit_section;
			int nEntries;
		};

		List<PidWatcherEntry> PidWatcherList;

		int					WatchPid(PidEntry *pidentry);
#endif
			
		static				TimerManager t;
		void				DumpTimerList(int, char* = NULL);

		void				free_descrip(char *p) { if(p &&  p != EMPTY_DESCRIP) free(p); }
	
		fd_set				readfds; 

#ifdef WIN32
		DWORD	dcmainThreadId;		// the thread id of the thread running the main daemon core
#endif

		// these need to be in thread local storage someday
		void **curr_dataptr;
		void **curr_regdataptr;
		// end of thread local storage
};



#ifndef _NO_EXTERN_DAEMON_CORE
extern DaemonCore* daemonCore;
#endif

#define MAX_INHERIT_SOCKS 10
#define _INHERITBUF_MAXSIZE 500

#endif
