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
    {"Project: %s", "프로젝트: %s"},
    {"Open...", "열기..."},
    {"Reload", "다시 읽기"},
    {"Save", "저장"},
    {"Save as...", "다른 이름으로 저장..."},
    {"Project: %s (changed)", "프로젝트: %s (바뀜)"},
    {"Rules", "규칙"},
    {"Cell size (m)", "칸 크기 (m)"},
    {"Grid (across, long)", "격자 (가로, 세로)"},
    {"Zone rows", "배치 구역 줄 수"},
    {"Round time (s)", "라운드 시간 (초)"},
    {"Gravity (m/s²)", "중력 (m/s²)"},
    {"Retarget interval (s)", "목표 재선택 간격 (초)"},
    {"Stop fraction", "정지 거리 비율"},
    {"Min. damage fraction", "최소 피해 비율"},
    {"Rules change only in deployment (Retry goes back to it).", "규칙은 배치 중에만 바꿀 수 있습니다(다시 시도로 돌아갑니다)."},
    {"Not changed: %s", "바꾸지 않았습니다: %s"},
    {"the project's text is too large", "프로젝트 글이 너무 큽니다"},
    {"Opened %s", "%s 파일을 열었습니다"},
    {"Saved %s", "%s 파일에 저장했습니다"},
    {"%s has %u error(s)", "%s 파일에 오류가 %u개 있습니다"},
    {"%s has %u error(s), so the project in use stays. The first: %s", "%s 파일에 오류가 %u개 있어 지금 프로젝트를 그대로 둡니다. 첫 오류: %s"},
    {"%s is larger than %u KB", "%s 파일이 %u KB보다 큽니다"},
    {"The file could not be opened or saved: %s", "파일을 열거나 저장하지 못했습니다: %s"},
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
    {"Pause", "일시정지"},
    {"< Tick", "< 틱"},
    {"Tick >", "틱 >"},
    {"Tap a cell of your zone (blue) to place the chosen unit; tap a placed unit to remove it. Drag to move the view, drag with the right button or two fingers to turn it, and use the wheel or pinch to zoom.",
     "내 구역(파란색)의 칸을 탭하면 고른 유닛을 놓고, 놓인 유닛을 탭하면 지웁니다. 드래그로 화면을 옮기고, 오른쪽 버튼 드래그나 두 손가락으로 돌리고, 휠이나 핀치로 확대합니다."},
};
// clang-format on

void game_strings_init(void)
{
    nv_strings_set_table(entries, NV_ARRAY_COUNT(entries));
}
