/**
 * Lollypop C++ - StackNavigator Implementation
 * Safe navigation wrapper for GtkStack with history management and transitions.
 */

#include "ui/components/StackNavigator.hpp"
#include "util/safe_gtk.hpp"

#include <gtk/gtk.h>
#include <iostream>

namespace lollypop {

StackNavigator::StackNavigator(GtkStack* stack)
    : stack_(stack) {
    if (!stack_) {
        std::cerr << "[StackNavigator] Warning: null stack provided" << std::endl;
    }
}

void StackNavigator::push(const std::string& page_name, GtkWidget* widget) {
    if (!stack_ || !widget || page_name.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Add to stack if not already present
    GtkWidget* existing = gtk_stack_get_child_by_name(GTK_STACK(stack_), page_name.c_str());
    if (!existing) {
        safe_stack_add_named(stack_, widget, page_name.c_str());
    }

    // Apply transition
    apply_transition(transition_);

    // Navigate
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_name.c_str());

    // Update history
    if (current_page_name_ != page_name) {
        if (!current_page_name_.empty()) {
            history_.push_back(current_page_name_);
        }
        current_page_name_ = page_name;
    }
}

void StackNavigator::pop() {
    if (!stack_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (history_.empty()) return;

    // Apply reverse transition
    Transition reverse_transition = (transition_ == Transition::SlideLeft)
        ? Transition::SlideRight
        : (transition_ == Transition::SlideRight)
            ? Transition::SlideLeft
            : transition_;
    apply_transition(reverse_transition);

    // Get previous page
    std::string prev_page = history_.back();
    history_.pop_back();

    gtk_stack_set_visible_child_name(GTK_STACK(stack_), prev_page.c_str());
    current_page_name_ = prev_page;
}

void StackNavigator::replace(const std::string& page_name, GtkWidget* widget) {
    if (!stack_ || !widget || page_name.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove old page if different
    if (!current_page_name_.empty() && current_page_name_ != page_name) {
        GtkWidget* old_widget = gtk_stack_get_child_by_name(GTK_STACK(stack_), current_page_name_.c_str());
        if (old_widget) {
            safe_stack_remove(stack_, old_widget);
        }
    }

    // Add new page
    safe_stack_add_named(stack_, widget, page_name.c_str());

    // Use crossfade for replace
    apply_transition(Transition::CrossFade);

    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_name.c_str());
    current_page_name_ = page_name;
}

std::string StackNavigator::current_page() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_page_name_;
}

bool StackNavigator::can_go_back() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !history_.empty();
}

void StackNavigator::set_transition(Transition t) {
    std::lock_guard<std::mutex> lock(mutex_);
    transition_ = t;
}

void StackNavigator::register_page_factory(const std::string& page_name, PageFactory factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    page_factories_[page_name] = std::move(factory);
}

void StackNavigator::navigate_to(const std::string& page_name) {
    if (!stack_ || page_name.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if already current
    if (current_page_name_ == page_name) return;

    // Get or create page
    GtkWidget* widget = get_or_create_page(page_name);
    if (!widget) {
        std::cerr << "[StackNavigator] No widget for page: " << page_name << std::endl;
        return;
    }

    // Add to stack if not already present
    GtkWidget* existing = gtk_stack_get_child_by_name(GTK_STACK(stack_), page_name.c_str());
    if (!existing) {
        safe_stack_add_named(stack_, widget, page_name.c_str());
    }

    // Apply transition
    apply_transition(transition_);

    // Navigate
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), page_name.c_str());

    // Update history
    if (!current_page_name_.empty()) {
        history_.push_back(current_page_name_);
    }
    current_page_name_ = page_name;
}

void StackNavigator::apply_transition(Transition t) {
    if (!stack_) return;

    GtkStackTransitionType gtk_transition;
    switch (t) {
        case Transition::SlideLeft:
            gtk_transition = GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT;
            break;
        case Transition::SlideRight:
            gtk_transition = GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT;
            break;
        case Transition::CrossFade:
            gtk_transition = GTK_STACK_TRANSITION_TYPE_CROSSFADE;
            break;
        case Transition::None:
        default:
            gtk_transition = GTK_STACK_TRANSITION_TYPE_NONE;
            break;
    }
    gtk_stack_set_transition_type(GTK_STACK(stack_), gtk_transition);
}

GtkWidget* StackNavigator::get_or_create_page(const std::string& page_name) {
    // Check if already in stack
    GtkWidget* existing = gtk_stack_get_child_by_name(GTK_STACK(stack_), page_name.c_str());
    if (existing) return existing;

    // Check factory
    auto it = page_factories_.find(page_name);
    if (it != page_factories_.end()) {
        return it->second();
    }

    return nullptr;
}

} // namespace lollypop