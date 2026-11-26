/**
 * Lollypop C++ - Fullscreen Close Button
 *
 * Standalone close control for the immersive player. It owns only its button
 * and click callback; placement belongs to the FullscreenPlayer shell.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <utility>

namespace lollypop {

class FullscreenCloseButton {
public:
    using ClickCallback = std::function<void()>;

    FullscreenCloseButton();
    ~FullscreenCloseButton();

    FullscreenCloseButton(const FullscreenCloseButton&) = delete;
    FullscreenCloseButton& operator=(const FullscreenCloseButton&) = delete;

    GtkWidget* get_widget() const { return button_; }

    void set_clicked_callback(ClickCallback callback) {
        callback_ = std::move(callback);
    }

private:
    GtkWidget* button_ = nullptr;
    ClickCallback callback_;

    static void on_clicked(GtkButton* button, gpointer user_data);
};

} // namespace lollypop
