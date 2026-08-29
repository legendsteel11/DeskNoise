// i18n.h - UI strings in Korean and English.
//
// One X-macro list holds both languages side by side, so the enum and the two
// tables cannot drift apart. Add a line here and both languages are covered.
// Entries whose name starts with S_FMT_ are format strings: the placeholders
// must stay identical, in the same order, in both languages.
#pragma once
#include <windows.h>

#define DESKNOISE_STRINGS(X)                                                                       \
    /* sound modes, long form (combo box) */                                                       \
    X(S_MODE_SINE,          L"삐 소리 (사인파)",                     L"Beep (sine)")                \
    X(S_MODE_NB,            L"바람 소리 (좁은 대역 노이즈)",         L"Wind (narrow-band noise)")   \
    X(S_MODE_WHITE,         L"라디오 잡음 (화이트 노이즈)",          L"Static (white noise)")       \
    X(S_MODE_PINK,          L"빗소리 (핑크 노이즈)",                 L"Rain (pink noise)")          \
    X(S_MODE_BROWN,         L"파도 소리 (브라운 노이즈)",            L"Waves (brown noise)")        \
    /* sound modes, short form (layer rows) */                                                     \
    X(S_MODES_SINE,         L"삐",                                   L"Beep")                       \
    X(S_MODES_NB,           L"바람",                                 L"Wind")                       \
    X(S_MODES_WHITE,        L"라디오",                               L"Static")                     \
    X(S_MODES_PINK,         L"비",                                   L"Rain")                       \
    X(S_MODES_BROWN,        L"파도",                                 L"Waves")                      \
    /* layer rows */                                                                               \
    X(S_FMT_LAYER_BEAT,     L"흔들림 %.1fHz",                        L"wobble %.1fHz")              \
    /* master volume */                                                                            \
    X(S_MASTER_MUTE,        L"전체 음량 : 무음",                     L"Master volume: muted")       \
    X(S_FMT_MASTER,         L"전체 음량 : %d%%  (%.1f dB)",          L"Master volume: %d%%  (%.1f dB)") \
    X(S_FMT_MASTER_FADE,    L"전체 음량 : %d%%  ·  자동 줄임 설정됨", L"Master volume: %d%%  ·  fade out set") \
    /* status line */                                                                              \
    X(S_STOPPED,            L"정지됨",                               L"Stopped")                    \
    X(S_RESTING,            L"대기 중",                              L"Waiting")                    \
    /* the status bar is one line now, so these stay short */                                      \
    X(S_NO_DEVICE,          L"출력 장치 없음 · 연결되면 재생",       L"No output device · resumes when connected") \
    X(S_NO_LAYER,           L"소리 없음 · 레이어를 체크해 주세요",   L"Nothing playing · check a layer") \
    X(S_FMT_PLAYING_N,      L"재생 중 · 레이어 %d개",                L"Playing · %d layers")        \
    X(S_LIMITING,           L" · 출력 한계, 음량을 낮춰 주세요",     L" · output limit, lower the volume") \
    X(S_PLAYING,            L"재생 중",                              L"Playing")                    \
    X(S_FMT_TIME_PLAY,      L" · %d:%02d 후 재생",                   L" · plays in %d:%02d")        \
    X(S_FMT_TIME_REST,      L" · %d:%02d 후 대기",                   L" · waits in %d:%02d")        \
    X(S_FMT_TIME_STOP,      L" · %d:%02d 후 정지",                   L" · stops in %d:%02d")        \
    /* left-hand side of the status bar: how long this session has been playing */                 \
    X(S_FMT_PLAYED_MIN,     L"재생시간 %d분",                        L"played %d min")              \
    X(S_FMT_PLAYED_HOUR,    L"재생시간 %d시간 %d분",                 L"played %dh %dm")             \
    /* mid row: bandwidth and wobble share it */                                                   \
    X(S_FMT_BEAT_RATE,      L"흔들림 : %.1f Hz",                     L"Wobble: %.1f Hz")            \
    X(S_BEAT_OFF,           L"흔들림 : 사용 안 함",                  L"Wobble: off")                \
    X(S_FMT_BW,             L"대역폭 : %.2f 옥타브",                 L"Bandwidth: %.2f octaves")    \
    X(S_BW_FULL,            L"대역폭 : 전체 (주파수 적용 안 함)",    L"Bandwidth: full (frequency unused)") \
    /* editor labels */                                                                            \
    X(S_VOL_MUTE,           L"음량 : 무음",                          L"Volume: muted")              \
    X(S_FMT_VOL,            L"음량 : %d%%  (%.1f dB)",               L"Volume: %d%%  (%.1f dB)")    \
    X(S_BAL_CENTER,         L"밸런스 : 중앙",                        L"Balance: center")            \
    X(S_FMT_BAL_LEFT,       L"밸런스 : 왼쪽 %d%%",                   L"Balance: left %d%%")         \
    X(S_FMT_BAL_RIGHT,      L"밸런스 : 오른쪽 %d%%",                 L"Balance: right %d%%")        \
    /* presets */                                                                                  \
    X(S_PRESET,             L"프리셋",                               L"Preset")                     \
    X(S_PRESET_CUSTOM,      L"-",                                    L"-")                          \
    X(S_PRESET_UNNAMED,     L"(이름 없음)",                          L"(unnamed)")                  \
    X(S_PRESET_FULL,        L"프리셋을 더 저장할 수 없습니다.",      L"No room for another preset.") \
    X(S_FMT_PRESET_REPLACE, L"\"%s\" 프리셋이 이미 있습니다. 덮어쓸까요?", \
                            L"A preset named \"%s\" already exists. Replace it?")                   \
    X(S_FMT_PRESET_DELETE,  L"\"%s\" 프리셋을 삭제할까요?",          L"Delete the preset \"%s\"?")  \
    /* buttons and static labels */                                                                \
    X(S_SAVE,               L"저장",                                 L"Save")                       \
    X(S_MODE_DEFAULT,       L"기본값",                               L"Reset")                      \
    X(S_DELETE,             L"삭제",                                 L"Delete")                     \
    X(S_CANCEL,             L"취소",                                 L"Cancel")                     \
    X(S_NAME,               L"이름",                                 L"Name")                       \
    X(S_DLG_SAVE_PRESET,    L"프리셋 저장",                          L"Save preset")                \
    X(S_LAYERS_HINT,        L"소리 레이어",                          L"Sound layers")               \
    /* box captions, drawn sitting on the box border */                                            \
    X(S_FMT_BOX_LAYER,      L"소리 레이어 %d 설정",                  L"Sound layer %d")             \
    X(S_BOX_COMMON,         L"공통 설정",                            L"Overall")                    \
    X(S_SOURCE,             L"소리 선택",                            L"Sound")                      \
    X(S_FREQ,               L"주파수 :",                             L"Frequency:")                 \
    X(S_BANDWIDTH,          L"대역폭",                               L"Bandwidth")                  \
    X(S_BEAT,               L"흔들림",                               L"Wobble")                     \
    X(S_VOLUME,             L"음량",                                 L"Volume")                     \
    X(S_BALANCE,            L"밸런스",                               L"Balance")                    \
    X(S_MASTER,             L"전체 음량",                            L"Master volume")              \
    X(S_PLAY,               L"재생",                                 L"Play")                       \
    X(S_STOP,               L"정지",                                 L"Stop")                       \
    X(S_REST,               L"간격",                                 L"Gap")                        \
    X(S_FADE,               L"자동 줄임",                            L"Fade out")                   \
    /* play and rest duration combos */                                                            \
    X(S_CONTINUOUS,         L"계속",                                 L"Continuous")                 \
    X(S_NONE,               L"없음",                                 L"None")                       \
    X(S_FMT_MINUTES,        L"%d분",                                 L"%d min")                     \
    /* checkboxes and tray menu */                                                                 \
    X(S_HIDE_TRAY,          L"트레이",                               L"Tray")                       \
    X(S_TRAY_SHOW,          L"창 열기",                              L"Open window")                \
    X(S_TRAY_RESTART,       L"다시 시작",                            L"Restart")                    \
    X(S_TRAY_EXIT,          L"종료",                                 L"Exit")                       \
    /* about box */                                                                                \
    X(S_ABOUT,              L"앱 정보",                              L"About")                      \
    X(S_AB_VERSION,         L"버전",                                 L"Version")                    \
    X(S_AB_AUTHOR,          L"제작자",                               L"Author")                     \
    X(S_AB_DATE,            L"배포일",                               L"Released")                   \
    X(S_AB_GITHUB,          L"GitHub",                               L"GitHub")                     \
    X(S_AB_TOOLS,           L"같은 개발자의 다른 도구",              L"Other tools by the same developer") \
    X(S_AB_LICENSE,         L"라이선스 요약",                        L"License")                    \
    X(S_AB_LICENSE_TEXT,    L"MIT 라이선스. 별도의 보증 없이 있는 그대로 제공되며, 사용에 따른 책임은 사용자 본인에게 있습니다.", \
                            L"MIT License. Provided as is, without warranty of any kind. You are responsible for how you use it.") \
    X(S_AB_NOTE,            L"안내",                                 L"Notice")                     \
    X(S_AB_NOTE_TEXT,       L"DeskNoise는 의료기기가 아니며 질병의 진단이나 치료를 위한 것이 아닙니다. 음량은 편안하게 들리는 정도로 유지해 주세요. 큰 소리로 듣거나 헤드폰, 이어폰으로 오래 사용하면 청력에 부담이 될 수 있습니다.", \
                            L"DeskNoise is not a medical device and is not intended to diagnose or treat any condition. Please keep the volume at a comfortable level. Listening at high volume for hours — especially through headphones or earbuds — can be hard on your hearing.") \
    X(S_AB_COPY_MAIL,       L"이메일 주소 복사",                     L"Copy email address")         \
    X(S_AB_COPIED,          L"복사했습니다",                         L"Copied")                     \
    X(S_CLOSE,              L"닫기",                                 L"Close")

enum StrId {
#define DESKNOISE_ENUM(id, ko, en) id,
    DESKNOISE_STRINGS(DESKNOISE_ENUM)
#undef DESKNOISE_ENUM
    S_COUNT
};

// Picks the language from the Windows UI language. Call once before any T().
// "/lang=ko" or "/lang=en" on the command line overrides the detected language,
// which is how the other language gets checked without changing Windows.
void I18nInit(LPCWSTR cmdLine);

const WCHAR* T(int id);
