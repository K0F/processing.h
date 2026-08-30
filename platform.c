/* platform.c — OS layer for pdeide.
 *
 * Full POSIX (Linux/macOS) implementation plus a best-effort _WIN32 one
 * (CreateProcess + CRT fd mapping), kept under #ifdef so the Windows path
 * compiles cleanly on both targets, including on a POSIX host (where it is
 * dormant).
 */

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
/* ------------------------------------------------------------------ */
/* Windows (best-effort)                                               */
/* ------------------------------------------------------------------ */
#include <windows.h>
#include <sys/stat.h>        /* _S_IREAD/_S_IWRITE/_S_IEXEC for _chmod */
#include <io.h>
#include <fcntl.h>
#include <process.h>
#ifndef ssize_t
typedef intptr_t ssize_t;
#endif

static char *win_cmdline(char *const argv[]) {
  size_t n = 0;
  for (int i = 0; argv[i]; i++) n += strlen(argv[i]) + 3;   /* quote + space */
  char *cmd = malloc(n + 1), *p = cmd;
  if (!cmd) return NULL;
  for (int i = 0; argv[i]; i++) {
    if (i) *p++ = ' ';
    *p++ = '"';
    for (const char *s = argv[i]; *s; s++) {
      if (*s == '"') *p++ = '\\';
      *p++ = *s;
    }
    *p++ = '"';
  }
  *p = 0;
  return cmd;
}

static HANDLE win_open_pid(plat_pid_t pid, DWORD access) {
  return OpenProcess(access, FALSE, pid);
}

int plat_spawn_capture(char *const argv[], const char *stdout_file, char **base_out) {
  *base_out = NULL;
  SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };          /* inheritable */
  HANDLE r = NULL, w = NULL;
  if (!CreatePipe(&r, &w, &sa, 0)) return -1;

  STARTUPINFOA si; PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si); memset(&pi, 0, sizeof pi);
  si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = w;
  si.hStdError = w;
  HANDLE hfile = (HANDLE)-1;                                    /* -1: close stdout in child */
  if (stdout_file) {
    hfile = CreateFileA(stdout_file, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile == INVALID_HANDLE_VALUE) { CloseHandle(r); CloseHandle(w); return -1; }
    si.hStdOutput = hfile;
  }
  char *cmd = win_cmdline(argv);
  if (!cmd) { CloseHandle(r); CloseHandle(w); if (hfile != (HANDLE)-1) CloseHandle(hfile); return -1; }
  /* allow child to inherit handles only for the pipe + maybe the file */
  if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                      CREATE_NO_WINDOW | (stdout_file ? 0 : CREATE_NO_WINDOW),
                      NULL, NULL, &si, &pi)) {
    free(cmd); CloseHandle(r); CloseHandle(w);
    if (hfile != (HANDLE)-1) CloseHandle(hfile);
    return -1;
  }
  free(cmd); CloseHandle(w);
  if (hfile != (HANDLE)-1) CloseHandle(hfile);

  /* read everything the child writes */
  size_t cap = 65536, n = 0; char *buf = malloc(cap); buf[0] = 0;
  for (;;) {
    char tmp[4096]; DWORD got = 0;
    if (!ReadFile(r, tmp, sizeof tmp, &got, NULL)) break;
    if (got == 0) break;
    if (n + got + 1 > cap) { while (n + got + 1 > cap) cap *= 2; buf = realloc(buf, cap); }
    memcpy(buf + n, tmp, got); n += got; buf[n] = 0;
  }
  CloseHandle(r);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  *base_out = buf;
  return (code == 0xC0000005 || code == 0x80000003 || code > 255) ? -1 : (int)code;
}

void plat_kill(plat_pid_t pid)        { plat_kill_force(pid); }
void plat_kill_force(plat_pid_t pid)  {
  HANDLE h = win_open_pid(pid, PROCESS_TERMINATE);
  if (h) { TerminateProcess(h, 1); CloseHandle(h); }
}

int plat_wait_nohang(plat_pid_t pid, int *status, int *sig) {
  HANDLE h = win_open_pid(pid, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION);
  if (!h) { *status = 0; *sig = -1; return 0; }      /* process gone */
  DWORD r = WaitForSingleObject(h, 0);
  if (r == WAIT_TIMEOUT) { CloseHandle(h); return 1; }
  DWORD code = 0; GetExitCodeProcess(h, &code);
  CloseHandle(h);
  *status = (int)code; *sig = -1;
  return 0;
}

void plat_sleep_ms(int ms) { Sleep((DWORD)ms); }

char *plat_shell_read(const char *cmd) {
  FILE *fp = _popen(cmd, "rt");
  if (!fp) return NULL;
  size_t cap = 4096, n = 0; char *buf = malloc(cap); buf[0] = 0;
  for (;;) {
    if (n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    size_t r = fread(buf + n, 1, cap - n - 1, fp);
    if (r == 0) break;
    n += r; buf[n] = 0;
  }
  _pclose(fp);
  return buf;
}

void plat_chmod_exec(const char *path) { _chmod(path, _S_IREAD | _S_IWRITE | _S_IEXEC); }

int plat_spawn_run(char *const argv[], const char *cwd,
                   int *out_fd, int *err_fd, plat_pid_t *pid) {
  SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
  HANDLE or_ = NULL, ow = NULL, er = NULL, ew = NULL;
  if (!CreatePipe(&or_, &ow, &sa, 0)) return -1;
  if (!CreatePipe(&er, &ew, &sa, 0)) { CloseHandle(or_); CloseHandle(ow); return -1; }
  STARTUPINFOA si; PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof si); memset(&pi, 0, sizeof pi);
  si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = ow; si.hStdError = ew; si.hStdInput = NULL;
  char *cmd = win_cmdline(argv);
  if (!cmd) { CloseHandle(or_); CloseHandle(ow); CloseHandle(er); CloseHandle(ew); return -1; }
  if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
    free(cmd); CloseHandle(or_); CloseHandle(ow); CloseHandle(er); CloseHandle(ew);
    return -1;
  }
  free(cmd); CloseHandle(ow); CloseHandle(ew);
  *out_fd = _open_osfhandle((intptr_t)or_, _O_BINARY);
  *err_fd = _open_osfhandle((intptr_t)er, _O_BINARY);
  *pid = pi.dwProcessId;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}

void plat_set_nonblocking(int fd) { (void)fd; }   /* CRT fds: reads stay blocking */

int plat_file_access(const char *path, int mode) { return _access(path, mode); }

void plat_exe_dir(char *buf, int size) {
  char path[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (n == 0) { strncpy(buf, ".", (size_t)size); return; }
  char *slash = strrchr(path, '\\');
  if (slash) *slash = 0;
  strncpy(buf, path, (size_t)size); buf[size-1] = 0;
}

int plat_mkdtemp_dir(const char *pattern, char *buf, int size) {
  char base[MAX_PATH]; DWORD n = GetTempPathA(MAX_PATH, base);
  if (n == 0 || n >= MAX_PATH) strcpy(base, ".");
  static unsigned long seq = 0;
  for (int i = 0; i < 100; i++) {
    snprintf(buf, (size_t)size, "%s%s%lu", base, pattern, seq++);
    if (CreateDirectoryA(buf, NULL)) return 0;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return -1;
  }
  return -1;
}

long plat_getpid(void) { return (long)GetCurrentProcessId(); }

#else /* !_WIN32 ------------------------------------------------------------- */
/* ------------------------------------------------------------------ */
/* POSIX (Linux / macOS)                                               */
/* ------------------------------------------------------------------ */
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* read everything from fd into a malloc'd, NUL-terminated buffer */
static char *fd_read_all(int fd) {
  size_t cap = 65536, n = 0;
  char *buf = malloc(cap);
  buf[0] = 0;
  for (;;) {
    if (n + 4096 + 1 > cap) { while (n + 4096 + 1 > cap) cap *= 2; buf = realloc(buf, cap); }
    ssize_t r = read(fd, buf + n, 4096);
    if (r > 0) { n += (size_t)r; buf[n] = 0; }
    else if (r == 0) break;
    else if (errno != EINTR) break;
  }
  return buf;
}

int plat_spawn_capture(char *const argv[], const char *stdout_file, char **base_out) {
  *base_out = NULL;
  int p[2];
  if (pipe(p) != 0) return -1;
  plat_pid_t pid = fork();
  if (pid < 0) { close(p[0]); close(p[1]); return -1; }
  if (pid == 0) {
    close(p[0]);
    dup2(p[1], STDERR_FILENO);
    if (stdout_file) {
      int fd = open(stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) dup2(fd, STDOUT_FILENO);
      else close(STDOUT_FILENO);
    } else {
      dup2(p[1], STDOUT_FILENO);
    }
    if (p[1] > 2) close(p[1]);
    execvp(argv[0], argv);
    _exit(127);
  }
  close(p[1]);
  char *buf = fd_read_all(p[0]);
  close(p[0]);
  if (!buf) buf = strdup("");
  int status = 0; waitpid(pid, &status, 0);
  *base_out = buf;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -1;
}

void plat_kill(plat_pid_t pid)       { kill((pid_t)pid, SIGTERM); }
void plat_kill_force(plat_pid_t pid) { kill((pid_t)pid, SIGKILL); }

int plat_wait_nohang(plat_pid_t pid, int *status, int *sig) {
  int st = 0;
  pid_t r = waitpid((pid_t)pid, &st, WNOHANG);
  if (r != (pid_t)pid) return 1;                 /* still running */
  *sig = -1; *status = 0;
  if (WIFSIGNALED(st)) *sig = WTERMSIG(st);
  else if (WIFEXITED(st)) *status = WEXITSTATUS(st);
  return 0;
}

void plat_sleep_ms(int ms) {
  struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

char *plat_shell_read(const char *cmd) {
  FILE *fp = popen(cmd, "r");
  if (!fp) return NULL;
  size_t cap = 4096, n = 0; char *buf = malloc(cap); buf[0] = 0;
  for (;;) {
    if (n + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    size_t r = fread(buf + n, 1, cap - n - 1, fp);
    if (r == 0) break;
    n += r; buf[n] = 0;
  }
  pclose(fp);
  return buf;
}

void plat_chmod_exec(const char *path) { chmod(path, 0755); }

int plat_spawn_run(char *const argv[], const char *cwd,
                   int *out_fd, int *err_fd, plat_pid_t *pid) {
  int o[2], e[2];
  if (pipe(o) != 0 || pipe(e) != 0) {
    if (o[0] >= 0) { close(o[0]); close(o[1]); }
    return -1;
  }
  plat_pid_t p = fork();
  if (p < 0) { close(o[0]); close(o[1]); close(e[0]); close(e[1]); return -1; }
  if (p == 0) {
    close(o[0]); close(e[0]);
    dup2(o[1], STDOUT_FILENO);
    dup2(e[1], STDERR_FILENO);
    if (o[1] > 2) close(o[1]);
    if (e[1] > 2) close(e[1]);
    int nullfd = open("/dev/null", O_RDONLY);
    if (nullfd >= 0) dup2(nullfd, STDIN_FILENO);
    if (cwd) chdir(cwd);
    execvp(argv[0], argv);
    _exit(127);
  }
  close(o[1]); close(e[1]);
  *out_fd = o[0]; *err_fd = e[0]; *pid = p;
  return 0;
}

void plat_set_nonblocking(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int plat_file_access(const char *path, int mode) { return access(path, mode); }

void plat_exe_dir(char *buf, int size) {
  char path[PATH_MAX];
  int ok = 0;
#ifdef __linux__
  {
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) { path[n] = 0; ok = 1; }
  }
#else
  {
    /* macOS: dyld gives the absolute executable path without using argv */
    extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
    uint32_t sz = sizeof path;
    if (_NSGetExecutablePath(path, &sz) == 0) ok = 1;
  }
#endif
  if (!ok) { strncpy(buf, ".", (size_t)size); return; }
  char *slash = strrchr(path, '/');
  if (slash) *slash = 0;
  strncpy(buf, path, (size_t)size); buf[size - 1] = 0;
}

int plat_mkdtemp_dir(const char *pattern, char *buf, int size) {
  const char *base = getenv("TMPDIR");
  if (!base || !*base) base = "/tmp";
  char tmpl[PATH_MAX];
  snprintf(tmpl, sizeof tmpl, "%s/%s", base, pattern);
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof tmp, "%s", tmpl);
  char *ok = mkdtemp(tmp);
  if (!ok) return -1;
  strncpy(buf, tmp, (size_t)size); buf[size - 1] = 0;
  return 0;
}

long plat_getpid(void) { return (long)getpid(); }

#endif