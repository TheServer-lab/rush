#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <signal.h>
  #include <unistd.h>
#endif

#include "rush.h"
#include "launch.h"

typedef struct {
    int in_use;
    int slot;      /* the number shown to the user via list pros/kill pros */
    char name[64]; /* the launch target's name, e.g. "game1" */
#ifdef _WIN32
    HANDLE handle;
    DWORD pid;
#else
    pid_t pid;
#endif
} LaunchedProcess;

static LaunchedProcess g_procs[RUSH_MAX_PROCESSES];
static int g_next_slot = 1;

/* Splits path into a directory part (out) so the child can be started
 * with that as its working directory - many games/programs expect to
 * run from their own folder to find their assets. If there's no
 * separator, out is left as "." (current directory). */
static void dirname_of(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *cut = slash;
    if (bslash && (!cut || bslash > cut)) cut = bslash;
    if (!cut) { strncpy(out, ".", outsz - 1); out[outsz - 1] = '\0'; return; }
    size_t len = (size_t)(cut - path);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

int launch_spawn(const char *name, const char *path) {
    int idx = -1;
    for (int i = 0; i < RUSH_MAX_PROCESSES; i++) {
        if (!g_procs[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        rush_error("too many tracked processes, kill some first (list pros)");
        return -1;
    }

    char workdir[RUSH_MAX_LINE];
    dirname_of(path, workdir, sizeof(workdir));

#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    /* CreateProcessA requires a writable command line buffer. */
    char cmdline[RUSH_MAX_LINE];
    strncpy(cmdline, path, sizeof(cmdline) - 1);
    cmdline[sizeof(cmdline) - 1] = '\0';

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, workdir, &si, &pi)) {
        rush_error("could not launch %s", path);
        return -1;
    }
    CloseHandle(pi.hThread);
    g_procs[idx].handle = pi.hProcess;
    g_procs[idx].pid = pi.dwProcessId;
#else
    pid_t pid = fork();
    if (pid < 0) {
        rush_error("could not fork to launch %s", path);
        return -1;
    }
    if (pid == 0) {
        if (chdir(workdir) != 0) { /* not fatal - fall back to inherited cwd */ }
        execl(path, path, (char *)NULL);
        _exit(127); /* only reached if execl failed */
    }
    g_procs[idx].pid = pid;
#endif

    g_procs[idx].in_use = 1;
    g_procs[idx].slot = g_next_slot++;
    strncpy(g_procs[idx].name, name, sizeof(g_procs[idx].name) - 1);
    g_procs[idx].name[sizeof(g_procs[idx].name) - 1] = '\0';
    printf("launched %s as pros no. %d\n", name, g_procs[idx].slot);
    return 0;
}

static int is_still_running(LaunchedProcess *p) {
#ifdef _WIN32
    DWORD code = 0;
    if (!GetExitCodeProcess(p->handle, &code)) return 0;
    return code == STILL_ACTIVE;
#else
    int status;
    pid_t r = waitpid(p->pid, &status, WNOHANG);
    return r == 0; /* 0 = still running; >0 or -1 = exited/reaped/gone */
#endif
}

void launch_list(void) {
    int any = 0;
    for (int i = 0; i < RUSH_MAX_PROCESSES; i++) {
        if (!g_procs[i].in_use) continue;
        any = 1;
        int running = is_still_running(&g_procs[i]);
        printf("%s pros no. %d (pid %ld) - %s\n",
               g_procs[i].name, g_procs[i].slot, (long)g_procs[i].pid,
               running ? "running" : "exited");
        if (!running) {
#ifdef _WIN32
            CloseHandle(g_procs[i].handle);
#endif
            g_procs[i].in_use = 0;
        }
    }
    if (!any) printf("no tracked processes\n");
}

int launch_kill(int slot) {
    for (int i = 0; i < RUSH_MAX_PROCESSES; i++) {
        if (g_procs[i].in_use && g_procs[i].slot == slot) {
#ifdef _WIN32
            TerminateProcess(g_procs[i].handle, 1);
            CloseHandle(g_procs[i].handle);
#else
            kill(g_procs[i].pid, SIGKILL);
            waitpid(g_procs[i].pid, NULL, 0);
#endif
            g_procs[i].in_use = 0;
            printf("killed pros no. %d\n", slot);
            return 0;
        }
    }
    rush_error("no tracked process with pros no. %d", slot);
    return -1;
}
