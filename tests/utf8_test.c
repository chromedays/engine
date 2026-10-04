// nv_utf8_fit and nv_utf8_trim keep text valid UTF-8 when it is cut to fit.
#include <engine/base.h>

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("FAILED line %d: %s\n", __LINE__, #condition);                 \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

int main(void)
{
    // "가나다" is three 3-byte syllables.
    const char* hangul = "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4";
    CHECK(nv_utf8_fit(hangul, 9) == 9);
    CHECK(nv_utf8_fit(hangul, 8) == 6); // the third syllable does not fit whole
    CHECK(nv_utf8_fit(hangul, 7) == 6);
    CHECK(nv_utf8_fit(hangul, 6) == 6);
    CHECK(nv_utf8_fit(hangul, 5) == 3);
    CHECK(nv_utf8_fit(hangul, 2) == 0);
    CHECK(nv_utf8_fit("abc", 2) == 2);
    CHECK(nv_utf8_fit("abc", 8) == 3);
    // Mixed: "a" + a 2-byte character (é) + a 4-byte one (an emoji).
    const char* mixed = "a\xC3\xA9\xF0\x9F\x98\x80";
    CHECK(nv_utf8_fit(mixed, 3) == 3);
    CHECK(nv_utf8_fit(mixed, 5) == 3);
    CHECK(nv_utf8_fit(mixed, 7) == 7);

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%s", hangul); // fits
    nv_utf8_trim(buffer);
    CHECK(strcmp(buffer, hangul) == 0);
    snprintf(buffer, 8, "%s", hangul); // cut after 7 bytes: two syllables and a lead byte
    nv_utf8_trim(buffer);
    CHECK(strcmp(buffer, "\xEA\xB0\x80\xEB\x82\x98") == 0);
    snprintf(buffer, 5, "%s", hangul); // one syllable and a lead byte
    nv_utf8_trim(buffer);
    CHECK(strcmp(buffer, "\xEA\xB0\x80") == 0);
    snprintf(buffer, 3, "%s", hangul); // two bytes of one syllable
    nv_utf8_trim(buffer);
    CHECK(buffer[0] == 0);
    snprintf(buffer, 5, "%s", "ab\xC3\xA9z"); // "ab" + é + z cut to 4 bytes: a, b, a whole é
    nv_utf8_trim(buffer);
    CHECK(strcmp(buffer, "ab\xC3\xA9") == 0);

    CHECK(nv_utf8_length('a') == 1 && nv_utf8_length(0xC3) == 2 && nv_utf8_length(0xEA) == 3 && nv_utf8_length(0xF0) == 4);

    if (!failures)
        printf("utf8_test: ok\n");
    return failures ? 1 : 0;
}
