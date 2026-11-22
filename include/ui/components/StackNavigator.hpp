/**
 * Lollypop C++ - StackNavigator
 * Safe navigation wrapper for GtkStack with history management and transitions.
 */

#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace lollypop {

class StackNavigator {
public:
    enum class Transition {
        SlideLeft,
        SlideRight,
        CrossFade,
        None
    };

    explicit StackNavigator(GtkStack* stack);
    ~StackNavigator() = default;

    StackNavigator(const StackNavigator&) = delete;
    StackNavigator& operator=(const StackNavigator&) = delete;

    // Push new page with transition
    void push(const std::string& page_name, GtkWidget* widget);

    // Pop current page with reverse transition
    void pop();

    // Replace current page (no animation by default)
    void replace(const std::string& page_name, GtkWidget* widget);

    // Get current page name
    std::string current_page() const;

    // Check if can go back
    bool can_go_back() const;

    // Set transition type for push/pop
    void set_transition(Transition t);

    // Register a page factory for lazy creation
    using PageFactory = std::function<GtkWidget*()>;
    void register_page_factory(const std::string& page_name, PageFactory factory);

    // Navigate to page (creates if factory registered)
    void navigate_to(const std::string& page_name);

private:
    GtkStack* stack_;
    std::vector<std::string> history_;
    Transition transition_ = Transition::SlideLeft;
    std::string current_page_name_;
    std::unordered_map<std::string, PageFactory> page_factories_;
    mutable std::mutex mutex_;

    void apply_transition(Transition t);
    GtkWidget* get_or_create_page(const std::string& page_name);
};

} // namespace lollypop