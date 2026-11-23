/**
 * Shared layout metrics for compact track lists.
 *
 * Keep track rows and their column headers in lockstep: both consume these
 * constants instead of repeating magic numbers.
 */

#pragma once

namespace lollypop {
namespace track_list_metrics {

constexpr int kRowHeight = 20;
constexpr int kArtworkSize = 18;
constexpr int kArtworkCornerRadius = 3;
constexpr int kEqualizerSize = 12;

constexpr int kColumnGap = 12;
constexpr int kSideMargin = 12;
constexpr int kDurationWidth = 40;

constexpr int kLikeButtonSize = 20;
constexpr int kLikeIconSize = 16;
constexpr int kLikeMargin = 4;
constexpr int kLikeColumnWidth = kLikeButtonSize + 2 * kLikeMargin;

constexpr int kStickyHeaderOffset = 64;   // clears the floating glass toolbar
constexpr int kStickyHeaderHeight = 44;   // matches .songs-header min-height

}  // namespace track_list_metrics
}  // namespace lollypop
