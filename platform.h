/* platform.h — thin OS abstraction for pdeide.
 *
 * pdeide needs to spawn/stop child build & sketch processes, read their
 * output through pipes, fetch compiler flags from the shell and locate its
 * own executable dir. These are the only OS-specific bits of the IDE, so
 * they were pulled out here so the rest of pdeide.c stays POSIX-shaped on
 * every platform (packaged next to platform.c).
 */

#ifndef PDEIDE_PLATFORM_H
#define PDEIDE_PLATFORM_H

#ifdef _WIN32
#include <windows.h>
typedef DWORD plat_pid_t;
#else
typedef long plat_pid_t;
#endif

/* spawn a command, capturing output (stdout+stderr combined when
 * stdout_file is NULL; otherwise stdout goes to the file, stderr captured).
 * Returns the child's exit status, or -1 on spawn failure. On success
 * *base_out is set to a malloc'd, NUL-terminated copy of the capture. */
int  plat_spawn_capture(char *const argv[], const char *stdout_file, char **base_out);

/* term/kill a running child (graduated: soft first, force as escalation) */
void plat_kill(plat_pid_t pid);
void plat_kill_force(plat_pid_t pid);

/* non-blocking wait; returns 0 when the child has been reaped, nonzero if it
 * is still running. On reap, *status gets the exit code and *sig the signal
 * that killed it (or -1 if it exited normally). */
int  plat_wait_nohang(plat_pid_t pid, int *status, int *sig);

void plat_sleep_ms(int ms);

/* run a shell command and return its stdout as a malloc'd string (or NULL) */
char *plat_shell_read(const char *cmd);

void plat_chmod_exec(const char *path);

/* spawn a child asynchronously: pipes its stdout/stderr out, optionally
 * chdirs into cwd. Returns 0 on success and sets *out_fd/*err_fd/*pid. */
int  plat_spawn_run(char *const argv[], const char *cwd,
                    int *out_fd, int *err_fd, plat_pid_t *pid);

void plat_set_nonblocking(int fd);

/* access() wrapper; flags follow R_OK(4)/W_OK(2)/X_OK(1) */
int  plat_file_access(const char *path, int mode);

/* directory of the current executable (NUL-terminated into buf) */
void plat_exe_dir(char *buf, int size);

/* create a unique temp dir from a mkdtemp-style pattern ("pdeide.XXXXXX").
 * Returns 0 on success with buf holding the created path. */
int  plat_mkdtemp_dir(const char *pattern, char *buf, int size);

long plat_getpid(void);

#endif