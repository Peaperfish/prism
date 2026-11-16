#pragma once

#include <clocale>
#include <string>

// Switches the C locale to one whose decimal point is a comma for as long as it lives, and
// restores the old one. active() says whether it really did: a machine may have no such locale
// (a CI runner often has only C and en_US), and a test that needs one skips on that, rather than
// quietly testing the C locale again.
class CommaLocale {
public:
    CommaLocale() {
        const char* const before = std::setlocale(LC_ALL, nullptr);
        saved_ = before != nullptr ? before : "C";
        // POSIX names, then Windows': the UCRT takes both "German_Germany" and "de-DE".
        for (const char* name : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR", "de_DE.utf8",
                                 "German_Germany", "de-DE", "fr-FR"}) {
            if (std::setlocale(LC_ALL, name) != nullptr) {
                const std::lconv* const conv = std::localeconv();
                if (conv != nullptr && conv->decimal_point != nullptr &&
                    std::string(conv->decimal_point) == ",") {
                    active_ = true;
                    return;
                }
            }
        }
        std::setlocale(LC_ALL, saved_.c_str());
    }
    ~CommaLocale() { std::setlocale(LC_ALL, saved_.c_str()); }
    CommaLocale(const CommaLocale&) = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;

    [[nodiscard]] bool active() const { return active_; }

private:
    std::string saved_;
    bool active_ = false;
};
