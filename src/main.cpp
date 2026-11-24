/**
 * Lollypop C++ - Main Entry Point
 * A modern, local-first music player with Apple Music-inspired library experience
 */

#include <gtk/gtk.h>
#include <iostream>
#include <memory>

#include "application.hpp"
#include "main_window.hpp"
#include "controller.hpp"
#include "manager.hpp"
#include "config/i18n.hpp"

// GTK, Pango and fontconfig keep process-lifetime caches (font maps, font
// config substitutes, Pango layout engines). LeakSanitizer reports them as
// leaks on exit even though they are intentional singletons. Suppress only the
// third-party UI stack so genuine application leaks are still reported.
#if defined(__SANITIZE_ADDRESS__)
#define LOLLYPOP_ASAN_ENABLED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define LOLLYPOP_ASAN_ENABLED 1
#endif
#endif

#if defined(LOLLYPOP_ASAN_ENABLED)
extern "C" const char* __lsan_default_suppressions(void);
extern "C" const char* __lsan_default_suppressions(void) {
    return "leak:libfontconfig\n"
           "leak:libpango-1.0\n"
           "leak:libpangoft2-1.0\n"
           "leak:libpangocairo-1.0\n"
           "leak:libgdk_pixbuf-2.0\n"
           "leak:libgtk-4\n"
           "leak:libgdk-4\n"
           "leak:libadwaita-1\n";
}
#endif

// Custom log handler to filter out benign GTK critical errors (only when explicitly enabled)
static guint old_log_handler_id = 0;

static void lollypop_log_handler(const gchar* log_domain,
                                 GLogLevelFlags log_level,
                                 const gchar* message,
                                 gpointer user_data) {
    (void)user_data;
    (void)log_domain;

    // Only filter if explicitly enabled via environment variable
    if (g_getenv("LOLLYPOP_HIDE_GTK_CRITICALS")) {
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
    }

    // Call the original handler for all other messages
    g_log_default_handler(log_domain, log_level, message, user_data);
}

int main(int argc, char** argv) {
    // Initialize internationalization
    lollypop::init_i18n();

    // Check for GTK critical filter environment variable
    if (g_getenv("LOLLYPOP_HIDE_GTK_CRITICALS")) {
        g_warning("GTK critical warnings are suppressed via LOLLYPOP_HIDE_GTK_CRITICALS. "
                  "This hides real bugs - only use for debugging!");
    }

    // Install custom log handler (only filters when LOLLYPOP_HIDE_GTK_CRITICALS is set)
    g_log_set_handler("GTK", G_LOG_LEVEL_MASK, lollypop_log_handler, nullptr);
    g_log_set_handler(nullptr, G_LOG_LEVEL_MASK, lollypop_log_handler, nullptr);

    // Initialize GTK
    auto app = lollypop::Application::create("com.lollypop.cpp");

    // Run the application
    return app->run(argc, argv);
}