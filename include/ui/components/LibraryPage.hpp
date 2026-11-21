/**
 * Lollypop C++ - LibraryPage Base Class
 *
 * Base class for all library pages (Home, Albums, Artists, etc.).
 * Used by MiddleLibrary's page stack.
 */

#pragma once

#include <gtk/gtk.h>
#include <memory>
#include <string>

namespace lollypop {

class LibraryPage {
public:
    virtual ~LibraryPage() = default;
    virtual GtkWidget* get_widget() const = 0;
    virtual void refresh() = 0;
    virtual std::string get_page_id() const = 0;
};

} // namespace lollypop
