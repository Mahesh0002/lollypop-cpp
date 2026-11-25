/**
 * Lollypop C++ - Playlist Dialogs Implementation
 */

#include "ui/components/PlaylistDialogs.hpp"

#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace lollypop {
namespace playlist_dialogs {

namespace {

constexpr const char* kNewPlaylist = "<New playlist…>";

struct AddContext {
    ILibraryService* library;
    std::vector<std::string> track_ids;
    GtkWidget* combo;
    GtkWidget* entry;
};

struct RenameContext {
    ILibraryService* library;
    std::string playlist_id;
    GtkWidget* entry;
};

struct DeleteContext {
    ILibraryService* library;
    std::string playlist_id;
};

std::string find_playlist_id(ILibraryService* library,
                             const std::string& name) {
    if (!library || name.empty()) return {};
    for (const auto& playlist : library->get_all_playlists()) {
        if (playlist.name == name) return playlist.id;
    }
    return {};
}

void close_dialog(GtkDialog* dialog) {
    gtk_window_destroy(GTK_WINDOW(dialog));
}

void on_add_response(GtkDialog* dialog, gint response, gpointer user_data) {
    auto* ctx = static_cast<AddContext*>(user_data);
    if (!ctx) {
        close_dialog(dialog);
        return;
    }

    if (response == GTK_RESPONSE_ACCEPT) {
        gchar* selected =
            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ctx->combo));
        const std::string chosen = selected ? selected : "";
        g_free(selected);

        std::string playlist_id;
        if (chosen == kNewPlaylist) {
            const std::string new_name = gtk_editable_get_text(GTK_EDITABLE(ctx->entry));
            if (!new_name.empty() && ctx->library->create_playlist(new_name)) {
                playlist_id = find_playlist_id(ctx->library, new_name);
            }
        } else {
            playlist_id = find_playlist_id(ctx->library, chosen);
        }

        if (!playlist_id.empty()) {
            ctx->library->add_tracks_to_playlist(playlist_id, ctx->track_ids);
        }
    }

    close_dialog(dialog);
    delete ctx;
}

void on_rename_response(GtkDialog* dialog, gint response, gpointer user_data) {
    auto* ctx = static_cast<RenameContext*>(user_data);
    if (!ctx) {
        close_dialog(dialog);
        return;
    }

    if (response == GTK_RESPONSE_ACCEPT) {
        const std::string name = gtk_editable_get_text(GTK_EDITABLE(ctx->entry));
        if (!name.empty()) {
            ctx->library->rename_playlist(ctx->playlist_id, name);
        }
    }

    close_dialog(dialog);
    delete ctx;
}

void on_delete_response(GtkDialog* dialog, gint response, gpointer user_data) {
    auto* ctx = static_cast<DeleteContext*>(user_data);
    if (!ctx) {
        close_dialog(dialog);
        return;
    }

    if (response == GTK_RESPONSE_ACCEPT) {
        ctx->library->delete_playlist(ctx->playlist_id);
    }

    close_dialog(dialog);
    delete ctx;
}

void attach_box(GtkWidget* dialog, GtkWidget* box) {
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_visible(box, TRUE);
    gtk_box_append(GTK_BOX(area), box);
}

GtkWidget* create_dialog(const char* title, GtkWindow* parent,
                         const char* confirm_label) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title, parent, GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL,
        confirm_label, GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return dialog;
}

} // namespace

void show_add_to_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const std::vector<std::string>& track_ids) {
    if (!library || track_ids.empty()) return;

    GtkWidget* dialog =
        create_dialog("Add to Playlist", parent, "_Add");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget* prompt = gtk_label_new("Choose a playlist:");
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.0);
    gtk_widget_set_halign(prompt, GTK_ALIGN_START);
    gtk_widget_set_visible(prompt, TRUE);
    gtk_box_append(GTK_BOX(box), prompt);

    GtkWidget* combo = gtk_combo_box_text_new();
    for (const auto& playlist : library->get_all_playlists()) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                       playlist.name.c_str());
    }
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), kNewPlaylist);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_widget_set_visible(combo, TRUE);
    gtk_box_append(GTK_BOX(box), combo);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(entry), "New playlist name (only used for the new playlist)");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_visible(entry, TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    attach_box(dialog, box);

    auto* ctx = new AddContext{library, track_ids, combo, entry};
    g_signal_connect(dialog, "response", G_CALLBACK(on_add_response), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

void show_rename_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const Playlist& playlist) {
    if (!library || playlist.id.empty()) return;

    GtkWidget* dialog = create_dialog("Rename Playlist", parent, "_Rename");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget* prompt = gtk_label_new("Playlist name:");
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.0);
    gtk_widget_set_halign(prompt, GTK_ALIGN_START);
    gtk_widget_set_visible(prompt, TRUE);
    gtk_box_append(GTK_BOX(box), prompt);

    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), playlist.name.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_visible(entry, TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    attach_box(dialog, box);

    auto* ctx = new RenameContext{library, playlist.id, entry};
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_response), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

void show_delete_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const Playlist& playlist) {
    if (!library || playlist.id.empty()) return;

    GtkWidget* dialog = create_dialog("Delete Playlist?", parent, "_Delete");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    const std::string message = "Delete “" + playlist.name + "”? This cannot be undone.";
    GtkWidget* prompt = gtk_label_new(message.c_str());
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.0);
    gtk_label_set_wrap(GTK_LABEL(prompt), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(prompt), 48);
    gtk_widget_set_halign(prompt, GTK_ALIGN_START);
    gtk_widget_set_visible(prompt, TRUE);
    gtk_box_append(GTK_BOX(box), prompt);

    attach_box(dialog, box);

    auto* ctx = new DeleteContext{library, playlist.id};
    g_signal_connect(dialog, "response", G_CALLBACK(on_delete_response), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

} // namespace playlist_dialogs
} // namespace lollypop
