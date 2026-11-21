/**
 * Lollypop C++ - MPRIS Manager Implementation
 * Uses Gio::DBus to watch org.mpris.MediaPlayer2.* on the session bus.
 */

#include "mpris_manager.hpp"

#include <giomm.h>
#include <glib.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace lollypop {
namespace {

constexpr const char* kMprisPrefix = "org.mpris.MediaPlayer2.";
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr const char* kBusService = "org.freedesktop.DBus";
constexpr const char* kBusPath = "/org/freedesktop/DBus";
constexpr guint kPositionPollIntervalMs = 500;

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool cached_property(const Glib::RefPtr<Gio::DBus::Proxy>& proxy,
                     const char* name,
                     Glib::VariantBase& out) {
    if (!proxy) return false;
    Glib::VariantBase value;
    proxy->get_cached_property(value, name);
    if (!value) return false;
    out = value;
    return true;
}

std::string cached_string(const Glib::RefPtr<Gio::DBus::Proxy>& proxy, const char* name) {
    Glib::VariantBase value;
    if (!cached_property(proxy, name, value)) return {};
    try {
        return Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get().raw();
    } catch (...) {
        return {};
    }
}

bool cached_bool(const Glib::RefPtr<Gio::DBus::Proxy>& proxy, const char* name, bool fallback) {
    Glib::VariantBase value;
    if (!cached_property(proxy, name, value)) return fallback;
    try {
        return Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(value).get();
    } catch (...) {
        return fallback;
    }
}

double cached_double(const Glib::RefPtr<Gio::DBus::Proxy>& proxy, const char* name, double fallback) {
    Glib::VariantBase value;
    if (!cached_property(proxy, name, value)) return fallback;
    try {
        return Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(value).get();
    } catch (...) {
        return fallback;
    }
}

std::string join_artists(const std::vector<Glib::ustring>& artists) {
    std::string joined;
    for (const auto& artist : artists) {
        if (!joined.empty()) joined += ", ";
        joined += artist.raw();
    }
    return joined;
}

void parse_metadata(const Glib::VariantBase& value, MprisPlayerInfo& info) {
    if (!value) return;

    try {
        auto dict = Glib::VariantDict::create(value);
        Glib::ustring text;

        if (dict->lookup_value("xesam:title", text)) {
            info.track_title = text.raw();
        }

        std::vector<Glib::ustring> artists;
        if (dict->lookup_value("xesam:artist", artists)) {
            info.track_artist = join_artists(artists);
        }

        // Album artist is a separate MPRIS property.
        Glib::ustring album_artist;
        if (dict->lookup_value("xesam:albumArtist", album_artist)) {
            info.track_album_artist = album_artist.raw();
        }

        if (dict->lookup_value("xesam:album", text)) {
            info.track_album = text.raw();
        }

        Glib::DBusObjectPathString track_id;
        if (dict->lookup_value("mpris:trackid", track_id)) {
            info.track_id = track_id.raw();
        }

        if (dict->lookup_value("mpris:artUrl", text)) {
            info.track_art_url = text.raw();
        }

        gint64 length_us = 0;
        if (dict->lookup_value("mpris:length", length_us)) {
            info.duration = static_cast<double>(length_us) / 1'000'000.0;
        }
    } catch (const Glib::Error& error) {
        std::cerr << "MprisManager: Failed to parse metadata: " << error.what() << std::endl;
    } catch (...) {
        // Ignore malformed metadata from misbehaving players.
    }
}

int loop_state_from_string(const std::string& status) {
    if (status == "Playlist") return 1;
    if (status == "Track") return 2;
    return 0;
}

const char* loop_state_to_string(int state) {
    if (state == 1) return "Playlist";
    if (state == 2) return "Track";
    return "None";
}

}  // namespace

struct MprisManager::Impl {
    struct PlayerEntry {
        MprisPlayerInfo info;
        // Separate proxies: root proxy for Identity/DesktopEntry, player proxy
        // for playback control and properties.
        Glib::RefPtr<Gio::DBus::Proxy> base;
        Glib::RefPtr<Gio::DBus::Proxy> player;
        Glib::RefPtr<Gio::DBus::Proxy> properties;
        sigc::connection properties_conn;
        sigc::connection seeked_conn;
    };

    Glib::RefPtr<Gio::DBus::Connection> bus;
    std::vector<PlayerEntry> players;

    std::string active_name;
    std::string manual_name;
    std::string preferred_name;  // stored lowercase
    std::string tracked_name;    // last player seen playing
    std::vector<std::string> mru_;  // most-recently-used player names (front = most recent)
    bool filter_duplicates = true;

    // Single subscription to NameOwnerChanged (well-known names only).
    guint name_subscription = 0;
    guint position_timer = 0;

    // Generation counter for async D-Bus replies. Each request is tagged with
    // a generation; stale replies (from a previous active player or a
    // superseded request) are dropped. (plan.md §14.6)
    std::atomic<uint64_t> request_generation_{0};
    // Outstanding position poll: one per active player at a time.
    std::atomic<uint64_t> position_poll_generation_{0};
    std::atomic<bool> position_poll_outstanding_{false};

    PlayerListChangedCallback player_list_cb;
    ActivePlayerChangedCallback active_player_cb;
    PlaybackStateChangedCallback playback_state_cb;
    TrackChangedCallback track_cb;
    PositionChangedCallback position_cb;
    VolumeChangedCallback volume_cb;
    LoopStateChangedCallback loop_state_cb;
    ShuffleChangedCallback shuffle_cb;

    PlayerEntry* find(const std::string& name) {
        auto it = std::find_if(players.begin(), players.end(),
                               [&name](const PlayerEntry& entry) {
                                   return entry.info.dbus_name == name;
                               });
        return it == players.end() ? nullptr : &*it;
    }

    const PlayerEntry* find(const std::string& name) const {
        auto it = std::find_if(players.begin(), players.end(),
                               [&name](const PlayerEntry& entry) {
                                   return entry.info.dbus_name == name;
                               });
        return it == players.end() ? nullptr : &*it;
    }

    PlayerEntry* active_entry() { return find(active_name); }
    const PlayerEntry* active_entry() const { return find(active_name); }

    // MRU management (plan.md §14.3): move a player to the front of the
    // most-recently-used list when it starts playing.
    void touch_mru(const std::string& name) {
        auto it = std::find(mru_.begin(), mru_.end(), name);
        if (it != mru_.end()) {
            mru_.erase(it);
        }
        mru_.insert(mru_.begin(), name);
        // Cap the MRU list to avoid unbounded growth.
        if (mru_.size() > 16) mru_.pop_back();
    }

    void remove_from_mru(const std::string& name) {
        auto it = std::find(mru_.begin(), mru_.end(), name);
        if (it != mru_.end()) mru_.erase(it);
    }

    bool is_filtered(const std::string& name,
                     const std::vector<std::string>& all_names) const {
        // Never list Lollypop itself as an external player.
        if (name.starts_with("org.mpris.MediaPlayer2.lollypop")) return true;

        if (!filter_duplicates) return false;

        // playerctld mirrors other buses; showing it duplicates every player.
        if (name.starts_with("org.mpris.MediaPlayer2.playerctld")) return true;

        // Ignore non-instance mpd buses.
        if (name.size() > 4 && name.ends_with(".mpd") &&
            !name.ends_with("MediaPlayer2.mpd")) {
            return true;
        }

        // Hide native browser buses while plasma-browser-integration is active.
        const bool has_plasma = std::any_of(
            all_names.begin(), all_names.end(), [](const std::string& candidate) {
                return candidate.starts_with(
                    "org.mpris.MediaPlayer2.plasma-browser-integration");
            });
        if (has_plasma &&
            (name.starts_with("org.mpris.MediaPlayer2.firefox") ||
             name.starts_with("org.mpris.MediaPlayer2.chromium") ||
             name.starts_with("org.mpris.MediaPlayer2.google-chrome"))) {
            return true;
        }

        return false;
    }

    void read_props(PlayerEntry& entry) {
        const std::string status = cached_string(entry.player, "PlaybackStatus");
        if (!status.empty()) {
            entry.info.playback_status = status;
            entry.info.is_playing = (status == "Playing");
        }

        entry.info.can_control = cached_bool(entry.player, "CanControl", false);
        entry.info.can_seek = cached_bool(entry.player, "CanSeek", false);
        entry.info.can_go_next = cached_bool(entry.player, "CanGoNext", false);
        entry.info.can_go_previous = cached_bool(entry.player, "CanGoPrevious", false);
        entry.info.can_play = cached_bool(entry.player, "CanPlay", false);
        entry.info.can_pause = cached_bool(entry.player, "CanPause", false);
        entry.info.volume = cached_double(entry.player, "Volume", entry.info.volume);
        entry.info.shuffle = cached_bool(entry.player, "Shuffle", entry.info.shuffle);

        const std::string loop_status = cached_string(entry.player, "LoopStatus");
        if (!loop_status.empty()) {
            entry.info.loop_state = loop_state_from_string(loop_status);
        }

        Glib::VariantBase metadata;
        if (cached_property(entry.player, "Metadata", metadata)) {
            parse_metadata(metadata, entry.info);
        }
    }

    // Asynchronously query the Position property; invokes cb with the
    // position in seconds (or 0.0 on failure). Stale replies are dropped by
    // comparing against @p gen.
    void query_position_async(const std::string& name,
                              uint64_t gen,
                              std::function<void(double)> cb) {
        PlayerEntry* entry = find(name);
        if (!entry || !entry->properties) {
            if (cb) cb(0.0);
            return;
        }

        Glib::VariantContainerBase params =
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<Glib::ustring>::create(kPlayerInterface),
                Glib::Variant<Glib::ustring>::create("Position"),
            });
        auto props = entry->properties;
        auto cb_self = std::move(cb);
        props->call("Get",
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, gen, props, cb_self = std::move(cb_self)](
                const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (position_poll_generation_.load() != gen) {
                    if (cb_self) cb_self(0.0);
                    return;
                }
                try {
                    auto reply = props->call_finish(result);
                    auto boxed = Glib::VariantBase::cast_dynamic<
                        Glib::Variant<Glib::Variant<gint64>>>(
                            reply.get_child(0));
                    const gint64 position_us = boxed.get().get();
                    if (cb_self)
                        cb_self(static_cast<double>(position_us) /
                                1'000'000.0);
                } catch (...) {
                    if (cb_self) cb_self(0.0);
                }
            }),
            params,
            -1);
    }

    void set_property_async(PlayerEntry& entry, const char* name,
                            const Glib::VariantBase& boxed_value) {
        if (!entry.properties) return;

        uint64_t gen = ++request_generation_;
        Glib::VariantContainerBase params =
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<Glib::ustring>::create(kPlayerInterface),
                Glib::Variant<Glib::ustring>::create(name),
                boxed_value,
            });
        auto props = entry.properties;
        // Fire-and-forget async Set; stale replies are dropped by generation.
        props->call("Set",
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, gen, props](const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (request_generation_.load() != gen) return;
                try {
                    props->call_finish(result);
                } catch (const Glib::Error& error) {
                    std::cerr << "MprisManager: async Set failed: "
                              << error.what() << std::endl;
                } catch (...) {
                }
            }),
            params,
            -1);
    }

    bool add_player(const std::string& name) {
        try {
            auto base = Gio::DBus::Proxy::create_sync(
                bus, name, kObjectPath, kRootInterface,
                Glib::RefPtr<Gio::DBus::InterfaceInfo>(),
                Gio::DBus::ProxyFlags::DO_NOT_AUTO_START);
            auto player = Gio::DBus::Proxy::create_sync(
                bus, name, kObjectPath, kPlayerInterface,
                Glib::RefPtr<Gio::DBus::InterfaceInfo>(),
                Gio::DBus::ProxyFlags::DO_NOT_AUTO_START);
            auto properties = Gio::DBus::Proxy::create_sync(
                bus, name, kObjectPath, kPropertiesInterface,
                Glib::RefPtr<Gio::DBus::InterfaceInfo>(),
                Gio::DBus::ProxyFlags::DO_NOT_AUTO_START);

            if (!player || !properties) return false;

            PlayerEntry entry;
            entry.info.dbus_name = name;
            // Root proxy provides Identity and DesktopEntry.
            entry.info.identity = cached_string(base, "Identity");
            entry.info.desktop_entry = cached_string(base, "DesktopEntry");
            entry.base = base;
            entry.player = player;
            entry.properties = properties;

            read_props(entry);

            entry.properties_conn = player->signal_properties_changed().connect(
                [this, name](
                    const Gio::DBus::Proxy::MapChangedProperties& changed,
                    const std::vector<Glib::ustring>& invalidated) {
                    on_properties_changed(name, changed, invalidated);
                });

            entry.seeked_conn = player->signal_signal().connect(
                [this, name](const Glib::ustring&, const Glib::ustring& signal_name,
                             const Glib::VariantContainerBase& parameters) {
                    on_player_signal(name, signal_name, parameters);
                });

            std::cout << "MprisManager: Added player "
                      << (entry.info.identity.empty() ? name : entry.info.identity)
                      << " (" << name << ")" << std::endl;

            players.push_back(std::move(entry));
            return true;
        } catch (const Glib::Error& error) {
            std::cerr << "MprisManager: Failed to connect to " << name << ": "
                      << error.what() << std::endl;
            return false;
        }
    }

    void refresh_players() {
        if (!bus) return;

        Glib::VariantContainerBase params =
            Glib::VariantContainerBase::create_tuple(
                std::vector<Glib::VariantBase>{});
        auto bus_ref = bus;
        bus->call(kBusPath, kBusService, "ListNames", params,
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, bus_ref](const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (!bus_ref) return;
                try {
                    auto reply = bus_ref->call_finish(result);
                    auto list = Glib::VariantBase::cast_dynamic<
                        Glib::Variant<std::vector<Glib::ustring>>>(
                            reply.get_child(0));
                    std::vector<std::string> names;
                    for (const auto& name : list.get()) {
                        names.push_back(name.raw());
                    }
                    apply_player_list(names);
                } catch (const Glib::Error& error) {
                    std::cerr << "MprisManager: Failed to list bus names: "
                              << error.what() << std::endl;
                } catch (...) {
                }
            }),
            kBusService, -1, Gio::DBus::CallFlags::NONE,
            Glib::VariantType("(as)"));
    }

    void apply_player_list(const std::vector<std::string>& names) {
        bool changed = false;

        // Drop players that vanished or are now considered duplicates.
        for (auto it = players.begin(); it != players.end();) {
            const std::string& name = it->info.dbus_name;
            const bool present =
                std::find(names.begin(), names.end(), name) != names.end();
            if (!present || is_filtered(name, names)) {
                // Disconnect signal handlers before removing the player.
                it->properties_conn.disconnect();
                it->seeked_conn.disconnect();
                remove_from_mru(name);
                if (name == active_name) {
                    active_name.clear();
                }
                it = players.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        // Add newly appeared players.
        for (const auto& name : names) {
            if (!name.starts_with(kMprisPrefix) || find(name) ||
                is_filtered(name, names)) {
                continue;
            }
            if (add_player(name)) changed = true;
        }

        if (changed && player_list_cb) {
            player_list_cb();
        }

        update_active_selection();
    }

    void on_name_owner_changed(const Glib::RefPtr<Gio::DBus::Connection>&,
                               const Glib::ustring& /*sender_name*/,
                               const Glib::ustring& /*object_path*/,
                               const Glib::ustring& /*interface_name*/,
                               const Glib::ustring& /*signal_name*/,
                               const Glib::VariantContainerBase& parameters) {
        // NameOwnerChanged signal parameters: (name, old_owner, new_owner)
        Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::ustring>> params;
        try {
            params = Glib::VariantBase::cast_dynamic<
                Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::ustring>>>(
                    parameters.get_child(0));
        } catch (...) {
            return;
        }
        auto [name, old_owner, new_owner] = params.get();

        // Distinguish well-known names from unique bus names.
        // Only react to well-known MPRIS names; ignore unique-name changes.
        if (name.compare(0, Glib::ustring(kMprisPrefix).size(), kMprisPrefix) !=
            0) {
            return;
        }
        refresh_players();
    }

    void on_properties_changed(
        const std::string& name,
        const Gio::DBus::Proxy::MapChangedProperties& changed,
        const std::vector<Glib::ustring>& /*invalidated*/) {
        PlayerEntry* entry = find(name);
        if (!entry) return;

        const std::string previous_track =
            entry->info.track_id + "|" + entry->info.track_title + "|" +
            entry->info.track_artist + "|" + entry->info.track_album;
        const bool was_playing = entry->info.is_playing;

        read_props(*entry);

        const std::string current_track =
            entry->info.track_id + "|" + entry->info.track_title + "|" +
            entry->info.track_artist + "|" + entry->info.track_album;

        if (entry->info.is_playing && !was_playing) {
            tracked_name = name;
            touch_mru(name);
        }

        if (name == active_name) {
            const bool track_changed = (current_track != previous_track);
            if (track_changed && track_cb) {
                track_cb(entry->info);
            }
            if (was_playing != entry->info.is_playing && playback_state_cb) {
                playback_state_cb(entry->info.is_playing);
            }
            if (changed.count("Volume") && volume_cb) {
                volume_cb(entry->info.volume);
            }
            if (changed.count("LoopStatus") && loop_state_cb) {
                loop_state_cb(entry->info.loop_state);
            }
            if (changed.count("Shuffle") && shuffle_cb) {
                shuffle_cb(entry->info.shuffle);
            }
            if (active_player_cb) {
                active_player_cb(entry->info);
            }
        }

        update_active_selection();
    }

    void on_player_signal(const std::string& name, const Glib::ustring& signal_name,
                          const Glib::VariantContainerBase& parameters) {
        if (signal_name != "Seeked") return;

        PlayerEntry* entry = find(name);
        if (!entry) return;

        try {
            const gint64 position_us =
                Glib::VariantBase::cast_dynamic<Glib::Variant<gint64>>(
                    parameters.get_child(0))
                    .get();
            entry->info.position = static_cast<double>(position_us) / 1'000'000.0;
            if (name == active_name && position_cb) {
                position_cb(entry->info.position, entry->info.duration);
            }
        } catch (...) {
            // Ignore malformed Seeked payloads.
        }
    }

    void update_active_selection() {
        std::string candidate;

        if (!manual_name.empty() && find(manual_name)) {
            candidate = manual_name;
        }

        if (candidate.empty() && !preferred_name.empty()) {
            for (const auto& entry : players) {
                if (to_lower(entry.info.identity).find(preferred_name) !=
                        std::string::npos ||
                    to_lower(entry.info.desktop_entry).find(preferred_name) !=
                        std::string::npos ||
                    to_lower(entry.info.dbus_name).find(preferred_name) !=
                        std::string::npos) {
                    candidate = entry.info.dbus_name;
                    break;
                }
            }
        }

        // MRU: the most recently used player that is currently playing.
        if (candidate.empty()) {
            for (const auto& name : mru_) {
                if (find(name) && find(name)->info.is_playing) {
                    candidate = name;
                    break;
                }
            }
        }

        if (candidate.empty() && !tracked_name.empty() && find(tracked_name)) {
            candidate = tracked_name;
        }

        if (candidate.empty() && !players.empty()) {
            candidate = players.front().info.dbus_name;
        }

        if (candidate == active_name) return;

        active_name = candidate;

        if (active_player_cb) {
            active_player_cb(get_active_info());
        }
        if (track_cb) {
            const PlayerEntry* entry = find(active_name);
            if (entry && !entry->info.track_title.empty()) {
                track_cb(entry->info);
            }
        }
        if (playback_state_cb) {
            const PlayerEntry* entry = find(active_name);
            playback_state_cb(entry && entry->info.is_playing);
        }
    }

    MprisPlayerInfo get_active_info() const {
        const PlayerEntry* entry = find(active_name);
        return entry ? entry->info : MprisPlayerInfo{};
    }

    void poll_position() {
        PlayerEntry* entry = active_entry();
        if (!entry || !entry->info.is_playing) return;

        // One outstanding Position poll per player at a time (plan.md §14.7).
        if (position_poll_outstanding_.exchange(true)) return;

        uint64_t gen = ++position_poll_generation_;
        auto name = entry->info.dbus_name;
        query_position_async(name, gen,
            [this, gen](double position) {
                // Drop stale results if a newer poll superseded this one.
                if (position_poll_generation_.load() != gen) {
                    position_poll_outstanding_ = false;
                    return;
                }
                position_poll_outstanding_ = false;
                PlayerEntry* e = active_entry();
                if (e && e->info.is_playing) {
                    e->info.position = position;
                    if (position_cb) {
                        position_cb(position, e->info.duration);
                    }
                }
            });
    }

    static gboolean position_tick(gpointer data) {
        static_cast<Impl*>(data)->poll_position();
        return G_SOURCE_CONTINUE;
    }

    // Asynchronous playback commands to avoid blocking the UI thread.
    void call_active_async(const char* method) {
        PlayerEntry* entry = active_entry();
        if (!entry || !entry->player) return;

        uint64_t gen = ++request_generation_;
        Glib::VariantContainerBase params;
        auto player = entry->player;
        player->call(method,
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, gen, player](const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (request_generation_.load() != gen) return;
                try {
                    player->call_finish(result);
                } catch (const Glib::Error& error) {
                    std::cerr << "MprisManager: async command failed: "
                              << error.what() << std::endl;
                } catch (...) {
                }
            }),
            params,
            -1);
    }
};

MprisManager::MprisManager() : impl_(std::make_unique<Impl>()) {}

MprisManager::~MprisManager() {
    shutdown();
}

bool MprisManager::initialize() {
    if (impl_->bus) return true;

    Gio::init();

    try {
        impl_->bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisManager: Cannot connect to session bus: "
                  << error.what() << std::endl;
        return false;
    }

    // Single subscription to NameOwnerChanged, matching well-known MPRIS names
    // only. The callback receives name, old_owner, new_owner.
    impl_->name_subscription = impl_->bus->signal_subscribe(
        sigc::mem_fun(*impl_, &Impl::on_name_owner_changed),
        kBusService, kBusService, "NameOwnerChanged", kBusPath, kMprisPrefix,
        Gio::DBus::SignalFlags::MATCH_ARG0_NAMESPACE);

    impl_->refresh_players();

    impl_->position_timer =
        g_timeout_add(kPositionPollIntervalMs, &Impl::position_tick, impl_.get());

    return true;
}

void MprisManager::shutdown() {
    if (impl_->position_timer) {
        g_source_remove(impl_->position_timer);
        impl_->position_timer = 0;
    }
    if (impl_->name_subscription && impl_->bus) {
        impl_->bus->signal_unsubscribe(impl_->name_subscription);
        impl_->name_subscription = 0;
    }
    // Disconnect all per-player signal handlers before clearing.
    for (auto& entry : impl_->players) {
        entry.properties_conn.disconnect();
        entry.seeked_conn.disconnect();
    }
    impl_->players.clear();
    impl_->active_name.clear();
    impl_->manual_name.clear();
    impl_->tracked_name.clear();
    impl_->bus.reset();
}

bool MprisManager::is_initialized() const {
    return static_cast<bool>(impl_->bus);
}

std::vector<MprisPlayerInfo> MprisManager::get_players() const {
    std::vector<MprisPlayerInfo> result;
    result.reserve(impl_->players.size());
    for (const auto& entry : impl_->players) {
        result.push_back(entry.info);
    }
    return result;
}

std::optional<MprisPlayerInfo> MprisManager::get_active_player() const {
    const auto* entry = impl_->find(impl_->active_name);
    if (!entry) return std::nullopt;
    return entry->info;
}

std::string MprisManager::get_active_player_name() const {
    return impl_->active_name;
}

void MprisManager::set_active_player(const std::string& dbus_name) {
    impl_->manual_name = dbus_name;
    impl_->update_active_selection();
}

void MprisManager::set_preferred_player(const std::string& name) {
    impl_->preferred_name = to_lower(name);
    impl_->update_active_selection();
}

void MprisManager::set_filter_duplicates(bool enabled) {
    if (impl_->filter_duplicates == enabled) return;
    impl_->filter_duplicates = enabled;
    impl_->refresh_players();
}

void MprisManager::play_pause() {
    impl_->call_active_async("PlayPause");
}

void MprisManager::play() {
    impl_->call_active_async("Play");
}

void MprisManager::pause() {
    impl_->call_active_async("Pause");
}

void MprisManager::stop() {
    impl_->call_active_async("Stop");
}

void MprisManager::next() {
    impl_->call_active_async("Next");
}

void MprisManager::previous() {
    impl_->call_active_async("Previous");
}

void MprisManager::seek(double position_seconds) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry || !entry->player) return;
    if (!entry->info.can_seek) return;

    const double clamped = std::max(0.0, position_seconds);

    // Absolute seek: SetPosition(track_id, position_us).
    if (!entry->info.track_id.empty()) {
        uint64_t gen = ++impl_->request_generation_;
        Glib::VariantContainerBase params =
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<Glib::DBusObjectPathString>::create(
                    Glib::DBusObjectPathString(entry->info.track_id)),
                Glib::Variant<gint64>::create(
                    static_cast<gint64>(clamped * 1'000'000.0)),
            });
        auto player = entry->player;
        player->call("SetPosition",
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, gen, player](const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (impl_->request_generation_.load() != gen) return;
                try {
                    player->call_finish(result);
                } catch (const Glib::Error& error) {
                    std::cerr << "MprisManager: async SetPosition failed: "
                              << error.what() << std::endl;
                } catch (...) {}
            }),
            params,
            -1);
        entry->info.position = clamped;
        return;
    }
}

void MprisManager::seek_by(double offset_seconds) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry || !entry->player) return;
    if (!entry->info.can_seek) return;

    try {
        uint64_t gen = ++impl_->request_generation_;
        Glib::VariantContainerBase params =
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<gint64>::create(
                    static_cast<gint64>(offset_seconds * 1'000'000.0)),
            });
        auto player = entry->player;
        player->call("Seek",
            sigc::slot<void(const Glib::RefPtr<Gio::AsyncResult>&)>([this, gen, player](const Glib::RefPtr<Gio::AsyncResult>& result) {
                if (impl_->request_generation_.load() != gen) return;
                try {
                    player->call_finish(result);
                } catch (const Glib::Error& error) {
                    std::cerr << "MprisManager: async Seek failed: "
                              << error.what() << std::endl;
                } catch (...) {}
            }),
            params,
            -1);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisManager: Seek failed: " << error.what() << std::endl;
    }
}

void MprisManager::set_volume(double volume) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    const double clamped = std::clamp(volume, 0.0, 1.0);
    impl_->set_property_async(
        *entry, "Volume",
        Glib::Variant<Glib::Variant<double>>::create(
            Glib::Variant<double>::create(clamped)));
    entry->info.volume = clamped;
}

void MprisManager::set_loop_state(int state) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    const int clamped = std::clamp(state, 0, 2);
    impl_->set_property_async(
        *entry, "LoopStatus",
        Glib::Variant<Glib::Variant<Glib::ustring>>::create(
            Glib::Variant<Glib::ustring>::create(
                loop_state_to_string(clamped))));
    entry->info.loop_state = clamped;
}

void MprisManager::set_shuffle(bool shuffle) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    impl_->set_property_async(
        *entry, "Shuffle",
        Glib::Variant<Glib::Variant<bool>>::create(
            Glib::Variant<bool>::create(shuffle)));
    entry->info.shuffle = shuffle;
}

bool MprisManager::can_control() const {
    const auto* entry = impl_->active_entry();
    return entry && entry->info.can_control;
}

bool MprisManager::can_go_next() const {
    const auto* entry = impl_->active_entry();
    return entry && entry->info.can_go_next;
}

bool MprisManager::can_go_previous() const {
    const auto* entry = impl_->active_entry();
    return entry && entry->info.can_go_previous;
}

bool MprisManager::can_seek() const {
    const auto* entry = impl_->active_entry();
    return entry && entry->info.can_seek;
}

double MprisManager::get_volume() const {
    const auto* entry = impl_->active_entry();
    return entry ? entry->info.volume : 0.0;
}

double MprisManager::get_position() const {
    const auto* entry = impl_->active_entry();
    return entry ? entry->info.position : 0.0;
}

double MprisManager::get_duration() const {
    const auto* entry = impl_->active_entry();
    return entry ? entry->info.duration : 0.0;
}

void MprisManager::set_player_list_changed_callback(PlayerListChangedCallback cb) {
    impl_->player_list_cb = std::move(cb);
}

void MprisManager::set_active_player_changed_callback(ActivePlayerChangedCallback cb) {
    impl_->active_player_cb = std::move(cb);
}

void MprisManager::set_playback_state_changed_callback(PlaybackStateChangedCallback cb) {
    impl_->playback_state_cb = std::move(cb);
}

void MprisManager::set_track_changed_callback(TrackChangedCallback cb) {
    impl_->track_cb = std::move(cb);
}

void MprisManager::set_position_changed_callback(PositionChangedCallback cb) {
    impl_->position_cb = std::move(cb);
}

void MprisManager::set_volume_changed_callback(VolumeChangedCallback cb) {
    impl_->volume_cb = std::move(cb);
}

void MprisManager::set_loop_state_changed_callback(LoopStateChangedCallback cb) {
    impl_->loop_state_cb = std::move(cb);
}

void MprisManager::set_shuffle_changed_callback(ShuffleChangedCallback cb) {
    impl_->shuffle_cb = std::move(cb);
}

}  // namespace lollypop
