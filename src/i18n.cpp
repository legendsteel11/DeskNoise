// i18n.cpp - language selection and the string tables built from DESKNOISE_STRINGS.
#include "i18n.h"
#include <wchar.h>

static const WCHAR* const kKo[] = {
#define DESKNOISE_KO(id, ko, en) ko,
    DESKNOISE_STRINGS(DESKNOISE_KO)
#undef DESKNOISE_KO
};

static const WCHAR* const kEn[] = {
#define DESKNOISE_EN(id, ko, en) en,
    DESKNOISE_STRINGS(DESKNOISE_EN)
#undef DESKNOISE_EN
};

static_assert(ARRAYSIZE(kKo) == S_COUNT, "Korean table is out of sync with StrId");
static_assert(ARRAYSIZE(kEn) == S_COUNT, "English table is out of sync with StrId");

// English is the fallback for every language other than Korean.
static const WCHAR* const* g_str = kEn;

void I18nInit(LPCWSTR cmdLine)
{
    bool korean = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN);
    if (cmdLine) {
        if (wcsstr(cmdLine, L"/lang=ko")) korean = true;
        else if (wcsstr(cmdLine, L"/lang=en")) korean = false;
    }
    g_str = korean ? kKo : kEn;
}

const WCHAR* T(int id)
{
    if (id < 0 || id >= S_COUNT) return L"";
    return g_str[id];
}
