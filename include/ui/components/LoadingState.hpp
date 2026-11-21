/**
 * Lollypop C++ - LoadingState
 *
 * Loading state component (plan Section 18).
 * Shown while data is being fetched asynchronously.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class LoadingState {
public:
    LoadingState();
    ~LoadingState();

    LoadingState(const LoadingState&) = delete;
    LoadingState& operator=(const LoadingState&) = delete;

    GtkWidget* get_widget() const { return widget_.get(); }

    /// Configure the loading state with a message.
    void setup(const std::string& message);

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };
    std::unique_ptr<GtkWidget, WidgetDeleter> widget_;
};

} // namespace lollypop
