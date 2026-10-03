#include "i18n.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cwchar>

namespace ekran::i18n {

namespace {

struct Entry {
    const wchar_t* tr;
    const wchar_t* en;
};

const Entry kTable[] = {
#define EKRAN_ENTRY(name, tr, en) {tr, en},
    EKRAN_STRINGS(EKRAN_ENTRY)
#undef EKRAN_ENTRY
};
static_assert(sizeof kTable / sizeof kTable[0] == kStrCount);

std::atomic<Lang> g_lang{Lang::En};

}  // namespace

Lang systemLanguage() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? Lang::Tr : Lang::En; }

void setLanguage(int setting) { g_lang = setting == 1 ? Lang::Tr : setting == 2 ? Lang::En : systemLanguage(); }

Lang language() { return g_lang; }

const wchar_t* W(Str id) {
    const Entry& e = kTable[id];
    return g_lang == Lang::Tr ? e.tr : e.en;
}

std::wstring F(Str id, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, id);
    vswprintf(buf, 1024, W(id), ap);
    va_end(ap);
    return buf;
}

}  // namespace ekran::i18n
