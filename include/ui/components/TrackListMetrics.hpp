/**
 * Shared layout metrics for compact track lists.
 *
 * Scaled 10% from the original Apple HIG → GTK mapping:
 *   - ~53px touch target per row,
 *   - ~10px columns, ~20px side margins,
 *   - HIG-inspired type scale (headline / subhead / footnote).
 *
 * The row count below is unchanged; taller rows simply show fewer at once.
 */

#pragma once

namespace lollypop {
namespace track_list_metrics {

constexpr int kVisibleRows = 30;
constexpr int kPageSize = kVisibleRows;

constexpr int kRowHeight = 53;
constexpr int kRowVerticalGap = 4;
constexpr int kArtworkSize = 44;  // centered with a ~4px optical inset
constexpr int kArtworkCornerRadius = 8;
constexpr int kEqualizerSize = 17;

constexpr int kColumnGap = 10;
constexpr int kSideMargin = 20;
constexpr int kDurationWidth = 58;

constexpr int kLikeButtonSize = 34;
constexpr int kLikeIconSize = 20;
constexpr int kLikeMargin = 4;
constexpr int kLikeColumnWidth = kLikeButtonSize + 2 * kLikeMargin;

constexpr int kTitleFontSize = 20;   // headline, semibold
constexpr int kArtistFontSize = 17;  // subhead, regular
constexpr int kAlbumFontSize = 15;   // footnote, regular
constexpr int kDurationFontSize = 15;
constexpr int kHeaderFontSize = 14;

constexpr int kStickyHeaderOffset = 0;    // no floating toolbar above the list
constexpr int kStickyHeaderHeight = 53;   // matches .middle-sticky-header
constexpr int kColumnHeaderHeight = 22;   // sort-label row, not a full track row

}  // namespace track_list_metrics
}  // namespace lollypop
