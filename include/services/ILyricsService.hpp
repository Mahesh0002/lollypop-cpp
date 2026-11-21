/**
 * Lollypop C++ - ILyricsService Interface
 *
 * Separate lyrics interface. Lyrics data may come from embedded lyrics,
 * local LRC files, cached lyrics, or an external provider. The UI must
 * display an appropriate unavailable state when no lyrics source exists.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "../types.hpp"

namespace lollypop {

class ILyricsService {
public:
    virtual ~ILyricsService() = default;

    /// Synchronous lookup (may block — prefer request_lyrics for UI use).
    virtual std::optional<Lyrics> get_lyrics(const std::string& track_id) = 0;

    /// Asynchronous request with callback delivered to GTK main context.
    using LyricsCallback = std::function<void(std::optional<Lyrics> result)>;
    virtual void request_lyrics(
        const std::string& track_id,
        LyricsCallback callback) = 0;

    /// Store lyrics (used by backend after fetching).
    virtual void set_lyrics(
        const std::string& track_id,
        const std::string& text,
        bool synced = false,
        const std::string& source = {}) = 0;
};

} // namespace lollypop
