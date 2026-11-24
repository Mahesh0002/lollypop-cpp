/**
 * Lollypop C++ - ErrorState
 *
 * Error state component (plan Section 18).
 * Shown when playback, library, or artwork operations fail.
 */

#pragma once

#include <functional>
#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class ErrorState {
public:
    ErrorState();
    ~ErrorState();

    ErrorState(const ErrorState&) = delete;
    ErrorState& operator=(const ErrorState&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    /// Configure the error state: title, description, optional retry callback.
    void setup(const std::string& title,
               const std::string& description,
               std::function<void()> retry_callback = nullptr);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (!w) return;
            if (gtk_widget_get_parent(w) != nullptr) {
                // GTK parents take ownership of the child's existing reference.
                gtk_widget_unparent(w);
            } else {
                g_object_unref(w);
            }
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
    std::unique_ptr<GtkWidget, WidgetDeleter> retry_button_;
};

} // namespace lollypop
