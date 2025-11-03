/**
 * @file backend.h
 * @brief The public API and data structures for the File Manager's backend.
 *
 * This header file acts as the "contract" or "table of contents" for our backend logic.
 * It declares the functions and data structures that the user interface (main.c)
 * is allowed to use. This separation of declaration from implementation is a fundamental
 * principle of modular programming in C.
 */

// These are "include guards". They are a standard C practice to prevent the compiler
// from including this file more than once if multiple files reference it. This avoids
// "redeclaration" errors.
#ifndef BACKEND_H
#define BACKEND_H

// We include the header for the GLib library. GLib is a powerful utility library
// that provides advanced and safe data structures, like the GList (a linked list)
// and gchar (a string type), which are more robust than standard C equivalents.
#include <glib.h>

// This defines a "struct", which is a custom data type that groups related variables.
// Think of it as a blueprint for a "FileInfo" object, which will hold all the
// important metadata for a single file or folder.
typedef struct {
    gchar *name;            // The file's name, e.g., "report.pdf"
    gchar *path;            // The full, absolute path to the file, e.g., "/Users/user/Documents/report.pdf"
    gchar *type;            // A string describing the item, either "File" or "Directory"
    gchar *size_formatted;  // The file size, formatted for human readability, e.g., "1.2 MB"
    gchar *modified;        // The timestamp of the last modification, as a string.
    gchar *permissions;     // The file's permissions in the standard UNIX format, e.g., "-rwxr-xr-x"
    gboolean is_dir;        // A simple TRUE/FALSE flag for efficient checking if the item is a directory.
} FileInfo;

// --- Function Declarations (The Public API) ---
// The following lines are function prototypes. They do not contain code, but instead
// promise the compiler that these functions exist somewhere else (in backend.c).
// This allows other files, like main.c, to use these functions legally.

// --- Functions for Getting Information ---

// Retrieves a list of all files and folders within a specified directory.
GList* get_directory_contents(const gchar *path);

// A helper function to properly free all the memory allocated for a single FileInfo struct.
// This is crucial for preventing memory leaks.
void free_file_info(gpointer data);

// Retrieves a list of common "Favourite" locations for the sidebar.
GList* get_favourite_locations();

// A helper function to free the memory used by the favourites list strings.
void free_favourite_location(gpointer data);


// --- Declarations for all our file manipulation capabilities ---

// Creates a new, empty directory at the specified location.
gboolean create_directory_item(const gchar *parent_dir, const gchar *dir_name);

// Creates a new, empty file at the specified location.
gboolean create_file_item(const gchar *parent_dir, const gchar *file_name);

// Renames a file or folder.
gboolean rename_item(const gchar *old_path, const gchar *new_name);

// Deletes a file or an entire directory tree recursively.
gboolean delete_item(const gchar *path);

// Copies a file or directory tree to a new location.
gboolean copy_item(const gchar *src_path, const gchar *dest_dir);

// Moves a file or directory to a new location.
gboolean move_item(const gchar *src_path, const gchar *dest_dir);

// Compresses a file or directory into a .zip archive.
gboolean zip_item(const gchar *src_path, const gchar *dest_zip_path);


// This ends the include guard block that was started at the top of the file.
#endif // BACKEND_H
