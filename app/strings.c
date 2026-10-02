#include "strings.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

// The Korean UI strings (docs/specs/korean.md): pairs of the English text, exactly as written in
// the code, and its Korean. tests/strings_test.mjs checks that every string the code wraps has a
// row and that a row keeps its printf conversions. Hangul that is not among KS X 1001's 2,350
// syllables is added to the font by tools/subset_hangul.sh.
typedef struct Entry {
    const char* english;
    const char* korean;
} Entry;

// clang-format off
local_persist const Entry entries[] = {
    {"Info", "정보"},
    {"Warning", "경고"},
    {"Error", "오류"},
    {"Warn", "경고"},
    {"Auto", "자동"},
    {"Auto-scroll", "자동 스크롤"},
    {"Clear", "지우기"},
    {"Copy", "복사"},
    {"Filter (-word leaves it out)", "필터 (-단어는 제외)"},
    {"Copy message", "메시지 복사"},
    {"Not saved: the state is larger than %u KB.", "저장되지 않음: 상태가 %u KB보다 큽니다."},
    {"Not saved: writing %s failed.", "저장되지 않음: %s 쓰기에 실패했습니다."},
    {"Reload", "다시 불러오기"},
    {"The save could not be loaded: %s. The app started fresh and kept it as %s.", "저장 파일을 불러오지 못했습니다: %s. 앱을 새로 시작했고 파일은 %s(으)로 남겨 두었습니다."},
    {"Reset everything?", "모두 초기화할까요?"},
    {"Delete the save and start over as on a first visit?", "저장 파일을 지우고 처음 방문한 것처럼 다시 시작할까요?"},
    {"Reset", "초기화"},
    {"Cancel", "취소"},
    {"Autosave", "자동 저장"},
    {"Browser storage is unavailable here (a private window may refuse it), so nothing is saved.", "여기서는 브라우저 저장소를 쓸 수 없어(시크릿 창은 거부할 수 있습니다) 아무것도 저장되지 않습니다."},
    {"Save now", "지금 저장"},
    {"Saved %.0f s ago (%u bytes)", "%.0f초 전에 저장됨 (%u바이트)"},
    {"Not saved yet this visit.", "이번 방문에는 아직 저장하지 않았습니다."},
    {"Browser storage: %s", "브라우저 저장소: %s"},
    {"Show save", "저장 파일 보기"},
    {"Language", "언어"},
    {"Scene", "씬"},
    {"Inspector", "인스펙터"},
    {"View", "보기"},
    {"Textures", "텍스처"},
    {"Stress", "스트레스"},
    {"Search", "검색"},
    {"Clear the search", "검색어 지우기"},
    {"No match for '%s'", "'%s'와(과) 일치하는 항목이 없습니다"},
    {"Action", "동작"},
    {"Setting", "설정"},
    {"Node", "노드"},
    {"and %u more: keep typing", "외 %u개: 계속 입력하세요"},
    {"Type a command, node or setting", "명령, 노드 또는 설정을 입력하세요"},
    {"No match", "일치하는 항목 없음"},
    {"File", "파일"},
    {"Edit", "편집"},
    {"Undo", "실행 취소"},
    {"Redo", "다시 실행"},
    {"Clear the selection", "선택 해제"},
    {"Play", "재생"},
    {"Play or Stop", "재생 또는 정지"},
    {"Gizmo", "기즈모"},
    {"Move", "이동"},
    {"Rotate", "회전"},
    {"Scale", "크기"},
    {"Local or world axes", "로컬 또는 월드 축"},
    {"Snap, the other way round, while dragging", "드래그하는 동안 스냅을 반대로"},
    {"Ctrl (held)", "Ctrl (누르는 동안)"},
    {"Move the orbit point to the selection", "궤도 중심을 선택한 노드로 이동"},
    {"Camera follows selection on or off", "카메라가 선택을 따라가기 켜기/끄기"},
    {"Back to the start view", "처음 시점으로 돌아가기"},
    {"Docks", "도크"},
    {"Scene dock", "씬 도크"},
    {"Inspector dock", "인스펙터 도크"},
    {"Console dock", "콘솔 도크"},
    {"Find", "찾기"},
    {"Command palette", "명령 팔레트"},
    {"Search the panel", "패널 검색"},
    {"Help", "도움말"},
    {"Keyboard shortcuts", "키보드 단축키"},
    {"Reset everything...", "모두 초기화..."},
    {"Show the Showcase scene", "쇼케이스 씬 보기"},
    {"Show the Stress scene", "스트레스 씬 보기"},
    {"Open the Textures tab", "텍스처 탭 열기"},
    {"Clear the console", "콘솔 지우기"},
    {"Keys do nothing while a text field is being edited or a popup is open.", "텍스트 입력 중이거나 팝업이 열려 있으면 단축키가 동작하지 않습니다."},
    {"Frame (1 s average, ms)", "프레임 (1초 평균, ms)"},
    {"Frame", "프레임"},
    {"FPS", "FPS"},
    {"Worst frame", "최악 프레임"},
    {"GPU scene pass", "GPU 씬 패스"},
    {"GPU shadow pass", "GPU 그림자 패스"},
    {"GPU upscale pass", "GPU 업스케일 패스"},
    {"GPU passes", "GPU 패스"},
    {"no timestamps", "타임스탬프 없음"},
    {"CPU anim", "CPU 애니메이션"},
    {"CPU scene", "CPU 씬"},
    {"CPU draw", "CPU 그리기"},
    {"CPU ui", "CPU UI"},
    {"Nodes", "노드"},
    {"Draws", "드로우"},
    {"Triangles", "삼각형"},
    {"Skinned draws", "스킨 드로우"},
    {"Skin matrices", "스킨 행렬"},
    {"Debug lines", "디버그 선"},
    {"Pipeline changes", "파이프라인 변경"},
    {"Material changes", "머티리얼 변경"},
    {"Mesh changes", "메시 변경"},
    {"Shadow draws", "그림자 드로우"},
    {"Anti-aliasing", "안티앨리어싱"},
    {"MSAA 4x", "MSAA 4x"},
    {"off", "끔"},
    {"Resolution", "해상도"},
    {"Workloads", "부하"},
    {"Cube grid", "큐브 격자"},
    {"Cubes", "큐브"},
    {"Many colors", "많은 색"},
    {"Colors", "색 개수"},
    {"Deep chain", "깊은 체인"},
    {"Links", "링크"},
    {"Crowd", "군중"},
    {"Characters", "캐릭터"},
    {"Churn", "생성/삭제 반복"},
    {"Cubes / frame", "큐브 / 프레임"},
    {"Crowd bones", "군중 뼈대"},
    {"Built: %u cubes, %u links, %u of %u characters", "생성됨: 큐브 %u개, 링크 %u개, 캐릭터 %u/%u개"},
    {"Benchmark", "벤치마크"},
    {"Stop", "정지"},
    {"Run benchmark", "벤치마크 실행"},
    {"Shadows: %s", "그림자: %s"},
    {"Anti-aliasing: %s", "안티앨리어싱: %s"},
    {"Resolution: %s", "해상도: %s"},
    {"Copy results", "결과 복사"},
    {"Shadows are off: a 1x1 placeholder stays bound.", "그림자가 꺼져 있습니다: 1x1 자리표시 텍스처가 연결되어 있습니다."},
    {"Made samplable on the next frame.", "다음 프레임에 샘플링할 수 있게 됩니다."},
    {"No preview: it is discarded after every frame; the scene pass resolves it into the swapchain.", "미리보기 없음: 매 프레임 후 버려지며, 씬 패스가 스왑체인으로 리졸브합니다."},
    {"No preview: the UI pass is drawing into it, and a pass cannot sample its own target.", "미리보기 없음: UI 패스가 여기에 그리고 있으며, 패스는 자신의 대상을 샘플링할 수 없습니다."},
    {"Used by", "사용처"},
    {"No node of this scene.", "이 씬에는 해당 노드가 없습니다."},
    {"No preview.", "미리보기 없음."},
    {"Zoom", "확대"},
    {"White at", "흰색 거리"},
    {"Distance from the camera shown white; nearer is darker", "카메라에서 이 거리를 흰색으로 표시하고, 더 가까울수록 어둡습니다"},
    {"Range", "범위"},
    {"Stored depth shown black and white", "저장된 깊이를 검정과 흰색으로 표시합니다"},
    {"Mip", "밉"},
    {"Checkerboard", "체크무늬"},
    {"texel (%u, %u) of %ux%u, uv (%.3f, %.3f)", "텍셀 (%u, %u) / %ux%u, uv (%.3f, %.3f)"},
    {"%ux%u shown; drag to pan", "%ux%u 표시 중, 드래그하여 이동"},
    {"In use only", "사용 중인 것만"},
    {"%u textures, %s", "텍스처 %u개, %s"},
    {"GPU memory of the textures listed, not counting the swapchain (the browser owns it)", "나열된 텍스처의 GPU 메모리입니다. 스왑체인(브라우저 소유)은 포함하지 않습니다"},
    {"Materials", "머티리얼"},
    {"Render targets", "렌더 타깃"},
    {"UI", "UI"},
    {"< Textures", "< 텍스처"},
    {"Pick a texture to see it large.", "텍스처를 고르면 크게 볼 수 있습니다."},
    {"Show it in the Textures tab", "텍스처 탭에서 보기"},
    {"Multiplied with the color.", "색과 곱해집니다."},
    {"... %u more levels", "... %u단계 더 있음"},
    {"and %u more", "외 %u개"},
    {"Animator", "애니메이터"},
    {"Jump", "점프"},
    {"Speed", "속도"},
    {"Fade", "페이드"},
    {"Blend", "블렌드"},
    {"Weight", "가중치"},
    {"Root motion", "루트 모션"},
    {"Turn", "회전 속도"},
    {"Back to center", "중앙으로 돌아가기"},
    {"Look at target", "대상 바라보기"},
    {"Attach", "부착"},
    {"Joint", "관절"},
    {"Visible", "보이기"},
    {"Off", "끔"},
    {"Edges", "가장자리"},
    {"Fixed size", "고정 크기"},
    {"Mode", "모드"},
    {"1/1 (full)", "1/1 (전체)"},
    {"360 x 640 (portrait)", "360 x 640 (세로)"},
    {"720 x 1280 (portrait)", "720 x 1280 (세로)"},
    {"Custom", "사용자 지정"},
    {"Size", "크기"},
    {"Whole multiples", "정수 배"},
    {"Fit to viewport", "뷰포트에 맞추기"},
    {"Stretch to viewport", "뷰포트에 늘리기"},
    {"Fit", "맞춤"},
    {"Width", "너비"},
    {"Height", "높이"},
    {"Renders %u x %u; a pixel shows as %.0f x %.0f screen pixels (%.1f per CSS pixel)", "%u x %u로 렌더링하며, 픽셀 하나가 화면 픽셀 %.0f x %.0f개로 보입니다 (CSS 픽셀당 %.1f)"},
    {"Renders %u x %u, fitted to the viewport at %.2fx: pixels are uneven blocks of whole screen pixels", "%u x %u로 렌더링하며 뷰포트에 %.2f배로 맞춥니다: 픽셀은 화면 픽셀 단위의 고르지 않은 블록입니다"},
    {"Renders %u x %u, stretched to the viewport: %.2f x %.2f screen pixels per pixel, in uneven blocks", "%u x %u로 렌더링하며 뷰포트에 늘립니다: 픽셀당 화면 픽셀 %.2f x %.2f개, 고르지 않은 블록"},
    {"Renders %u x %u, larger than the viewport: shown at %.2fx x %.2fx, some pixels dropped", "%u x %u로 렌더링하며 뷰포트보다 큽니다: %.2f배 x %.2f배로 표시되어 일부 픽셀이 버려집니다"},
    {"Shadows", "그림자"},
    {"Map size", "맵 크기"},
    {"32-bit float", "32비트 실수"},
    {"16-bit", "16비트"},
    {"Format", "형식"},
    {"Low", "낮음"},
    {"High", "높음"},
    {"Filter", "필터"},
    {"Distance", "거리"},
    {"Show light box", "라이트 박스 보기"},
    {"Select a node in the Scene tab.", "씬 탭에서 노드를 선택하세요."},
    {"%u selected", "%u개 선택됨"},
    {"Keep one", "하나만 남기기"},
    {"Keep only the node shown here selected", "여기 보이는 노드만 선택된 채로 둡니다"},
    {"Multi", "다중"},
    {"Name", "이름"},
    {"Local", "로컬"},
    {"Snap", "스냅"},
    {"Position", "위치"},
    {"Rotation", "회전"},
    {"Mesh", "메시"},
    {"Color", "색"},
    {"Texture", "텍스처"},
    {"Camera", "카메라"},
    {"Field of view", "시야각"},
    {"Light", "조명"},
    {"Intensity", "세기"},
    {"Playing", "재생 중"},
    {"Showcase", "쇼케이스"},
    {"%.0f FPS (%.2f ms)", "%.0f FPS (%.2f ms)"},
    {"Commit: %s", "커밋: %s"},
    {"Camera yaw", "카메라 좌우"},
    {"Camera pitch", "카메라 상하"},
    {"Camera follows selection", "카메라가 선택을 따라감"},
    {"Show bones", "뼈대 보기"},
    {"Planet orbit", "행성 공전"},
    {"Playing: edits are lost on Stop.", "재생 중: 정지하면 편집 내용이 사라집니다."},
    {"Console", "콘솔"},
    {"Reset...", "초기화..."},
    {"Showcase scene", "쇼케이스 씬"},
    {"Stress scene", "스트레스 씬"},
    {"Stop and restore the scene", "정지하고 씬 복원"},
    {"Run the scene", "씬 실행"},
    {"Show Console", "콘솔 보기"},
    {"%u new", "새 항목 %u개"},
    {"Hide", "숨기기"},
    {"Collapse the dock to its strip", "도크를 탭 줄로 접기"},
};
// clang-format on

#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))
#define TABLE_SIZE 2048 // a power of two, more than twice the entries
#define TL_SLOTS 64
#define TL_SIZE 192

local_persist Language language = LANG_EN;
local_persist u16 table[TABLE_SIZE]; // entry index + 1; 0 = empty
local_persist b32 table_built;

EM_JS(int, js_browser_is_korean, (void), {
    const language = (navigator.language || "").toLowerCase();
    return language.startsWith("ko") ? 1 : 0;
});

Language strings_language(void) { return language; }
void strings_set_language(Language value) { language = value < LANG_COUNT ? value : LANG_EN; }
Language strings_browser_language(void) { return js_browser_is_korean() ? LANG_KO : LANG_EN; }

internal u32 hash_text(const char* text, umm length)
{
    u32 hash = 2166136261u;
    for (umm i = 0; i < length; ++i)
        hash = (hash ^ (u8)text[i]) * 16777619u;
    return hash;
}

internal void build_table(void)
{
    table_built = 1;
    for (u32 i = 0; i < ENTRY_COUNT; ++i) {
        u32 slot = hash_text(entries[i].english, strlen(entries[i].english)) & (TABLE_SIZE - 1);
        while (table[slot])
            slot = (slot + 1) & (TABLE_SIZE - 1);
        table[slot] = (u16)(i + 1);
    }
}

// The entry for `length` bytes of English, or NULL.
internal const Entry* find(const char* english, umm length)
{
    if (!table_built)
        build_table();
    u32 slot = hash_text(english, length) & (TABLE_SIZE - 1);
    while (table[slot]) {
        const Entry* entry = &entries[table[slot] - 1];
        if (strlen(entry->english) == length && memcmp(entry->english, english, length) == 0)
            return entry;
        slot = (slot + 1) & (TABLE_SIZE - 1);
    }
    return NULL;
}

const char* strings_find_korean(const char* english)
{
    const Entry* entry = find(english, strlen(english));
    return entry ? entry->korean : NULL;
}

const char* T(const char* english)
{
    if (language != LANG_KO)
        return english;
    const Entry* entry = find(english, strlen(english));
    return entry ? entry->korean : english;
}

const char* TL(const char* english)
{
    local_persist char slots[TL_SLOTS][TL_SIZE];
    local_persist u32 next;
    char* out = slots[next++ % TL_SLOTS];
    // The text is what comes before any "##"; the id is what follows "###", or all of it.
    const char* mark = strstr(english, "##");
    umm text_length = mark ? (umm)(mark - english) : strlen(english);
    const char* id = english;
    const char* triple = strstr(english, "###");
    if (triple)
        id = triple + 3;
    const char* shown = english;
    if (language == LANG_KO) {
        const Entry* entry = find(english, text_length);
        if (entry) {
            snprintf(out, TL_SIZE, "%s###%s", entry->korean, id);
            nv_utf8_trim(out);
            return out;
        }
    }
    snprintf(out, TL_SIZE, "%.*s###%s", (int)text_length, shown, id);
    nv_utf8_trim(out);
    return out;
}
