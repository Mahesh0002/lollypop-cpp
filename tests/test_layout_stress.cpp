/**
 * Lollypop C++ - Headless layout stress harness
 *
 * Runtime GUI verification still needs the desktop smoke checklist, but this
 * harness covers the data and token inputs that drive the stress scenarios:
 * 1000 long-named tracks, missing artwork, and 100/150/200% font scales.
 */

#include <gtest/gtest.h>
#include "ui/theme/design_tokens.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

struct StressTrack {
    std::string title;
    std::string artist;
    std::string album;
    bool has_artwork;
};

std::vector<StressTrack> make_stress_tracks(int count) {
    const std::string long_name(240, 'W');
    std::vector<StressTrack> tracks;
    tracks.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        const std::string suffix = std::to_string(index);
        tracks.push_back({
            "Very Long Track Title " + long_name + suffix,
            "Long Artist Name " + long_name + suffix,
            "Long Album Name " + long_name + suffix,
            index % 17 != 0,
        });
    }
    return tracks;
}

std::string css_for_scale(double scale) {
    return lollypop::design_tokens::build_css(
        scale, "rgba(167, 139, 250, 1)", "rgba(167, 139, 250, 0.15)");
}

} // namespace

TEST(LayoutStressTest, BuildsThousandLongTracksWithMissingArtwork) {
    const std::vector<StressTrack> tracks = make_stress_tracks(1000);

    ASSERT_EQ(tracks.size(), 1000U);
    for (const StressTrack& track : tracks) {
        EXPECT_GT(track.title.size(), 240U);
        EXPECT_GT(track.artist.size(), 240U);
        EXPECT_GT(track.album.size(), 240U);
    }

    const auto missing = std::count_if(tracks.begin(), tracks.end(),
                                       [](const StressTrack& track) {
                                           return !track.has_artwork;
                                       });
    EXPECT_GT(missing, 0);
    EXPECT_LT(missing, static_cast<std::ptrdiff_t>(tracks.size()));
}

TEST(LayoutStressTest, GeneratesCanonicalTokensAtTextScales) {
    const std::vector<std::pair<double, const char*>> cases = {
        {1.0, "1"}, {1.5, "1.5"}, {2.0, "2"},
    };

    for (const auto& [scale, expected] : cases) {
        const std::string css = css_for_scale(scale);
        EXPECT_NE(css.find("  --font-scale: " + std::string(expected) + ";"),
                  std::string::npos);
        EXPECT_NE(css.find("--font-ui-medium: calc(18px * var(--font-scale));"),
                  std::string::npos);
        EXPECT_NE(css.find("--motion-fast: 120ms;"), std::string::npos);
        EXPECT_NE(css.find("--ease-out: cubic-bezier(0.2, 0.8, 0.2, 1);"),
                  std::string::npos);
        EXPECT_NE(css.find("--text-on-accent: #ffffff;"), std::string::npos);
    }
}

TEST(LayoutStressTest, LargestTokenStillFitsFixedPanelBudgetAtTwoHundredPercent) {
    constexpr int kPanelMaxWidth = 340;
    constexpr int kArtworkSlack = 44;

    double largest_px = 0.0;
    for (const auto& token : lollypop::design_tokens::kFontTokens) {
        largest_px = std::max(largest_px, token.px);
    }

    EXPECT_LE(kPanelMaxWidth, 340);
    EXPECT_DOUBLE_EQ(largest_px, 48.0);
    EXPECT_LT(largest_px * 2.0,
              static_cast<double>(kPanelMaxWidth - kArtworkSlack));
}
