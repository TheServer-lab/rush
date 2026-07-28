#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rush.h"
#include "disk.h"

#ifdef _WIN32

/* Writes script_body to a small temp diskpart script, runs
 * "diskpart /s <script>" (so its own output prints straight to
 * rush's console), then cleans up the temp file. */
static int run_diskpart_script(const char *script_body) {
    const char *scriptpath = "rush_diskpart_tmp.txt";
    FILE *f = fopen(scriptpath, "w");
    if (!f) {
        rush_error("could not create a temporary diskpart script");
        return -1;
    }
    fputs(script_body, f);
    fclose(f);

    char cmdline[512];
    snprintf(cmdline, sizeof(cmdline), "diskpart /s \"%s\"", scriptpath);
    fflush(stdout);
    int rc = system(cmdline);
    remove(scriptpath);

    if (rc != 0) {
        rush_error("diskpart exited with a nonzero status - try running rush as administrator");
        return -1;
    }
    return 0;
}

int disk_list(void) {
    fflush(stdout);
    int rc = system(
        "powershell -NoProfile -Command \""
        "Get-Disk | Format-Table Number,FriendlyName,"
        "@{Name='SizeGB';Expression={[math]::round($_.Size/1GB,2)}},"
        "PartitionStyle,OperationalStatus -AutoSize\""
    );
    if (rc != 0) {
        rush_error("could not list disks via PowerShell");
        return -1;
    }
    return 0;
}

int disk_list_volumes(void) {
    fflush(stdout);
    int rc = system(
        "powershell -NoProfile -Command \""
        "Get-Volume | Format-Table DriveLetter,FileSystemLabel,FileSystem,"
        "@{Name='SizeGB';Expression={[math]::round($_.Size/1GB,2)}},"
        "@{Name='FreeGB';Expression={[math]::round($_.SizeRemaining/1GB,2)}} -AutoSize\""
    );
    if (rc != 0) {
        rush_error("could not list volumes via PowerShell");
        return -1;
    }
    return 0;
}

int disk_list_partitions(int disknum) {
    char cmdline[256];
    snprintf(cmdline, sizeof(cmdline),
             "powershell -NoProfile -Command \""
             "Get-Partition -DiskNumber %d | Format-Table PartitionNumber,DriveLetter,"
             "@{Name='SizeGB';Expression={[math]::round($_.Size/1GB,2)}},Type -AutoSize\"",
             disknum);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) {
        rush_error("could not list partitions via PowerShell");
        return -1;
    }
    return 0;
}

int disk_format(const char *drive, const char *fs) {
    char cmdline[256];
    snprintf(cmdline, sizeof(cmdline), "format %s /FS:%s /Q /Y", drive, fs);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) {
        rush_error("format exited with a nonzero status - try running rush as administrator");
        return -1;
    }
    return 0;
}

int disk_partition_create(int disknum, long size_mb) {
    char script[160];
    if (size_mb > 0) {
        snprintf(script, sizeof(script), "select disk %d\ncreate partition primary size=%ld\n", disknum, size_mb);
    } else {
        /* diskpart uses all remaining free space when size= is omitted */
        snprintf(script, sizeof(script), "select disk %d\ncreate partition primary\n", disknum);
    }
    return run_diskpart_script(script);
}

int disk_partition_delete(int disknum, int partnum) {
    char script[160];
    snprintf(script, sizeof(script), "select disk %d\nselect partition %d\ndelete partition\n", disknum, partnum);
    return run_diskpart_script(script);
}

int disk_partition_resize(int disknum, int partnum, long long size_bytes) {
    char cmdline[256];
    snprintf(cmdline, sizeof(cmdline),
             "powershell -NoProfile -Command \"Resize-Partition -DiskNumber %d -PartitionNumber %d -Size %lld\"",
             disknum, partnum, size_bytes);
    fflush(stdout);
    int rc = system(cmdline);
    if (rc != 0) {
        rush_error("could not resize partition via PowerShell");
        return -1;
    }
    return 0;
}

#else /* not _WIN32 */

static int windows_only(void) {
    rush_error("disk management commands are Windows-only for now");
    return -1;
}

int disk_list(void) { return windows_only(); }
int disk_list_volumes(void) { return windows_only(); }
int disk_list_partitions(int disknum) { (void)disknum; return windows_only(); }
int disk_format(const char *drive, const char *fs) { (void)drive; (void)fs; return windows_only(); }
int disk_partition_create(int disknum, long size_mb) { (void)disknum; (void)size_mb; return windows_only(); }
int disk_partition_delete(int disknum, int partnum) { (void)disknum; (void)partnum; return windows_only(); }
int disk_partition_resize(int disknum, int partnum, long long size_bytes) {
    (void)disknum; (void)partnum; (void)size_bytes;
    return windows_only();
}

#endif
