/**
 * Lollypop C++ - EmptyState
 *
 * Deliberate empty state component (plan Section 18).
 * Never leaves a blank panel when data is unavailable.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class EmptyState {
public:
    EmptyState();
    ~EmptyState();

    EmptyState(const EmptyState&) = delete;
    EmptyState& operator=(const EmptyState&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    /// Configure the empty state: icon name, title, description.
    void setup(const std::string& icon_name,
               const std::string& title,
               const std::string& description);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
};

} // namespace lollypop
