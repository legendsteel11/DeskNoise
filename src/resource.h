#pragma once
#define IDI_APPICON        101
#define IDD_PRESETNAME     200
#define IDC_PRESET_NAME    201
#define IDC_PRESET_NAME_LBL 202

#define IDD_ABOUT          210
#define IDC_AB_ICON        211
#define IDC_AB_TITLE       212
#define IDC_AB_VER_L       213
#define IDC_AB_VER         214
#define IDC_AB_AUTHOR_L    215
#define IDC_AB_AUTHOR      216
#define IDC_AB_DATE_L      217
#define IDC_AB_DATE        218
#define IDC_AB_GITHUB_L    219
#define IDC_AB_GITHUB      220
#define IDC_AB_TOOLS_L     221
#define IDC_AB_TOOLS       222
#define IDC_AB_LIC_L       223
#define IDC_AB_LIC         224
#define IDC_AB_NOTE_L      225
#define IDC_AB_NOTE        226
#define IDC_AB_COPY        227

// Single source of the app version. Used by app.rc (VERSIONINFO) and by
// main.cpp (title bar). Keep the numeric and string forms in sync.
#define APP_VER_MAJOR      1
#define APP_VER_MINOR      0
#define APP_VER_PATCH      0
#define APP_VERSION_STR    "1.0.0"
#define APP_VERSION_STRW  L"1.0.0"

// Filled in when a build is actually published; left empty until then.
#define APP_RELEASE_DATE_W L""

#define APP_AUTHOR_MAIL   L"pjh85336@gmail.com"
#define APP_REPO_URL      L"https://github.com/legendsteel11/DeskNoise"
#define APP_REPO_TEXT     L"github.com/legendsteel11/DeskNoise"
#define APP_DEV_URL       L"https://github.com/legendsteel11"
#define APP_DEV_TOOLS     L"Edgetree · TabStick · SweepCap"
