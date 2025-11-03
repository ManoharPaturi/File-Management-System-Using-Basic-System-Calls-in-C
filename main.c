/**
 * @file main.c
 * @brief The main user interface and event handling logic for the C File Manager.
 *
 * This file is responsible for creating the graphical user interface (GUI) using the GTK
 * library. It defines the application's structure, handles user input through signals
 * and callbacks, and communicates with the backend to perform file system operations.
 * This architectural separation of UI from logic is a key software engineering principle.
 */

// Include the GTK library header, which provides all the necessary functions
// and data types for building the graphical user interface.
#include <gtk/gtk.h>
// Include our custom backend header. This acts as a contract, allowing this file
// to use the functions declared in `backend.h` without needing to know their
// internal implementation details.
#include "backend.h"

// --- Global Application State ---
// These variables are declared globally, meaning they are accessible from any function
// within this file. They are used to maintain the application's state—its memory
// of what the user is currently doing.

gchar *current_path = NULL;     // A string that stores the absolute path of the directory the user is currently viewing.
gchar *clipboard_path = NULL;   // A string that stores the path of the file/folder that has been copied or cut.
gchar *clipboard_op = NULL;     // A string that remembers the last clipboard operation: either "copy" or "move".

// --- Global UI Widget Pointers ---
// We need global pointers to certain UI elements (widgets) so that different functions
// can interact with them. For example, a menu click function might need to update the file list.

GtkListStore *store;        // A pointer to the GTK "ListStore". This is the data model—an invisible container
                            // that holds all the rows and columns of data for our file list.
GtkTreeView *tree_view;     // A pointer to the GTK "TreeView". This is the visible widget that displays the
                            // data from the GtkListStore in a user-friendly, scrollable list.
GtkEntry *path_entry;       // A pointer to the text entry box at the top, used to display and edit the current path.
GtkWidget *context_menu;    // A pointer to the right-click context menu widget.
GtkWidget *paste_menu_item; // A specific pointer to the "Paste" item within the context menu. This allows us
                            // to enable or disable it based on whether the clipboard is empty.

// --- Forward Declarations ---
// In C, a function must be declared before it is used. Since many of our functions
// call each other, we declare all of their "signatures" here at the top to inform the
// compiler of their existence and prevent compilation errors.

void refresh_view();
static void on_row_activated(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data);
static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void on_rename(GtkMenuItem *item, gpointer data);
static void on_delete(GtkMenuItem *item, gpointer data);
static void on_copy(GtkMenuItem *item, gpointer data);
static void on_cut(GtkMenuItem *item, gpointer data);
static void on_paste(GtkMenuItem *item, gpointer data);
static void on_zip(GtkMenuItem *item, gpointer data);
static void on_create_folder(GtkMenuItem *item, gpointer data);
static void on_create_file(GtkMenuItem *item, gpointer data);

// --- Helper to get selected path ---
/**
 * @brief Retrieves the full path of the currently selected item in the file list.
 * @return A newly allocated string containing the path, or NULL if nothing is selected.
 * The caller is responsible for freeing this string with g_free().
 */
static gchar* get_selected_path() {
    // Get the selection object associated with our tree view.
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter; // An "iterator" is like a pointer to a specific row in our data model.

    // This function checks if a row is currently selected. If so, it points our 'iter' to it.
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *path;
        // We retrieve the data from the 5th column (index 4), which is where we secretly stored the full path.
        gtk_tree_model_get(model, &iter, 4, &path, -1);
        return path;
    }
    return NULL; // Return NULL if no row was selected.
}

// --- UI Creation ---

/**
 * @brief Constructs the right-click context menu and connects its signals.
 * This function builds the menu in memory but does not show it.
 */
static void create_context_menu() {
    context_menu = gtk_menu_new(); // Create a new, empty menu widget.
    // Create each individual menu item with its visible label.
    GtkWidget *create_folder_item = gtk_menu_item_new_with_label("New Folder");
    GtkWidget *create_file_item = gtk_menu_item_new_with_label("New File");
    GtkWidget *rename_item = gtk_menu_item_new_with_label("Rename");
    GtkWidget *delete_item = gtk_menu_item_new_with_label("Delete");
    GtkWidget *copy_item = gtk_menu_item_new_with_label("Copy");
    GtkWidget *cut_item = gtk_menu_item_new_with_label("Cut");
    paste_menu_item = gtk_menu_item_new_with_label("Paste");
    GtkWidget *zip_item = gtk_menu_item_new_with_label("Compress (ZIP)");

    // This is the core of event-driven programming. `g_signal_connect` tells GTK:
    // "When the 'activate' signal occurs on this widget (i.e., the user clicks it),
    // please execute the function I'm providing (e.g., on_create_folder)."
    // The function that gets called is known as a "callback function".
    g_signal_connect(create_folder_item, "activate", G_CALLBACK(on_create_folder), NULL);
    g_signal_connect(create_file_item, "activate", G_CALLBACK(on_create_file), NULL);
    g_signal_connect(rename_item, "activate", G_CALLBACK(on_rename), NULL);
    g_signal_connect(delete_item, "activate", G_CALLBACK(on_delete), NULL);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), NULL);
    g_signal_connect(cut_item, "activate", G_CALLBACK(on_cut), NULL);
    g_signal_connect(paste_menu_item, "activate", G_CALLBACK(on_paste), NULL);
    g_signal_connect(zip_item, "activate", G_CALLBACK(on_zip), NULL);

    // We now add all the created items to the menu widget in the desired order,
    // using separators to create logical groups.
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), create_folder_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), create_file_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), rename_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), delete_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), cut_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), paste_menu_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(context_menu), zip_item);
    // This function makes the menu widget and all its children ready to be displayed when called.
    gtk_widget_show_all(context_menu);
}

/**
 * @brief The callback function executed when a "Favourite" location button is clicked.
 */
static void on_favourite_clicked(GtkButton *button, gpointer data) {
    // The 'data' passed to this function is the string like "🏠 Home;/Users/manohar".
    // We split this string at the semicolon to separate the display name from the actual path.
    gchar **split = g_strsplit(g_strdup(data), ";", 2);
    // We must free the memory of the old path before assigning a new one to prevent a memory leak.
    g_free(current_path);
    // We update the application's state by setting the new current path.
    current_path = g_strdup(split[1]);
    // We call refresh_view to update the file list to show the contents of the new directory.
    refresh_view();
    // We free the memory used by the temporary split string array.
    g_strfreev(split);
}

// --- Main App Activation ---
/**
 * @brief This is the primary function that constructs the entire application window and its widgets.
 * It is called by the GTK framework when the application is launched.
 */
static void activate(GtkApplication *app, gpointer user_data) {
    // Create the main application window.
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Gemini C File Manager Pro ✨");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

    // Create a "paned" widget, which is a container with a draggable divider.
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(window), paned);

    // Create the sidebar (a vertical box container).
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_paned_add1(GTK_PANED(paned), sidebar); // Add the sidebar to the left pane.
    // Call our backend function to get the list of favourite locations.
    GList *favourites = get_favourite_locations();
    // Loop through the list and create a button for each favourite location.
    for (GList *l = favourites; l != NULL; l = l->next) {
        gchar **split = g_strsplit(l->data, ";", 2);
        GtkWidget *btn = gtk_button_new_with_label(split[0]);
        // Connect the button's "clicked" signal to our on_favourite_clicked callback.
        g_signal_connect(btn, "clicked", G_CALLBACK(on_favourite_clicked), g_strdup(l->data));
        gtk_box_pack_start(GTK_BOX(sidebar), btn, FALSE, FALSE, 0);
        g_strfreev(split);
    }
    g_list_free_full(favourites, free_favourite_location); // Clean up the memory used by the list.

    // Create the main content area (another vertical box).
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_paned_add2(GTK_PANED(paned), main_box); // Add it to the right pane.
    // Create the path entry text box and assign it to our global pointer.
    path_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_pack_start(GTK_BOX(main_box), GTK_WIDGET(path_entry), FALSE, FALSE, 0);

    // Create a scrolled window. This widget provides scrollbars if its content is too large.
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled_window, TRUE, TRUE, 0);

    // Create the data model for our list. It has 6 columns: Name, Size, Type, Modified, Full Path, Is Directory.
    store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    // Create the visible TreeView widget and connect it to our data model.
    tree_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)));
    gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(tree_view));

    // Create the visible columns for the list (Name, Size, etc.).
    const char *cols[] = {"Name", "Size", "Type", "Modified"};
    for (int i=0; i<4; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new(); // How to draw the text.
        // Create a column, give it a title, and tell it which column of data from the 'store' to display (index 'i').
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(cols[i], r, "text", i, NULL);
        gtk_tree_view_append_column(tree_view, c);
    }

    // Connect the signals for double-clicking ("row-activated") and right-clicking ("button-press-event").
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_row_activated), NULL);
    g_signal_connect(tree_view, "button-press-event", G_CALLBACK(on_button_press), NULL);

    // Call our function to build the right-click menu and prepare it.
    create_context_menu();

    // Set the application's starting path to the user's home directory.
    current_path = g_strdup(g_get_home_dir());
    // Call refresh_view() for the first time to load the initial list of files.
    refresh_view();

    // Finally, after building everything in memory, this function displays the window and all its contents.
    gtk_widget_show_all(window);
}

/**
 * @brief Reloads and displays the contents of the `current_path` directory.
 */
void refresh_view() {
    // First, clear out all the old items from the data model to prevent duplicates.
    gtk_list_store_clear(store);
    // Update the path entry box to show the correct current path.
    gtk_entry_set_text(path_entry, current_path);
    // Call our backend function to get a fresh list of files for the current path.
    GList *files = get_directory_contents(current_path);
    // Loop through the linked list of FileInfo structs returned by the backend.
    for (GList *l = files; l != NULL; l = l->next) {
        FileInfo *info = (FileInfo *)l->data;
        GtkTreeIter iter;
        // Add a new, empty row to our data model.
        gtk_list_store_append(store, &iter);
        // Fill the new row with the data from the FileInfo struct, column by column.
        gtk_list_store_set(store, &iter, 0, info->name, 1, info->size_formatted, 2, info->type, 3, info->modified, 4, info->path, 5, info->is_dir, -1);
    }
    // CRITICAL MEMORY MANAGEMENT: The backend allocated memory for the list. We must free it now
    // to prevent a memory leak. `g_list_free_full` calls our `free_file_info` on each item.
    g_list_free_full(files, free_file_info);
}

/**
 * @brief Callback for when a user double-clicks a row in the file list.
 */
static void on_row_activated(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data) {
    GtkTreeIter iter;
    // Get an iterator (pointer) to the specific row that was double-clicked.
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path)) {
        gboolean is_dir; gchar *file_path;
        // Get the data for that row from our model.
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 5, &is_dir, 4, &file_path, -1);
        if (is_dir) { // If the item was a folder...
            // ...update the current path and refresh the view to navigate into it.
            g_free(current_path);
            current_path = file_path;
            refresh_view();
        } else { // If the item was a file...
            // ...we build a command like "open '/path/to/file.txt'" and ask the OS to run it.
            // This will open the file with its default application.
            gchar *command = g_strconcat("open ", "\"", file_path, "\"", NULL);
            system(command);
            g_free(command);
            g_free(file_path);
        }
    }
}

/**
 * @brief Callback for when a mouse button is pressed on the file list. Used to show the context menu.
 */
static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    // We check if the event was a right-click (button 3, or button 1 + Control key on Mac).
    if (event->type == GDK_BUTTON_PRESS && (event->button == GDK_BUTTON_SECONDARY || (event->button == GDK_BUTTON_PRIMARY && event->state & GDK_CONTROL_MASK))) {
        // Before showing the menu, we check if there's anything on our clipboard.
        // If there is, we enable the "Paste" menu item. If not, we disable it.
        gtk_widget_set_sensitive(paste_menu_item, clipboard_path != NULL);
        // This function shows the context menu at the current mouse pointer's location.
        gtk_menu_popup_at_pointer(GTK_MENU(context_menu), (GdkEvent*)event);
        return TRUE; // We have handled this event completely.
    }
    return FALSE; // It was not a right-click, so we let GTK handle it normally.
}

// --- Action Implementations ---
// All these functions are the "callbacks" for our context menu items.
// They all follow the same pattern:
// 1. Get the selected path.
// 2. Create a dialog box to ask the user for input if needed.
// 3. Call the correct function from our backend to do the real work (the system call).
// 4. Clean up any memory we used (free strings, destroy dialogs).
// 5. Refresh the view to show the changes.

static void on_rename(GtkMenuItem *item, gpointer data) {
    gchar *path = get_selected_path();
    if (!path) return;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Rename", GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tree_view))), GTK_DIALOG_MODAL, "_OK", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), g_path_get_basename(path));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    // gtk_dialog_run is "modal" - it pauses this function until the user clicks a button.
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        rename_item(path, gtk_entry_get_text(GTK_ENTRY(entry)));
    }
    gtk_widget_destroy(dialog); // Always destroy dialogs after use.
    g_free(path);
    refresh_view();
}

static void on_delete(GtkMenuItem *item, gpointer data) {
    gchar *path = get_selected_path();
    if (!path) return;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tree_view))), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Delete '%s' permanently?", g_path_get_basename(path));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        delete_item(path);
    }
    gtk_widget_destroy(dialog);
    g_free(path);
    refresh_view();
}

static void on_copy(GtkMenuItem *item, gpointer data) {
    g_free(clipboard_path); g_free(clipboard_op); // Free old clipboard data first.
    clipboard_path = get_selected_path();
    clipboard_op = g_strdup("copy");
}

static void on_cut(GtkMenuItem *item, gpointer data) {
    g_free(clipboard_path); g_free(clipboard_op);
    clipboard_path = get_selected_path();
    clipboard_op = g_strdup("move");
}

static void on_paste(GtkMenuItem *item, gpointer data) {
    if (!clipboard_path) return;
    if (g_strcmp0(clipboard_op, "copy") == 0) {
        copy_item(clipboard_path, current_path);
    } else if (g_strcmp0(clipboard_op, "move") == 0) {
        move_item(clipboard_path, current_path);
        // After a move, the clipboard should be cleared.
        g_free(clipboard_path); clipboard_path = NULL;
        g_free(clipboard_op); clipboard_op = NULL;
    }
    refresh_view();
}

static void on_zip(GtkMenuItem *item, gpointer data) {
    gchar *path = get_selected_path();
    if (!path) return;
    gchar *zip_name = g_strconcat(g_path_get_basename(path), ".zip", NULL);
    gchar *dest_path = g_build_filename(current_path, zip_name, NULL);
    zip_item(path, dest_path);
    g_free(path); g_free(zip_name); g_free(dest_path);
    refresh_view();
}

static void on_create_folder(GtkMenuItem *item, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("New Folder", GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(tree_view))), GTK_DIALOG_MODAL, "_Create", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_REJECT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), "Untitled Folder");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        create_directory_item(current_path, gtk_entry_get_text(GTK_ENTRY(entry)));
    }
    gtk_widget_destroy(dialog);
    refresh_view();
}

static void on_create_file(GtkMenuItem *item, gpointer data) {
    // For simplicity, this action doesn't ask for a name.
    create_file_item(current_path, "untitled file.txt");
    refresh_view();
}

// This is the entry point of our entire application.
int main(int argc, char **argv) {
    // Create a new GTK application instance. This sets up the connection to the windowing system.
    GtkApplication *app = gtk_application_new("com.gemini.filemanager.pro", G_APPLICATION_DEFAULT_FLAGS);
    // Tell the application: "When you are ready to start, call my 'activate' function."
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    // Start the application and enter the GTK main loop. This function will not return until the user closes the window.
    // It sits and waits for user events (clicks, key presses) to happen.
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    // When the user closes the window, the loop ends. We clean up our global variables to be good citizens.
    g_object_unref(app);
    g_free(current_path); g_free(clipboard_path); g_free(clipboard_op);
    return status;
}
