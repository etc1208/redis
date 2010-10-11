#ifdef _WIN32

#include <windows.h>
#include <process.h>
#include <errno.h>
#include <winsock2.h>
#include <signal.h>
#include "redis.h"
#include "win32fixes.h"

int w32initWinSock(void) {

    WSADATA t_wsa; // WSADATA structure
    WORD wVers; // version number
    int iError; // error number

    wVers = MAKEWORD(2, 2); // Set the version number to 2.2
    iError = WSAStartup(wVers, &t_wsa); // Start the WSADATA

    if(iError != NO_ERROR || LOBYTE(t_wsa.wVersion) != 2 || HIBYTE(t_wsa.wVersion) != 2 ) {
        return 0; /* not done; check WSAGetLastError() for error number */
    };

    return 1; /* Initialized */
}

int w32CeaseAndDesist(pid_t pid) {

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

    /* invalid process; no access rights; etc   */
    if (h == NULL)
        return errno = EINVAL;

    if (!TerminateProcess(h, 127))
        return errno = EINVAL;

    errno = WaitForSingleObject(h, INFINITE);
    CloseHandle(h);

    return 0;
}

int sigaction(int sig, struct sigaction *in, struct sigaction *out) {
    REDIS_NOTUSED(out);

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction
     * is used. Otherwise, sa_handler is used */
    if (in->sa_flags & SA_SIGINFO)
     	signal(sig, in->sa_sigaction);
    else
        signal(sig, in->sa_handler);

    return 0;
}

int kill(pid_t pid, int sig) {

  if (sig == SIGKILL) {

    HANDLE h = OpenProcess(PROCESS_TERMINATE, 0, pid);

    if (!TerminateProcess(h, 127)) {
        errno = EINVAL; /* GetLastError() */
        CloseHandle(h);
        return -1;
    };

    CloseHandle(h);
    return 0;
  }
  else {
    errno = EINVAL;
    return -1;
  };
}

int fsync (int fd) {
    HANDLE h = (HANDLE) _get_osfhandle (fd);
    DWORD err;

    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    if (!FlushFileBuffers (h)) {
        /* Windows error -> Unix */
        err = GetLastError ();
        switch (err) {
            case ERROR_INVALID_HANDLE:
            errno = EINVAL;
            break;

            default:
            errno = EIO;
        }
        return -1;
    }

    return 0;
}

pid_t wait3(int *stat_loc, int options, void *rusage) {
    REDIS_NOTUSED(stat_loc);
    REDIS_NOTUSED(options);
    REDIS_NOTUSED(rusage);
    return waitpid((pid_t) -1, 0, WAIT_FLAGS);
}

/* Holder for more complex windows sockets */
int replace_setsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen) {
    return (setsockopt)(socket, level, optname, optval, optlen);
}

/* Rename which works when file exists */
int replace_rename(const char *src, const char *dst) {

    if (MoveFileEx(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
        return 0;
    else
        /* On error we will return generic eroor code without GetLastError() */
        return EIO;
}

/* mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0); */
void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset) {
	HANDLE h;
	void *data;

    REDIS_NOTUSED(offset);

	if ((flags != MAP_SHARED) || (prot != PROT_READ)) {
	  /*  Not supported  in this port */
      return MAP_FAILED;
    };

	h = CreateFileMapping((HANDLE)_get_osfhandle(fd),
                        NULL,PAGE_READONLY,0,0,NULL);

	if (!h) return MAP_FAILED;

	data = MapViewOfFileEx(h, FILE_MAP_READ,0,0,length,start);

	CloseHandle(h);

    if (!data) return MAP_FAILED;

	return data;
}

int munmap(void *start, size_t length) {
    REDIS_NOTUSED(length);
	return !UnmapViewOfFile(start);
}

static unsigned __stdcall win32_proxy_threadproc(void *arg) {
    void (*func)(void*) = arg;
    func(NULL);

    _endthreadex(0);
	return 0;
}

int pthread_create(pthread_t *thread, const void *unused,
		   void *(*start_routine)(void*), void *arg) {

    REDIS_NOTUSED(unused);
    HANDLE h;

    /*  Arguments not supported in this port */
    if (arg) exit(1);

    REDIS_NOTUSED(arg);
	h =(HANDLE) _beginthreadex(NULL,  /* Security not used */
                               REDIS_THREAD_STACK_SIZE, /* Set custom stack size */
                               win32_proxy_threadproc,  /* calls win32 stdcall proxy */
                               start_routine, /* real threadproc is passed as paremeter */
                               STACK_SIZE_PARAM_IS_A_RESERVATION,  /* reserve stack */
                               thread /* returned thread id */
                );

	if (!h)
		return errno;

    CloseHandle(h);
	return 0;
}

int pthread_detach (pthread_t thread) {
    REDIS_NOTUSED(thread);
    return 0; /* noop */
  }

pthread_t pthread_self(void) {
	return GetCurrentThreadId();
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset) {
    REDIS_NOTUSED(set);
    REDIS_NOTUSED(oset);
    switch (how) {
      case SIG_BLOCK:
      case SIG_UNBLOCK:
      case SIG_SETMASK:
           break;
      default:
            errno = EINVAL;
            return -1;
    }

  errno = ENOSYS;
  return -1;
}

/*
int inet_aton(const char *cp_arg, struct in_addr *addr) {
	register unsigned long val;
	register int base, n;
	register unsigned char c;
	register unsigned const char *cp = (unsigned const char *) cp_arg;
	unsigned int parts[4];
	register unsigned int *pp = parts;

	for (;;) {

		// Collect number up to ``.''.
		 // Values are specified as for C:
		 // 0x=hex, 0=octal, other=decimal.

		val = 0; base = 10;
		if (*cp == '0') {
			if (*++cp == 'x' || *cp == 'X')
				base = 16, cp++;
			else
				base = 8;
		}
		while ((c = *cp) != '\0') {
			if (isascii(c) && isdigit(c)) {
				val = (val * base) + (c - '0');
				cp++;
				continue;
			}
			if (base == 16 && isascii(c) && isxdigit(c)) {
				val = (val << 4) +
					(c + 10 - (islower(c) ? 'a' : 'A'));
				cp++;
				continue;
			}
			break;
		}
		if (*cp == '.') {
			//
			// Internet format:
			//	a.b.c.d
			//	a.b.c	(with c treated as 16-bits)
			//	a.b	(with b treated as 24 bits)
			//
			if (pp >= parts + 3 || val > 0xff)
				return (0);
			*pp++ = val, cp++;
		} else
			break;
	}

	// Check for trailing characters.

	if (*cp && (!isascii(*cp) || !isspace(*cp)))
		return (0);

	 // Concoct the address according to
	 // the number of parts specified.

	n = pp - parts + 1;
	switch (n) {

	case 1:				// a -- 32 bits
		break;

	case 2:				// a.b -- 8.24 bits
		if (val > 0xffffff)
			return (0);
		val |= parts[0] << 24;
		break;

	case 3:				//a.b.c -- 8.8.16 bits
		if (val > 0xffff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16);
		break;

	case 4:				// a.b.c.d -- 8.8.8.8 bits
		if (val > 0xff)
			return (0);
		val |= (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
		break;
	}
	if (addr)
		addr->s_addr = htonl(val);
	return (1);
}
*/

int fork(void) {
#ifndef _WIN32_FORK
  return -1;
#else
  /* TODO: Implement fork() for redis background writing */
  return -1;
#endif
 }
/*
int getrusage(int who, struct rusage * rusage) {

   FILETIME starttime, exittime, kerneltime, usertime;
   ULARGE_INTEGER li;

   if (r == NULL) {
       errno = EFAULT;
       return -1;
   }

   memset(rusage, 0, sizeof(struct rusage));

   if (who == RUSAGE_SELF) {
     if (!GetProcessTimes(GetCurrentProcess(),
                        &starttime,
                        &exittime,
                        &kerneltime,
                        &usertime))
     {
         errno = EFAULT;
         return -1;
     }
   }

   if (who == RUSAGE_CHILDREN) {



   }


    //
    memcpy(&li, &kerneltime, sizeof(FILETIME));
    li.QuadPart /= 10L;         //
    rusage->ru_stime.tv_sec = li.QuadPart / 1000000L;
    rusage->ru_stime.tv_usec = li.QuadPart % 1000000L;

    memcpy(&li, &usertime, sizeof(FILETIME));
    li.QuadPart /= 10L;         //
    rusage->ru_utime.tv_sec = li.QuadPart / 1000000L;
    rusage->ru_utime.tv_usec = li.QuadPart % 1000000L;
}
*/

#endif
