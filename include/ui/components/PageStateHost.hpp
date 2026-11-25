/**
 * Lollypop C++ - PageStateHost
 *
 * Shared wrapper for library pages. It owns the page content plus the
 * standard empty and unavailable states, so every browse page swaps states
 * the same way instead of reimplementing visibility bookkeeping.
 */

#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

#include "ui/components/EmptyState.hpp"
#include "ui/components/ErrorState.hpp"

namespace lollypop {

class PageStateHost {
public:
    PageStateHost(const std::string& empty_icon,
                  const std::string& empty_title,
                  const std::string& empty_description,
                  const std::string& error_title,
                  const std::string& error_description,
                  std::function<void()> retry_callback = nullptr) {
        root_.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        g_object_ref_sink(root_.get());
        gtk_widget_add_css_class(root_.get(), "page-state-host");
        gtk_widget_set_hexpand(root_.get(), TRUE);
        gtk_widget_set_vexpand(root_.get(), TRUE);
        gtk_widget_set_visible(root_.get(), TRUE);

        empty_ = std::make_unique<EmptyState>();
        empty_->setup(empty_icon, empty_title, empty_description);
        gtk_widget_set_visible(empty_->get_widget(), FALSE);
        gtk_box_append(GTK_BOX(root_.get()), empty_->get_widget());

        error_ = std::make_unique<ErrorState>();
        error_->setup(error_title, error_description, std::move(retry_callback));
        gtk_widget_set_visible(error_->get_widget(), FALSE);
        gtk_box_append(GTK_BOX(root_.get()), error_->get_widget());
    }

    PageStateHost(const PageStateHost&) = delete;
    PageStateHost& operator=(const PageStateHost&) = delete;

    GtkWidget* get_widget() const { return root_.get(); }

    /// Attach the normal page content. Call once during setup.
    void set_content(GtkWidget* content) {
        if (!content || content_ == content) return;
        content_ = content;
        gtk_box_prepend(GTK_BOX(root_.get()), content);
        gtk_widget_set_visible(content, TRUE);
    }

    void show_content() {
        set_visible(content_, TRUE);
        set_visible(empty_->get_widget(), FALSE);
        set_visible(error_->get_widget(), FALSE);
    }

    void show_empty() {
        set_visible(content_, FALSE);
        set_visible(empty_->get_widget(), TRUE);
        set_visible(error_->get_widget(), FALSE);
    }

    void show_error() {
        set_visible(content_, FALSE);
        set_visible(empty_->get_widget(), FALSE);
        set_visible(error_->get_widget(), TRUE);
    }

private:
    struct WidgetDeleter {
        void operator()(GtkWidget* w) const {
            if (w) g_object_unref(w);
        }
    };

    static void set_visible(GtkWidget* widget, gboolean visible) {
        if (widget) gtk_widget_set_visible(widget, visible);
    }

    std::unique_ptr<GtkWidget, WidgetDeleter> root_;
    std::unique_ptr<EmptyState> empty_;
    std::unique_ptr<ErrorState> error_;
    GtkWidget* content_ = nullptr;
};

} // namespace lollypop
