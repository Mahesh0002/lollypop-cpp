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
    bool filter_duplicates = true;

    guint name_subscription = 0;
    guint position_timer = 0;

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

    bool is_filtered(const std::string& name,
                     const std::vector<std::string>& all_names) const {
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

    double query_position(const std::string& name) {
        PlayerEntry* entry = find(name);
        if (!entry || !entry->properties) return 0.0;

        try {
            auto reply = entry->properties->call_sync(
                "Get",
                Glib::VariantContainerBase::create_tuple({
                    Glib::Variant<Glib::ustring>::create(kPlayerInterface),
                    Glib::Variant<Glib::ustring>::create("Position"),
                }));
            auto boxed = Glib::VariantBase::cast_dynamic<
                Glib::Variant<Glib::Variant<gint64>>>(reply.get_child(0));
            const gint64 position_us = boxed.get().get();
            return static_cast<double>(position_us) / 1'000'000.0;
        } catch (...) {
            return 0.0;
        }
    }

    void set_property(PlayerEntry& entry, const char* name,
                      const Glib::VariantBase& boxed_value) {
        if (!entry.properties) return;
        try {
            entry.properties->call_sync(
                "Set",
                Glib::VariantContainerBase::create_tuple({
                    Glib::Variant<Glib::ustring>::create(kPlayerInterface),
                    Glib::Variant<Glib::ustring>::create(name),
                    boxed_value,
                }));
        } catch (const Glib::Error& error) {
            std::cerr << "MprisManager: Failed to set " << name << ": "
                      << error.what() << std::endl;
        }
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

        std::vector<std::string> names;
        try {
            auto reply = bus->call_sync(
                kBusPath, kBusService, "ListNames",
                Glib::VariantContainerBase::create_tuple(
                    std::vector<Glib::VariantBase>{}),
                kBusService, -1, Gio::DBus::CallFlags::NONE,
                Glib::VariantType("(as)"));
            auto list = Glib::VariantBase::cast_dynamic<
                Glib::Variant<std::vector<Glib::ustring>>>(reply.get_child(0));
            for (const auto& name : list.get()) {
                names.push_back(name.raw());
            }
        } catch (const Glib::Error& error) {
            std::cerr << "MprisManager: Failed to list bus names: "
                      << error.what() << std::endl;
            return;
        }

        bool changed = false;

        // Drop players that vanished or are now considered duplicates.
        for (auto it = players.begin(); it != players.end();) {
            const std::string& name = it->info.dbus_name;
            const bool present =
                std::find(names.begin(), names.end(), name) != names.end();
            if (!present || is_filtered(name, names)) {
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
                               const Glib::ustring&, const Glib::ustring&,
                               const Glib::ustring&, const Glib::ustring&,
                               const Glib::VariantContainerBase&) {
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

        entry->info.position = query_position(entry->info.dbus_name);
        if (position_cb) {
            position_cb(entry->info.position, entry->info.duration);
        }
    }

    static gboolean position_tick(gpointer data) {
        static_cast<Impl*>(data)->poll_position();
        return G_SOURCE_CONTINUE;
    }

    void call_active(const char* method) {
        PlayerEntry* entry = active_entry();
        if (!entry || !entry->player) return;
        try {
            entry->player->call_sync(method);
        } catch (const Glib::Error& error) {
            std::cerr << "MprisManager: " << method << " failed: "
                      << error.what() << std::endl;
        }
    }
};

MprisManager::MprisManager() : impl_(std::make_unique<Impl>()) {}

MprisManager::~MprisManager() {
    shutdown();
}

bool MprisManager::initialize() {
    if (impl_->bus) return true;

    // Required for glibmm's GObject wrapper tables before touching Gio types.
    Gio::init();

    try {
        impl_->bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisManager: Cannot connect to session bus: "
                  << error.what() << std::endl;
        return false;
    }

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

MprisPlayerInfo MprisManager::get_active_player() const {
    return impl_->get_active_info();
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
    impl_->call_active("PlayPause");
}

void MprisManager::play() {
    impl_->call_active("Play");
}

void MprisManager::pause() {
    impl_->call_active("Pause");
}

void MprisManager::stop() {
    impl_->call_active("Stop");
}

void MprisManager::next() {
    impl_->call_active("Next");
}

void MprisManager::previous() {
    impl_->call_active("Previous");
}

void MprisManager::seek(double position_seconds) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry || !entry->player) return;

    const double clamped = std::max(0.0, position_seconds);

    if (!entry->info.track_id.empty() && entry->info.track_id.front() == '/') {
        try {
            entry->player->call_sync(
                "SetPosition",
                Glib::VariantContainerBase::create_tuple({
                    Glib::Variant<Glib::DBusObjectPathString>::create(
                        Glib::DBusObjectPathString(entry->info.track_id)),
                    Glib::Variant<gint64>::create(static_cast<gint64>(
                        clamped * 1'000'000.0)),
                }));
            entry->info.position = clamped;
            return;
        } catch (const Glib::Error& error) {
            std::cerr << "MprisManager: SetPosition failed: " << error.what()
                      << std::endl;
        }
    }

    seek_by(clamped - entry->info.position);
}

void MprisManager::seek_by(double offset_seconds) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry || !entry->player) return;

    try {
        entry->player->call_sync(
            "Seek",
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<gint64>::create(
                    static_cast<gint64>(offset_seconds * 1'000'000.0)),
            }));
    } catch (const Glib::Error& error) {
        std::cerr << "MprisManager: Seek failed: " << error.what() << std::endl;
    }
}

void MprisManager::set_volume(double volume) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    const double clamped = std::clamp(volume, 0.0, 1.0);
    impl_->set_property(
        *entry, "Volume",
        Glib::Variant<Glib::Variant<double>>::create(
            Glib::Variant<double>::create(clamped)));
    entry->info.volume = clamped;
}

void MprisManager::set_loop_state(int state) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    const int clamped = std::clamp(state, 0, 2);
    impl_->set_property(
        *entry, "LoopStatus",
        Glib::Variant<Glib::Variant<Glib::ustring>>::create(
            Glib::Variant<Glib::ustring>::create(
                loop_state_to_string(clamped))));
    entry->info.loop_state = clamped;
}

void MprisManager::set_shuffle(bool shuffle) {
    Impl::PlayerEntry* entry = impl_->active_entry();
    if (!entry) return;

    impl_->set_property(
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
