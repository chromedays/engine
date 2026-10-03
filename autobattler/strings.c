// The auto-battler's Korean UI strings (docs/specs/korean.md): pairs of the English text, exactly as written in the code, and its
// Korean. The lookup is the engine's (engine/strings.h); tests/strings_test.mjs checks that every string the code wraps has a row
// and that a row keeps its printf conversions. Unit and weapon names come from the definition files and stay English.

#include "game.h"

// clang-format off
global const NvStringPair entries[] = {
    {"Battle", "전투"},
    {"Deployment", "배치"},
    {"Fight", "전투 중"},
    {"Result", "결과"},
    {"Language", "언어"},
    {"Definitions could not be loaded", "정의 파일을 불러오지 못했습니다"},
    {"Supply: %u / %u", "공급: %u / %u"},
    {"Player: %u alive", "아군: %u 생존"},
    {"Enemy: %u alive", "적군: %u 생존"},
    {"Time: %.1f / %.0f s (%u / %u ticks)", "시간: %.1f / %.0f초 (%u / %u틱)"},
    {"Victory", "승리"},
    {"Defeat", "패배"},
    {"Draw", "무승부"},
    {"Value left: %.0f against %.0f", "남은 가치: %.0f 대 %.0f"},
    {"Start", "시작"},
    {"Retry", "다시 시도"},
    {"Reset", "초기화"},
    {"Tap a cell of your zone (blue) to place the chosen unit; tap a placed unit to remove it. Drag to move the view, drag with the right button or two fingers to turn it, and use the wheel or pinch to zoom.",
     "내 구역(파란색)의 칸을 탭하면 고른 유닛을 놓고, 놓인 유닛을 탭하면 지웁니다. 드래그로 화면을 옮기고, 오른쪽 버튼 드래그나 두 손가락으로 돌리고, 휠이나 핀치로 확대합니다."},
};
// clang-format on

void game_strings_init(void)
{
    nv_strings_set_table(entries, NV_ARRAY_COUNT(entries));
}
