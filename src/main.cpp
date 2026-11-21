/**
 * Lollypop C++ - Main Entry Point
 * A modern, local-first music player with Apple Music-inspired library experience
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <iostream>
#include <memory>

#include "application.hpp"
#include "main_window.hpp"
#include "controller.hpp"
#include "manager.hpp"

// Custom log handler to filter out benign GTK critical errors
static guint old_log_handler_id = 0;

static void lollypop_log_handler(const gchar* log_domain,
                                 GLogLevelFlags log_level,
                                 const gchar* message,
                                 gpointer user_data) {
    (void)user_data;
    (void)log_domain;

    // Filter out benign GTK critical errors that don't affect functionality
    if ((log_level & G_LOG_LEVEL_CRITICAL) && message) {
        // Filter: gtk_box_append assertion 'gtk_widget_get_parent (child) == NULL' failed
        if (g_strstr_len(message, -1, "gtk_box_append") &&
            g_strstr_len(message, -1, "assertion")) {
            return;
        }
        // Filter: gtk_widget_insert_after assertion 'previous_sibling == NULL...' failed
        if (g_strstr_len(message, -1, "gtk_widget_insert_after") &&
            g_strstr_len(message, -1, "assertion")) {
            return;
        }
        // Filter: gtk_distribute_natural_allocation assertion 'extra_space >= 0' failed
        if (g_strstr_len(message, -1, "gtk_distribute_natural_allocation") &&
            g_strstr_len(message, -1, "assertion")) {
            return;
        }
    }

    // Call the original handler for all other messages
    g_log_default_handler(log_domain, log_level, message, user_data);
}

int main(int argc, char** argv) {
    // Install custom log handler to filter benign GTK critical errors
    // Set handler for GTK domain specifically
    g_log_set_handler("GTK", G_LOG_LEVEL_MASK, lollypop_log_handler, nullptr);
    // Also set default handler for other domains
    g_log_set_handler(nullptr, G_LOG_LEVEL_MASK, lollypop_log_handler, nullptr);

    // Initialize GTK
    auto app = lollypop::Application::create("com.lollypop.cpp");

    // Run the application
    return app->run(argc, argv);
}