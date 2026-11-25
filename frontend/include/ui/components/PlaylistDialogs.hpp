/**
 * Lollypop C++ - Playlist Dialogs
 *
 * Small modal helpers for the playlist actions that used to fall back to the
 * queue: add tracks to a playlist (or create one), rename a playlist, and
 * confirm deletion.
 */

#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

#include "services/ILibraryService.hpp"
#include "types.hpp"

namespace lollypop {
namespace playlist_dialogs {

void show_add_to_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const std::vector<std::string>& track_ids);

void show_rename_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const Playlist& playlist);

void show_delete_playlist_dialog(GtkWindow* parent,
                                 ILibraryService* library,
                                 const Playlist& playlist);

} // namespace playlist_dialogs
} // namespace lollypop
