/**
 * Lollypop C++ - Spring Animation Utilities
 * Apple-standard spring physics: damping 0.82, stiffness 180, mass 1.0
 * Uses GTK tick callbacks for smooth spring animations
 */

#pragma once

#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <algorithm>
#include <string>
#include <cstring>

namespace lollypop {

// Check if animations are enabled globally (respects gtk-enable-animations setting)
inline bool animations_enabled() {
    GtkSettings* settings = gtk_settings_get_default();
    gboolean enable_animations = TRUE;
    g_object_get(settings, "gtk-enable-animations", &enable_animations, nullptr);
    return enable_animations;
}

// Apple-standard spring parameters
struct SpringParams {
    double damping = 0.82;    // Apple standard
    double stiffness = 180.0; // Apple standard
    double mass = 1.0;
};

// Spring animation state for manual tick-based animation
struct SpringState {
    double position = 0.0;
    double velocity = 0.0;
    double target = 0.0;
    SpringParams params;

    SpringState() = default;
    SpringState(double initial_pos, double target_pos, const SpringParams& p)
        : position(initial_pos), target(target_pos), params(p) {}

    // Semi-implicit Euler integration (stable for spring physics)
    void step(double dt) {
        double force = -params.stiffness * (position - target) - params.damping * velocity;
        double acceleration = force / params.mass;
        velocity += acceleration * dt;
        position += velocity * dt;
    }

    bool is_settled(double epsilon = 0.001) const {
        return std::abs(position - target) < epsilon && std::abs(velocity) < 0.01;
    }
};

/**
 * Spring Animation utilities using GTK tick callbacks
 * Provides smooth spring animations for widget properties
 */
class SpringAnimation {
public:
    static constexpr double DEFAULT_DT = 1.0 / 60.0; // 60 FPS fixed timestep

    // Animate a widget property to a target value using spring physics
    // Uses tick callback for smooth animation
    static void animate_to(
        GtkWidget* widget,
        const char* property,
        double target,
        const SpringParams& params = SpringParams{0.82, 180.0, 1.0},
        std::function<void()> on_complete = nullptr
    ) {
        // If animations are disabled, set property directly and call completion
        if (!animations_enabled()) {
            // Clamp target to valid range for common properties
            if (strcmp(property, "opacity") == 0) {
                target = std::clamp(target, 0.0, 1.0);
            } else if (strcmp(property, "margin-top") == 0 ||
                       strcmp(property, "margin-bottom") == 0) {
                target = std::max(target, 0.0);
            }

            // Set property using typed GValue based on the property's value type
            GObject* obj = G_OBJECT(widget);
            GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), property);
            if (pspec) {
                GValue value = G_VALUE_INIT;
                g_value_init(&value, pspec->value_type);
                if (G_VALUE_HOLDS_DOUBLE(&value)) {
                    g_value_set_double(&value, target);
                } else if (G_VALUE_HOLDS_FLOAT(&value)) {
                    g_value_set_float(&value, static_cast<float>(target));
                } else if (G_VALUE_HOLDS_INT(&value)) {
                    g_value_set_int(&value, static_cast<int>(target));
                } else {
                    // Unsupported type - skip setting
                    g_value_unset(&value);
                    if (on_complete) on_complete();
                    return;
                }
                g_object_set_property(obj, property, &value);
                g_value_unset(&value);
            }

            if (on_complete) on_complete();
            return;
        }

        // Get current value
        double from_value = 0.0;
        GObject* obj = G_OBJECT(widget);
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), property);
        if (pspec) {
            GValue value = G_VALUE_INIT;
            g_value_init(&value, pspec->value_type);
            g_object_get_property(obj, property, &value);
            if (G_VALUE_HOLDS_DOUBLE(&value)) {
                from_value = g_value_get_double(&value);
            } else if (G_VALUE_HOLDS_FLOAT(&value)) {
                from_value = g_value_get_float(&value);
            } else if (G_VALUE_HOLDS_INT(&value)) {
                from_value = static_cast<double>(g_value_get_int(&value));
            }
            g_value_unset(&value);
        }

        // Clamp target to valid range for common properties
        if (strcmp(property, "opacity") == 0) {
            target = std::clamp(target, 0.0, 1.0);
        } else if (strcmp(property, "margin-top") == 0 ||
                   strcmp(property, "margin-bottom") == 0) {
            target = std::max(target, 0.0);
        }

        // Create spring state
        struct AnimContext {
            SpringState state;
            GtkWidget* widget;
            std::string property;
            std::function<void()> on_complete;
        };

        auto* ctx = new AnimContext{
            SpringState(from_value, target, params),
            widget,
            property,
            std::move(on_complete)
        };

        // Use a static C function as callback to avoid lambda capture issues
        gtk_widget_add_tick_callback(
            widget,
            spring_tick_callback,
            ctx,
            [](gpointer data) { delete static_cast<AnimContext*>(data); }
        );
    }

    // Manual tick-based spring (for custom rendering or complex animations)
    static void add_spring_tick(
        GtkWidget* widget,
        SpringState state,
        std::function<void()> on_tick,
        std::function<void()> on_complete = nullptr
    ) {
        struct SpringTickContext {
            SpringState state;
            std::function<void()> on_tick;
            std::function<void()> on_complete;
        };

        auto* ctx = new SpringTickContext{std::move(state), std::move(on_tick), std::move(on_complete)};

        gtk_widget_add_tick_callback(
            widget,
            [](GtkWidget*, GdkFrameClock*, gpointer data) -> gboolean {
                auto* ctx = static_cast<SpringTickContext*>(data);
                double dt = 1.0 / 60.0; // Fixed 60 FPS timestep

                ctx->state.step(dt);
                ctx->on_tick();

                if (ctx->state.is_settled()) {
                    // Extract and call completion callback. Return G_SOURCE_REMOVE so
                    // the destroy notify deletes ctx (do NOT delete here).
                    std::function<void()> on_complete = std::move(ctx->on_complete);
                    if (on_complete) on_complete();
                    return G_SOURCE_REMOVE;
                }
                return G_SOURCE_CONTINUE;
            },
            ctx,
            [](gpointer data) { delete static_cast<SpringTickContext*>(data); }
        );
    }

private:
    struct AnimContext {
        SpringState state;
        GtkWidget* widget;
        std::string property;
        std::function<void()> on_complete;
    };

    static gboolean spring_tick_callback(GtkWidget*, GdkFrameClock*, gpointer data) {
        auto* ctx = static_cast<AnimContext*>(data);
        double dt = 1.0 / 60.0; // Fixed 60 FPS timestep

        ctx->state.step(dt);

        // Clamp position to valid range for common properties
        double pos = ctx->state.position;
        if (ctx->property == "opacity") {
            pos = std::clamp(pos, 0.0, 1.0);
        } else if (ctx->property == "margin-top" ||
                   ctx->property == "margin-bottom") {
            pos = std::max(pos, 0.0);
        }

        // Set the property value using typed GValue based on the property's value type
        GObject* obj = G_OBJECT(ctx->widget);
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(obj),
                                                         ctx->property.c_str());
        if (pspec) {
            GValue value = G_VALUE_INIT;
            g_value_init(&value, pspec->value_type);
            if (G_VALUE_HOLDS_DOUBLE(&value)) {
                g_value_set_double(&value, pos);
            } else if (G_VALUE_HOLDS_FLOAT(&value)) {
                g_value_set_float(&value, static_cast<float>(pos));
            } else if (G_VALUE_HOLDS_INT(&value)) {
                g_value_set_int(&value, static_cast<int>(pos));
            } else {
                // Unsupported type - skip setting
                g_value_unset(&value);
                return G_SOURCE_REMOVE;
            }
            g_object_set_property(obj, ctx->property.c_str(), &value);
            g_value_unset(&value);
        }

        if (ctx->state.is_settled()) {
            // Extract and call completion callback. Return G_SOURCE_REMOVE so
            // the destroy notify deletes ctx (do NOT delete here - that would
            // double-free when the destroy notify also runs).
            std::function<void()> on_complete = std::move(ctx->on_complete);
            if (on_complete) on_complete();
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
};

/**
 * Transition coordinator for shared-element transitions
 * (e.g., album card → Now Playing hero animation)
 */
class TransitionCoordinator {
public:
    struct SharedElement {
        GtkWidget* from_widget = nullptr;
        GtkWidget* to_widget = nullptr;
        std::string transition_name;
    };

    void add_shared_element(GtkWidget* from, GtkWidget* to, const std::string& name) {
        elements_.push_back({from, to, name});
    }

    // Start the shared element transition
    void start(double duration = 300.0) {
        (void)duration;
        // For now, implement as crossfade with scale
        // In GTK 4.16+, we'd use GtkTransitionController
        for (auto& elem : elements_) {
            if (elem.from_widget && elem.to_widget) {
                // Crossfade approach
                gtk_widget_set_opacity(elem.from_widget, 1.0);
                gtk_widget_set_opacity(elem.to_widget, 0.0);

                // Use SpringAnimation for crossfade
                SpringAnimation::animate_to(elem.from_widget, "opacity", 0.0);
                SpringAnimation::animate_to(elem.to_widget, "opacity", 1.0);
            }
        }
    }

    void clear() { elements_.clear(); }

private:
    std::vector<SharedElement> elements_;
};

} // namespace lollypop