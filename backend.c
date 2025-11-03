/**
 * @file_backend.c
 * @brief_The core logic engine for the C File Manager.
 *
 * This file contains all the functions that directly interact with the operating system's
 * file system. It uses low-level POSIX system calls to perform operations like reading
 * directories, getting file metadata, and manipulating files. This file acts as the
 * "engine" of the application, completely separate from the UI.
 */

// We include our own "backend.h" to get the function declarations and the FileInfo struct.
// This ensures our implementation matches the "contract" we defined in the header.
#include "backend.h"
// We include all the standard C library headers that give us access to the system calls we need.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>   // Provides the stat() system call for file metadata (inodes).
#include <dirent.h>     // Provides opendir(), readdir(), and closedir() for directory traversal.
#include <time.h>
#include <unistd.h>
#include <ftw.h>        // Provides nftw() for advanced file tree walking (used for recursive delete).
#include <fcntl.h>      // Provides open() and flags for file control (O_CREAT, O_RDONLY, etc.).
#include <errno.h>
#include <zip.h>        // The header for the external libzip library we use for compression.

// --- Helper Functions ---

/**
 * @brief Formats a file size in bytes into a human-readable string (KB, MB).
 * @param size The size of the file in bytes.
 * @return A newly allocated string with the formatted size. Must be freed with g_free().
 */
static gchar* format_size(off_t size) {
    // g_malloc is a safe way from the GLib library to request a small piece of memory.
    gchar *buf = g_malloc(32);
    if (size < 1024) g_snprintf(buf, 32, "%ld B", size);
    else if (size < 1024 * 1024) g_snprintf(buf, 32, "%.1f KB", (gdouble)size / 1024);
    else g_snprintf(buf, 32, "%.1f MB", (gdouble)size / (1024 * 1024));
    return buf;
}

/**
 * @brief A callback function used by GLib's GList to free the memory of a FileInfo struct.
 * In C, we must manually manage memory. This function ensures that when we are done
 * with a FileInfo object, all the memory allocated for it and its internal strings is returned.
 */
void free_file_info(gpointer data) {
    FileInfo *info = (FileInfo *)data;
    // We must free each string inside the struct first...
    g_free(info->name); g_free(info->path); g_free(info->type);
    g_free(info->size_formatted); g_free(info->modified); g_free(info->permissions);
    // ...and then free the memory for the struct container itself.
    g_free(info);
}

/**
 * @brief A simpler memory cleanup function for the Favourites list.
 */
void free_favourite_location(gpointer data) { g_free(data); }

// --- Core Data Fetching ---

/**
 * @brief Reads all the files and folders inside a given directory path.
 * @param path The absolute path of the directory to read.
 * @return A GList (a linked list from GLib) containing FileInfo structs for each item.
 */
GList* get_directory_contents(const gchar *path) {
    // Create an empty list to hold our results.
    GList *list = NULL;
    // The opendir() system call asks the OS kernel for a "handle" or "stream" to a directory.
    DIR *d = opendir(path);
    // CRITICAL ERROR HANDLING: If the kernel returns NULL, the directory doesn't exist or we
    // don't have permission to read it. We must stop immediately.
    if (!d) return NULL;

    // This struct will hold the info for each item as the kernel gives it to us.
    struct dirent *dir;
    // The readdir() system call, used in a loop, asks the kernel: "What's the next item in this directory?"
    // It keeps returning items until there are no more, at which point it returns NULL and the loop terminates.
    while ((dir = readdir(d)) != NULL) {
        // Every directory in a UNIX-like system contains entries for itself (".") and its parent ("..").
        // We must explicitly ignore these to prevent infinite loops and to provide a clean listing.
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        // We allocate a new block of memory for our custom FileInfo struct.
        FileInfo *info = g_new0(FileInfo, 1);
        // We must make our own copy of the item's name. The memory from `readdir` is temporary.
        info->name = g_strdup(dir->d_name);
        // We use a safe GLib function to construct the full path, e.g., "/path/to" + "file.txt".
        info->path = g_build_filename(path, dir->d_name, NULL);

        // This struct will be filled by the kernel with the file's metadata.
        struct stat st;
        // The stat() system call asks the kernel: "Tell me everything about the inode for this file."
        // The kernel fills our 'st' struct with the metadata (size, permissions, timestamps, etc.).
        if (stat(info->path, &st) == 0) { // A return value of 0 means the system call was successful.
            // S_ISDIR is a macro that checks a special bitmask (st_mode) to see if the item is a directory.
            info->is_dir = S_ISDIR(st.st_mode);
            info->type = g_strdup(info->is_dir ? "Directory" : "File");
            info->size_formatted = info->is_dir ? g_strdup("") : format_size(st.st_size);
            
            gchar time_buf[64];
            // We format the raw timestamp from the kernel into a human-readable string.
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
            info->modified = g_strdup(time_buf);
            
            // We also format the permissions bitmask into the familiar "-rwxr-xr-x" string.
            info->permissions = g_malloc(11);
            strmode(st.st_mode, info->permissions);
        }
        // We add the completed FileInfo struct to our list of results.
        list = g_list_append(list, info);
    }
    // The closedir() system call tells the kernel: "I am finished with this directory stream."
    // This is a critical step to release the underlying resources and prevent leaks.
    closedir(d);
    return list;
}

/**
 * @brief Builds the list of "Favourite" locations for the sidebar.
 */
GList* get_favourite_locations() {
    GList *list = NULL;
    // g_get_home_dir() is a convenient GLib function to find the current user's home folder path.
    const gchar* home = g_get_home_dir();
    // We create strings with a semicolon separator to bundle the display name and the actual path together.
    list = g_list_append(list, g_strdup_printf("🏠 Home;%s", home));
    list = g_list_append(list, g_strdup_printf("🖥️ Desktop;%s/Desktop", home));
    list = g_list_append(list, g_strdup_printf("📄 Documents;%s/Documents", home));
    list = g_list_append(list, g_strdup_printf("📥 Downloads;%s/Downloads", home));
    return list;
}

// --- File Operations ---

/**
 * @brief Creates a new directory.
 */
gboolean create_directory_item(const gchar *parent_dir, const gchar *dir_name) {
    // Build the full path for the new folder.
    gchar *path = g_build_filename(parent_dir, dir_name, NULL);
    // The mkdir() system call asks the kernel to create the new directory. 0755 sets its permissions.
    gboolean success = (mkdir(path, 0755) == 0);
    // We must free the memory we allocated for the path string.
    g_free(path);
    return success;
}

/**
 * @brief Creates a new, empty file.
 */
gboolean create_file_item(const gchar *parent_dir, const gchar *file_name) {
    gchar *path = g_build_filename(parent_dir, file_name, NULL);
    // The open() system call is the most powerful one. Here we ask the kernel to create a new file
    // for writing. O_EXCL means "fail if it already exists". 0644 sets permissions.
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    g_free(path);
    // If open() succeeds, it gives us a "file descriptor" (an integer). We close it right away.
    if (fd != -1) { close(fd); return TRUE; }
    return FALSE;
}

/**
 * @brief Renames a file or folder.
 */
gboolean rename_item(const gchar *old_path, const gchar *new_name) {
    gchar *dir = g_path_get_dirname(old_path);
    gchar *new_path = g_build_filename(dir, new_name, NULL);
    // The rename() system call is an atomic operation. It's extremely fast because it just changes
    // a name pointer in the filesystem metadata; it doesn't move any actual data.
    gboolean success = (rename(old_path, new_path) == 0);
    g_free(dir); g_free(new_path);
    return success;
}

/**
 * @brief A callback function used by nftw() for recursive deletion.
 * This function is called for every single item found during the file tree walk.
 */
static int unlink_cb(const gchar *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // The remove() system call deletes one file or one empty directory.
    return remove(fpath);
}

/**
 * @brief Deletes a file or an entire directory tree.
 */
gboolean delete_item(const gchar *path) {
    // nftw() stands for "file tree walk". It's a powerful function that traverses an entire
    // directory and all its subdirectories. We tell it to call our helper function (unlink_cb)
    // on every item it finds, effectively deleting everything from the inside out.
    return (nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS) == 0);
}

/**
 * @brief A helper function that copies the raw data from one file to another.
 */
static gboolean copy_file_content(const gchar *src, const gchar *dst) {
    int src_fd, dst_fd; // Integers to hold the "keys" (file descriptors) to our files.
    gchar buf[8192];    // A small bucket (8KB) to carry data between files.
    ssize_t nread;      // To keep track of how many bytes were read in each step.

    // Get a file descriptor for the source file (read-only).
    src_fd = open(src, O_RDONLY);
    if (src_fd == -1) return FALSE; // Always check for errors!

    // Get a file descriptor for the destination file (write-only, create if needed, overwrite if exists).
    dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) { close(src_fd); return FALSE; }

    // This is the main I/O loop. It continues as long as read() successfully reads data.
    // The read() system call fills our bucket with data from the source file.
    while ((nread = read(src_fd, buf, sizeof(buf))) > 0) {
        // The write() system call pours the data from our bucket into the destination file.
        if (write(dst_fd, buf, nread) != nread) {
            // If we couldn't write everything, something is wrong (e.g., disk is full).
            close(src_fd); close(dst_fd); return FALSE;
        }
    }
    // We're done, so we give back the file descriptors to the OS.
    close(src_fd); close(dst_fd);
    return nread == 0; // Success if the last read returned 0 (meaning we reached the end of the file).
}

/**
 * @brief Copies an item (file or directory) from a source to a destination.
 * This function uses recursion to handle directories.
 */
gboolean copy_item(const gchar *src_path, const gchar *dest_dir) {
    gchar *base = g_path_get_basename(src_path);
    gchar *dest_path = g_build_filename(dest_dir, base, NULL);
    gboolean result = TRUE;
    struct stat st;
    stat(src_path, &st); // First, we use stat() to check if the source is a file or a folder.

    if (S_ISDIR(st.st_mode)) { // If it's a folder...
        mkdir(dest_path, st.st_mode); // ...make a new empty folder at the destination.
        DIR *d = opendir(src_path);   // Then, open the source folder to see what's inside.
        if (d) {
            struct dirent *dir;
            while ((dir = readdir(d)) != NULL) { // Loop through every item inside.
                if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                gchar *new_src = g_build_filename(src_path, dir->d_name, NULL);
                // This is RECURSION! The function calls itself to copy the item it found inside.
                if (!copy_item(new_src, dest_path)) result = FALSE;
                g_free(new_src);
            }
            closedir(d);
        }
    } else { // If it's just a file...
        // ...we just call our helper to copy its data.
        result = copy_file_content(src_path, dest_path);
    }
    g_free(base); g_free(dest_path);
    return result;
}

/**
 * @brief Moves an item to a new directory.
 */
gboolean move_item(const gchar *src_path, const gchar *dest_dir) {
    gchar *base = g_path_get_basename(src_path);
    gchar *dest_path = g_build_filename(dest_dir, base, NULL);
    // We can just use the rename() system call. If the destination is in a different
    // folder on the same disk, the kernel just updates pointers. It's an instant, atomic operation.
    gboolean success = (rename(src_path, dest_path) == 0);
    g_free(base); g_free(dest_path);
    return success;
}

/**
 * @brief A recursive helper function to add files and directories to a zip archive.
 */
static void add_to_zip_recursive(zip_t *zip, const gchar *base_path_in_fs, const gchar *parent_path_in_zip) {
    DIR *d = opendir(base_path_in_fs);
    if (!d) return;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
        gchar *full_fs_path = g_build_filename(base_path_in_fs, dir->d_name, NULL);
        gchar *full_zip_path = g_strconcat(parent_path_in_zip, dir->d_name, NULL);
        struct stat st;
        stat(full_fs_path, &st);
        if (S_ISDIR(st.st_mode)) { // If the item is a folder...
            zip_dir_add(zip, full_zip_path, ZIP_FL_ENC_UTF_8); // ...add an empty folder entry to the zip.
            gchar *zip_dir_path = g_strconcat(full_zip_path, "/", NULL);
            // And then, RECURSION! Call this function again to add the contents of that folder.
            add_to_zip_recursive(zip, full_fs_path, zip_dir_path);
            g_free(zip_dir_path);
        } else { // If the item is a file...
            // ...we get the file's data as a "source"...
            zip_source_t *source = zip_source_file(zip, full_fs_path, 0, 0);
            // ...and add the source data to the zip archive.
            zip_file_add(zip, full_zip_path, source, ZIP_FL_ENC_UTF_8);
        }
        g_free(full_fs_path); g_free(full_zip_path);
    }
    closedir(d);
}

/**
 * @brief Compresses a file or directory into a .zip archive.
 */
gboolean zip_item(const gchar *src_path, const gchar *dest_zip_path) {
    int error;
    // We open a new, empty zip file for writing.
    zip_t *zip = zip_open(dest_zip_path, ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!zip) return FALSE;
    struct stat st;
    stat(src_path, &st);
    if (S_ISDIR(st.st_mode)) { // If we're zipping a folder...
        gchar *base = g_path_get_basename(src_path);
        gchar *base_zip_path = g_strconcat(base, "/", NULL);
        // First, add the main folder entry to the zip.
        zip_dir_add(zip, base, ZIP_FL_ENC_UTF_8);
        // Then, call our recursive helper to add everything inside it.
        add_to_zip_recursive(zip, src_path, base_zip_path);
        g_free(base); g_free(base_zip_path);
    } else { // If it's just a file...
        // ...we just add the single file to the zip.
        zip_source_t *source = zip_source_file(zip, src_path, 0, 0);
        zip_file_add(zip, g_path_get_basename(src_path), source, ZIP_FL_ENC_UTF_8);
    }
    // Finally, we close the zip file, which finalizes the archive and writes it to disk.
    return (zip_close(zip) == 0);
}