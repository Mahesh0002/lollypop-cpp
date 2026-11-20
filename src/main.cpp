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

int main(int argc, char** argv) {
    // Initialize GTK
    auto app = lollypop::Application::create("com.lollypop.cpp");

    // Run the application
    return app->run(argc, argv);
}