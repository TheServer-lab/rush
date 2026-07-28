#ifndef RUSH_DISK_H
#define RUSH_DISK_H

/* Disk/volume/partition management. All of this is Windows-only (it
 * shells out to diskpart / the format command / PowerShell's
 * Resize-Partition) - on other platforms every function here prints
 * an error and returns -1 without touching anything.
 *
 * This module only does the OS mechanics. All safety gating (auth
 * requirement, -force requirement, retyped confirmation) lives in the
 * cmd_* wrappers in commands.c - by the time any of these functions
 * runs, the user has already confirmed. */

/* Prints diskpart's "list disk" output: number, status, size, free. */
int disk_list(void);

/* Prints a drive letter / label / filesystem / size / free table for
 * every volume, via PowerShell's Get-Volume. */
int disk_list_volumes(void);

/* Prints diskpart's "list partition" output for the given disk. */
int disk_list_partitions(int disknum);

/* Formats drive (e.g. "D:") with filesystem fs (e.g. "ntfs"). */
int disk_format(const char *drive, const char *fs);

/* Creates a new primary partition on disk disknum. size_mb <= 0 means
 * "use all remaining free space on the disk". */
int disk_partition_create(int disknum, long size_mb);

/* Deletes the given partition on the given disk. */
int disk_partition_delete(int disknum, int partnum);

/* Resizes the given partition to an absolute target size in bytes
 * (grows or shrinks as needed to reach it). */
int disk_partition_resize(int disknum, int partnum, long long size_bytes);

#endif
