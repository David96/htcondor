#ifndef _CONDOR_SYSCALLS_H
#define _CONDOR_SYSCALLS_H


typedef int BOOL;

static const int 	SYS_LOCAL = 1;
static const int 	SYS_REMOTE = 0;
static const int	SYS_RECORDED = 2;
static const int	SYS_MAPPED = 2;

static const int	SYS_UNRECORDED = 0;
static const int	SYS_UNMAPPED = 0;

#if defined(__cplusplus)
extern "C" {
#endif

int SetSyscalls( int mode );
BOOL LocalSysCalls();
BOOL RemoteSysCalls();
BOOL MappingFileDescriptors();
int REMOTE_syscall( int syscall_num, ... );
int syscall( int, ... );


#if defined(__cplusplus)
}
#endif

#endif
