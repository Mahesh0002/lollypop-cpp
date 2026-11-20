/**
 * Lollypop C++ - MPRIS Server Implementation
 * Exports Lollypop as org.mpris.MediaPlayer2.lollypop on the session bus.
 */

#include "mpris_server.hpp"

#include <giomm.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

namespace lollypop {
namespace {

constexpr const char* kBusName = "org.mpris.MediaPlayer2.lollypop";
constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";

constexpr const char* kIntrospectionXml = R"(
<node>
  <interface name="org.mpris.MediaPlayer2">
    <property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/>
    <property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <property name="PlaybackStatus" type="s" access="read"/>
    <property name="LoopStatus" type="s" access="read"/>
    <property name="Shuffle" type="b" access="readwrite"/>
    <property name="Metadata" type="a{sv}" access="read"/>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Position" type="x" access="read"/>
    <property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/>
    <property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/>
    <property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/>
    <property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
    <method name="Next"/>
    <method name="Previous"/>
    <method name="Pause"/>
    <method name="PlayPause"/>
    <method name="Stop"/>
    <method name="Play"/>
    <method name="Seek">
      <arg type="x" direction="in"/>
    </method>
    <method name="SetPosition">
      <arg type="o" direction="in"/>
      <arg type="x" direction="in"/>
    </method>
    <method name="OpenUri">
      <arg type="s" direction="in"/>
    </method>
    <method name="SetVolume">
      <arg type="d" direction="in"/>
    </method>
    <signal name="Seeked">
      <arg type="x"/>
    </signal>
  </interface>
</node>
)";

std::string loop_state_to_string(int state) {
    if (state == 1) return "Playlist";
    if (state == 2) return "Track";
    return "None";
}

int loop_state_from_string(const std::string& status) {
    if (status == "Playlist") return 1;
    if (status == "Track") return 2;
    return 0;
}

// Build MPRIS Metadata dict (a{sv})
Glib::VariantContainerBase build_metadata(const Track& track) {
    std::vector<Glib::VariantBase> entries;

    if (!track.title.empty()) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("xesam:title"),
            Glib::Variant<Glib::ustring>::create(track.title),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }
    if (!track.artist.empty()) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("xesam:artist"),
            Glib::Variant<std::vector<Glib::ustring>>::create({track.artist}),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }
    if (!track.album.empty()) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("xesam:album"),
            Glib::Variant<Glib::ustring>::create(track.album),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }
    if (!track.file_path.empty()) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("mpris:trackid"),
            Glib::Variant<Glib::DBusObjectPathString>::create(
                Glib::DBusObjectPathString(track.file_path)),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }
    if (!track.artwork_path.empty()) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("mpris:artUrl"),
            Glib::Variant<Glib::ustring>::create(track.artwork_path),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }
    if (track.duration > 0) {
        std::vector<Glib::VariantBase> kv = {
            Glib::Variant<Glib::ustring>::create("mpris:length"),
            Glib::Variant<gint64>::create(
                static_cast<gint64>(track.duration * 1'000'000.0)),
        };
        entries.push_back(Glib::VariantContainerBase::create_tuple(kv));
    }

    return Glib::VariantContainerBase::create_tuple(
        std::vector<Glib::VariantBase>{
            Glib::Variant<std::vector<Glib::VariantBase>>::create(entries),
        });
}

}  // namespace

struct MprisServer::Impl {
    Glib::RefPtr<Gio::DBus::Connection> bus;
    guint name_owner_id = 0;
    guint player_reg_id = 0;
    guint root_reg_id = 0;

    Glib::RefPtr<Gio::DBus::NodeInfo> node_info;
    Glib::RefPtr<Gio::DBus::InterfaceInfo> root_iface;
    Glib::RefPtr<Gio::DBus::InterfaceInfo> player_iface;

    // InterfaceVTable instances must outlive the D-Bus registration
    // (giomm requirement - they cannot be temporaries)
    Gio::DBus::InterfaceVTable root_vtable{
        sigc::mem_fun(*this, &Impl::on_method_call),
        sigc::mem_fun(*this, &Impl::on_get_property),
        sigc::mem_fun(*this, &Impl::on_set_property)};
    Gio::DBus::InterfaceVTable player_vtable{
        sigc::mem_fun(*this, &Impl::on_method_call),
        sigc::mem_fun(*this, &Impl::on_get_property),
        sigc::mem_fun(*this, &Impl::on_set_property)};

    // Current state
    std::string playback_status = "Stopped";
    std::string loop_status = "None";
    bool shuffle = false;
    double volume = 1.0;
    double position_sec = 0.0;
    double duration_sec = 0.0;
    Track current_track;

    bool can_control = true;
    bool can_seek = true;
    bool can_go_next = true;
    bool can_go_previous = true;
    bool can_play = true;
    bool can_pause = true;

    // Callbacks
    PlaybackStateChangedCallback playback_state_cb;
    TrackChangedCallback track_cb;
    SeekCallback seek_cb;
    SetPositionCallback set_position_cb;
    VolumeChangedCallback volume_cb;
    LoopStateChangedCallback loop_state_cb;
    ShuffleChangedCallback shuffle_cb;

    void emit_properties_changed(const char* interface_name,
                                 const std::vector<Glib::VariantBase>& changed_entries) {
        if (!bus) return;

        std::vector<Glib::VariantBase> changed_dict_entries = changed_entries;
        Glib::VariantContainerBase changed_dict =
            Glib::VariantContainerBase::create_tuple(
                std::vector<Glib::VariantBase>{
                    Glib::Variant<std::vector<Glib::VariantBase>>::create(changed_dict_entries),
                });

        Glib::VariantContainerBase invalidated =
            Glib::VariantContainerBase::create_tuple(std::vector<Glib::VariantBase>{});

        Glib::VariantContainerBase params = Glib::VariantContainerBase::create_tuple(
            std::vector<Glib::VariantBase>{
                Glib::Variant<Glib::ustring>::create(interface_name),
                changed_dict,
                Glib::Variant<std::vector<Glib::ustring>>::create({}),
            });

        try {
            bus->emit_signal(
                kObjectPath, "org.freedesktop.DBus.Properties",
                "PropertiesChanged", {}, params);
        } catch (const Glib::Error& e) {
            std::cerr << "MprisServer: Failed to emit PropertiesChanged: " << e.what() << std::endl;
        }
    }

    void emit_properties_changed_simple(const char* interface_name,
                                        const char* property_name,
                                        const Glib::VariantBase& value) {
        std::vector<Glib::VariantBase> entries = {
            Glib::VariantContainerBase::create_tuple(
                std::vector<Glib::VariantBase>{
                    Glib::Variant<Glib::ustring>::create(property_name),
                    value,
                }),
        };
        emit_properties_changed(interface_name, entries);
    }

    void emit_seeked(double position_sec) {
        if (!bus) return;

        Glib::VariantContainerBase params = Glib::VariantContainerBase::create_tuple(
            std::vector<Glib::VariantBase>{
                Glib::Variant<gint64>::create(
                    static_cast<gint64>(position_sec * 1'000'000.0)),
            });

        try {
            bus->emit_signal(
                kObjectPath, kPlayerInterface, "Seeked", {}, params);
        } catch (const Glib::Error& e) {
            std::cerr << "MprisServer: Failed to emit Seeked: " << e.what() << std::endl;
        }
    }

    void on_method_call(
        const Glib::RefPtr<Gio::DBus::Connection>& /*connection*/,
        const Glib::ustring& /*sender*/, const Glib::ustring& /*object_path*/,
        const Glib::ustring& interface_name, const Glib::ustring& method_name,
        const Glib::VariantContainerBase& parameters,
        const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation) {

        if (interface_name != kPlayerInterface) {
            invocation->return_value({});
            return;
        }

        try {
            if (method_name == "Next") {
                // Next handled by playback state callback (playing=true)
                if (playback_state_cb) playback_state_cb(true);
                invocation->return_value({});
            } else if (method_name == "Previous") {
                if (playback_state_cb) playback_state_cb(true);
                invocation->return_value({});
            } else if (method_name == "Pause") {
                if (playback_state_cb) playback_state_cb(false);
                invocation->return_value({});
            } else if (method_name == "Play") {
                if (playback_state_cb) playback_state_cb(true);
                invocation->return_value({});
            } else if (method_name == "PlayPause") {
                // Toggle - handled by caller
                invocation->return_value({});
            } else if (method_name == "Stop") {
                if (playback_state_cb) playback_state_cb(false);
                invocation->return_value({});
            } else if (method_name == "Seek") {
                gint64 us =
                    Glib::VariantBase::cast_dynamic<Glib::Variant<gint64>>(
                        parameters.get_child(0)).get();
                if (seek_cb) seek_cb(static_cast<double>(us) / 1'000'000.0);
                invocation->return_value({});
            } else if (method_name == "SetPosition") {
                Glib::DBusObjectPathString track_id =
                    Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::DBusObjectPathString>>(
                        parameters.get_child(0)).get();
                gint64 us =
                    Glib::VariantBase::cast_dynamic<Glib::Variant<gint64>>(
                        parameters.get_child(1)).get();
                if (set_position_cb)
                    set_position_cb(track_id.raw(),
                                    static_cast<double>(us) / 1'000'000.0);
                invocation->return_value({});
            } else if (method_name == "OpenUri") {
                invocation->return_value({});
            } else if (method_name == "SetVolume") {
                double vol =
                    Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(
                        parameters.get_child(0)).get();
                volume = std::clamp(vol, 0.0, 1.0);
                if (volume_cb) volume_cb(volume);
                emit_properties_changed_simple(
                    kPlayerInterface, "Volume",
                    Glib::Variant<double>::create(volume));
                invocation->return_value({});
            } else {
                invocation->return_value({});
            }
        } catch (...) {
            invocation->return_value({});
        }
    }

    void on_get_property(
        Glib::VariantBase& property,
        const Glib::RefPtr<Gio::DBus::Connection>& /*connection*/,
        const Glib::ustring& /*sender*/, const Glib::ustring& /*object_path*/,
        const Glib::ustring& interface_name, const Glib::ustring& property_name) {

        if (interface_name == kRootInterface) {
            if (property_name == "Identity") {
                property = Glib::Variant<Glib::ustring>::create("Lollypop C++");
            } else if (property_name == "DesktopEntry") {
                property = Glib::Variant<Glib::ustring>::create("lollypop-cpp");
            } else if (property_name == "SupportedUriSchemes") {
                property = Glib::Variant<std::vector<Glib::ustring>>::create(
                    {"file", "http", "https"});
            } else if (property_name == "SupportedMimeTypes") {
                property = Glib::Variant<std::vector<Glib::ustring>>::create(
                    {"audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4"});
            }
            // Unknown properties left unset
        } else if (interface_name == kPlayerInterface) {
            if (property_name == "PlaybackStatus") {
                property = Glib::Variant<Glib::ustring>::create(playback_status);
            } else if (property_name == "LoopStatus") {
                property = Glib::Variant<Glib::ustring>::create(loop_status);
            } else if (property_name == "Shuffle") {
                property = Glib::Variant<bool>::create(shuffle);
            } else if (property_name == "Metadata") {
                property = build_metadata(current_track);
            } else if (property_name == "Volume") {
                property = Glib::Variant<double>::create(volume);
            } else if (property_name == "Position") {
                property = Glib::Variant<gint64>::create(
                    static_cast<gint64>(position_sec * 1'000'000.0));
            } else if (property_name == "MinimumRate") {
                property = Glib::Variant<double>::create(1.0);
            } else if (property_name == "MaximumRate") {
                property = Glib::Variant<double>::create(1.0);
            } else if (property_name == "CanGoNext") {
                property = Glib::Variant<bool>::create(can_go_next);
            } else if (property_name == "CanGoPrevious") {
                property = Glib::Variant<bool>::create(can_go_previous);
            } else if (property_name == "CanPlay") {
                property = Glib::Variant<bool>::create(can_play);
            } else if (property_name == "CanPause") {
                property = Glib::Variant<bool>::create(can_pause);
            } else if (property_name == "CanSeek") {
                property = Glib::Variant<bool>::create(can_seek);
            } else if (property_name == "CanControl") {
                property = Glib::Variant<bool>::create(can_control);
            }
        }
    }

    bool on_set_property(
        const Glib::RefPtr<Gio::DBus::Connection>& /*connection*/,
        const Glib::ustring& /*sender*/, const Glib::ustring& /*object_path*/,
        const Glib::ustring& interface_name, const Glib::ustring& property_name,
        const Glib::VariantBase& value) {

        if (interface_name != kPlayerInterface) return false;

        try {
            if (property_name == "Shuffle") {
                shuffle = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(value).get();
                if (shuffle_cb) shuffle_cb(shuffle);
                emit_properties_changed_simple(
                    kPlayerInterface, "Shuffle",
                    Glib::Variant<bool>::create(shuffle));
            } else if (property_name == "LoopStatus") {
                Glib::ustring val_str =
                    Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
                int state = loop_state_from_string(val_str.raw());
                loop_status = val_str.raw();
                if (loop_state_cb) loop_state_cb(state);
                emit_properties_changed_simple(
                    kPlayerInterface, "LoopStatus",
                    Glib::Variant<Glib::ustring>::create(loop_status));
            } else if (property_name == "Volume") {
                volume = std::clamp(
                    Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(value).get(),
                    0.0, 1.0);
                if (volume_cb) volume_cb(volume);
                emit_properties_changed_simple(
                    kPlayerInterface, "Volume",
                    Glib::Variant<double>::create(volume));
            } else {
                return false;
            }
            return true;
        } catch (...) {
            return false;
        }
    }
};

MprisServer::MprisServer() : impl_(std::make_unique<Impl>()) {}

MprisServer::~MprisServer() {
    shutdown();
}

bool MprisServer::initialize() {
    if (impl_->bus) return true;

    try {
        impl_->bus = Gio::DBus::Connection::get_sync(Gio::DBus::BusType::SESSION);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisServer: Cannot connect to session bus: " << error.what() << std::endl;
        return false;
    }

    // Parse introspection XML
    try {
        impl_->node_info = Gio::DBus::NodeInfo::create_for_xml(kIntrospectionXml);
        impl_->root_iface = impl_->node_info->lookup_interface(kRootInterface);
        impl_->player_iface = impl_->node_info->lookup_interface(kPlayerInterface);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisServer: Failed to parse introspection XML: " << error.what() << std::endl;
        return false;
    }

    // Own the bus name
    try {
        impl_->name_owner_id = Gio::DBus::own_name(
            Gio::DBus::BusType::SESSION, kBusName,
            {},  // bus_acquired_slot
            {},  // name_acquired_slot
            {},  // name_lost_slot
            Gio::DBus::BusNameOwnerFlags::NONE);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisServer: Failed to own bus name " << kBusName << ": "
                  << error.what() << std::endl;
        return false;
    }

    // Register org.mpris.MediaPlayer2 interface
    try {
        impl_->root_reg_id = impl_->bus->register_object(
            kObjectPath, impl_->root_iface, impl_->root_vtable);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisServer: Failed to register root interface: "
                  << error.what() << std::endl;
        return false;
    }

    // Register org.mpris.MediaPlayer2.Player interface (same path)
    try {
        impl_->player_reg_id = impl_->bus->register_object(
            kObjectPath, impl_->player_iface, impl_->player_vtable);
    } catch (const Glib::Error& error) {
        std::cerr << "MprisServer: Failed to register player interface: "
                  << error.what() << std::endl;
        return false;
    }

    std::cout << "MprisServer: Exported as " << kBusName << std::endl;
    return true;
}

void MprisServer::shutdown() {
    if (impl_->player_reg_id && impl_->bus) {
        impl_->bus->unregister_object(impl_->player_reg_id);
        impl_->player_reg_id = 0;
    }
    if (impl_->root_reg_id && impl_->bus) {
        impl_->bus->unregister_object(impl_->root_reg_id);
        impl_->root_reg_id = 0;
    }
    if (impl_->name_owner_id) {
        Gio::DBus::unown_name(impl_->name_owner_id);
        impl_->name_owner_id = 0;
    }
    impl_->bus.reset();
}

bool MprisServer::is_initialized() const {
    return static_cast<bool>(impl_->bus);
}

void MprisServer::update_playback_status(bool playing) {
    impl_->playback_status = playing ? "Playing" : "Paused";
    impl_->emit_properties_changed_simple(
        kPlayerInterface, "PlaybackStatus",
        Glib::Variant<Glib::ustring>::create(impl_->playback_status));
}

void MprisServer::update_track(const Track& track) {
    impl_->current_track = track;

    // Emit Metadata changed
    std::vector<Glib::VariantBase> entries = {
        Glib::VariantContainerBase::create_tuple(
            std::vector<Glib::VariantBase>{
                Glib::Variant<Glib::ustring>::create("Metadata"),
                build_metadata(track),
            }),
    };
    impl_->emit_properties_changed(kPlayerInterface, entries);

    // Update duration
    impl_->duration_sec = track.duration;
}

void MprisServer::update_position(double position_seconds) {
    impl_->position_sec = std::max(0.0, position_seconds);
    impl_->emit_properties_changed_simple(
        kPlayerInterface, "Position",
        Glib::Variant<gint64>::create(
            static_cast<gint64>(impl_->position_sec * 1'000'000.0)));
    impl_->emit_seeked(impl_->position_sec);
}

void MprisServer::update_duration(double duration_seconds) {
    impl_->duration_sec = duration_seconds;
}

void MprisServer::update_volume(double volume) {
    impl_->volume = std::clamp(volume, 0.0, 1.0);
    impl_->emit_properties_changed_simple(
        kPlayerInterface, "Volume",
        Glib::Variant<double>::create(impl_->volume));
}

void MprisServer::update_loop_state(int loop_state) {
    impl_->loop_status = loop_state_to_string(loop_state);
    impl_->emit_properties_changed_simple(
        kPlayerInterface, "LoopStatus",
        Glib::Variant<Glib::ustring>::create(impl_->loop_status));
}

void MprisServer::update_shuffle(bool shuffle) {
    impl_->shuffle = shuffle;
    impl_->emit_properties_changed_simple(
        kPlayerInterface, "Shuffle",
        Glib::Variant<bool>::create(shuffle));
}

void MprisServer::set_playback_state_changed_callback(PlaybackStateChangedCallback cb) {
    impl_->playback_state_cb = std::move(cb);
}
void MprisServer::set_track_changed_callback(TrackChangedCallback cb) {
    impl_->track_cb = std::move(cb);
}
void MprisServer::set_seek_callback(SeekCallback cb) {
    impl_->seek_cb = std::move(cb);
}
void MprisServer::set_set_position_callback(SetPositionCallback cb) {
    impl_->set_position_cb = std::move(cb);
}
void MprisServer::set_volume_changed_callback(VolumeChangedCallback cb) {
    impl_->volume_cb = std::move(cb);
}
void MprisServer::set_loop_state_changed_callback(LoopStateChangedCallback cb) {
    impl_->loop_state_cb = std::move(cb);
}
void MprisServer::set_shuffle_changed_callback(ShuffleChangedCallback cb) {
    impl_->shuffle_cb = std::move(cb);
}

}  // namespace lollypop
