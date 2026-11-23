/**
 * Lollypop C++ - Internationalization Support
 * Basic gettext wrapper for translations.
 */

#pragma once

#include <libintl.h>
#include <locale.h>

namespace lollypop {

// Initialize gettext for the application
inline void init_i18n() {
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, "/usr/share/locale");
    textdomain(GETTEXT_PACKAGE);
}

// Translation function - wraps gettext
inline const char* tr(const char* msgid) {
    return gettext(msgid);
}

// Translation function for context
inline const char* tr_context(const char* context, const char* msgid) {
    (void)context;
    return dgettext(GETTEXT_PACKAGE, msgid);
}

// Plural form translation
inline const char* tr_plural(const char* msgid, const char* msgid_plural, unsigned long n) {
    return ngettext(msgid, msgid_plural, n);
}

} // namespace lollypop

// Convenience macros for use in source files
#define _(String) ::lollypop::tr(String)
#define N_(String) (String)  // Mark for extraction but don't translate
#define C_(Context, String) ::lollypop::tr_context(Context, String)
#define P_(Singular, Plural, N) ::lollypop::tr_plural(Singular, Plural, N)