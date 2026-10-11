// The line-based text reading in engine/text.c: lines, tokens, numbers, names, and the error reports.
#include <engine/text.h>

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

static NvTextToken token_of(const char* text)
{
    return (NvTextToken){text, (u32)strlen(text)};
}

static NvTextTokenized tokenize_text(NvTextReader* reader, const char* text)
{
    return nv_text_tokenize(reader, text, text + strlen(text), 1);
}

int main(void)
{
    // Lines: a text without a final '\n' still has its last line, and an empty line is a line.
    {
        NvTextLines lines = {"one\n\ntwo", 8, 0, 0};
        NvTextLineRange first = nv_text_next_line(&lines);
        CHECK(first.ok && first.end - first.begin == 3 && lines.number == 1);
        NvTextLineRange blank = nv_text_next_line(&lines);
        CHECK(blank.ok && blank.end == blank.begin && lines.number == 2);
        NvTextLineRange last = nv_text_next_line(&lines);
        CHECK(last.ok && last.end - last.begin == 3 && lines.number == 3);
        CHECK(!nv_text_next_line(&lines).ok);
        NvTextLines empty = {"", 0, 0, 0};
        CHECK(!nv_text_next_line(&empty).ok);
        NvTextLines trailing = {"a\n", 2, 0, 0};
        CHECK(nv_text_next_line(&trailing).ok && !nv_text_next_line(&trailing).ok);
    }

    // Tokens: indentation, comments, CRLF, trailing spaces, the token limit.
    {
        NvTextTokenized t = tokenize_text(NULL, "    cost 100   # comment");
        CHECK(t.ok && t.line.indent == 4 && t.line.token_count == 2 && t.line.number == 1);
        CHECK(nv_text_token_is(t.line.tokens[0], "cost") && nv_text_token_is(t.line.tokens[1], "100"));
        CHECK(!nv_text_token_is(t.line.tokens[0], "cos") && !nv_text_token_is(t.line.tokens[0], "costs"));

        t = tokenize_text(NULL, "   # only a comment");
        CHECK(t.ok && t.line.token_count == 0); // blank: its indentation does not matter
        t = tokenize_text(NULL, "");
        CHECK(t.ok && t.line.token_count == 0);
        t = tokenize_text(NULL, "key value\r");
        CHECK(t.ok && t.line.token_count == 2 && nv_text_token_is(t.line.tokens[1], "value"));
        t = tokenize_text(NULL, "a#b c");
        CHECK(t.ok && t.line.token_count == 1 && nv_text_token_is(t.line.tokens[0], "a"));
        t = tokenize_text(NULL, "a    b     c");
        CHECK(t.ok && t.line.token_count == 3);

        t = tokenize_text(NULL, "a b c d e f g h");
        CHECK(t.ok && t.line.token_count == NV_TEXT_MAX_TOKENS);
        t = tokenize_text(NULL, "a b c d e f g h i");
        CHECK(!t.ok);
        t = tokenize_text(NULL, "a\tb");
        CHECK(!t.ok);
        t = tokenize_text(NULL, "\tkey");
        CHECK(!t.ok);
        t = tokenize_text(NULL, "key # a tab in a comment is fine\t");
        CHECK(t.ok && t.line.token_count == 1);
    }

    // Numbers.
    {
        NvTextNumber n = nv_text_parse_number(NULL, 1, token_of("120"), 1);
        CHECK(n.ok && n.value == 120.0);
        n = nv_text_parse_number(NULL, 1, token_of("-3"), 0);
        CHECK(n.ok && n.value == -3.0);
        n = nv_text_parse_number(NULL, 1, token_of("+0.5"), 0);
        CHECK(n.ok && n.value == 0.5);
        n = nv_text_parse_number(NULL, 1, token_of(".5"), 0);
        CHECK(n.ok && n.value == 0.5);
        n = nv_text_parse_number(NULL, 1, token_of("5."), 0);
        CHECK(n.ok && n.value == 5.0);
        n = nv_text_parse_number(NULL, 1, token_of("0.5"), 1); // a fraction where an integer goes
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("1e3"), 0); // no exponents
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("abc"), 0);
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("-"), 0);
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("."), 0);
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("1.2.3"), 0);
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("12345678901234567890123456789012345678901234567890"), 0); // too long
        CHECK(!n.ok);
        n = nv_text_parse_number(NULL, 1, token_of("0.1"), 0); // read as strtod does, so every build gets the same float
        CHECK(n.ok && n.value == 0.1);
    }

    // Names.
    {
        char name[8];
        CHECK(nv_text_parse_name(NULL, 1, token_of("Crawler"), name, sizeof(name)) && strcmp(name, "Crawler") == 0);
        CHECK(nv_text_parse_name(NULL, 1, token_of("_x1"), name, sizeof(name)) && strcmp(name, "_x1") == 0);
        CHECK(!nv_text_parse_name(NULL, 1, token_of("1x"), name, sizeof(name)));      // not starting with a digit
        CHECK(!nv_text_parse_name(NULL, 1, token_of("a-b"), name, sizeof(name)));
        CHECK(!nv_text_parse_name(NULL, 1, token_of("Crawlers"), name, sizeof(name))); // 8 bytes do not fit in 8 with the NUL
        CHECK(nv_text_parse_name(NULL, 1, token_of("Crawler"), name, sizeof(name)));   // 7 do
    }

    // Shown: a long token is cut at 24 bytes, but never inside a character.
    {
        CHECK(nv_text_shown(token_of("short")) == 5);
        CHECK(nv_text_shown(token_of("0123456789012345678901234567890")) == 24);
        // 23 ASCII bytes and then a 3-byte syllable that would end at 26: cut before it.
        CHECK(nv_text_shown(token_of("01234567890123456789012\xEA\xB0\x80xx")) == 23);
    }

    // Reports: the first goes to `first_error`, every one is counted, and a missing reader reports nothing.
    {
        u32 total = 0;
        char first[160] = "";
        NvTextReader reader = {.file_name = "a.txt", .log_source = "test", .total = &total, .first_error = first,
                               .first_error_capacity = sizeof(first)};
        nv_text_report(&reader, 12, "unknown key '%s'", "healt");
        CHECK(reader.errors == 1 && total == 1 && strcmp(first, "a.txt:12: unknown key 'healt'") == 0);
        nv_text_report(&reader, 0, "no units");
        CHECK(reader.errors == 2 && total == 2 && strcmp(first, "a.txt:12: unknown key 'healt'") == 0); // the first stays
        nv_text_report(NULL, 3, "nobody hears this");
        CHECK(reader.errors == 2 && total == 2);

        // A second read sharing the total counts on from it, and its own errors start from zero.
        NvTextReader second = {.file_name = "b.txt", .log_source = "test", .total = &total, .first_error = first,
                               .first_error_capacity = sizeof(first)};
        nv_text_report(&second, 1, "again");
        CHECK(second.errors == 1 && total == 3 && strcmp(first, "a.txt:12: unknown key 'healt'") == 0);

        // Without a total, the read's own count decides what is first.
        char only[160] = "";
        NvTextReader alone = {.file_name = "c.txt", .first_error = only, .first_error_capacity = sizeof(only)};
        nv_text_report(&alone, 4, "one");
        nv_text_report(&alone, 5, "two");
        CHECK(alone.errors == 2 && strcmp(only, "c.txt:4: one") == 0);

        // A long message is cut, and never inside a character.
        char cut[160] = "";
        NvTextReader longer = {.file_name = "d.txt", .first_error = cut, .first_error_capacity = sizeof(cut)};
        char message[200];
        memset(message, 'x', sizeof(message));
        message[198] = (char)0xEA; // the lead byte of a syllable, cut off by the end
        message[199] = 0;
        nv_text_report(&longer, 7, "%s", message);
        CHECK(strncmp(cut, "d.txt:7: xxx", 12) == 0 && strlen(cut) < sizeof(cut));
        CHECK(((u8)cut[strlen(cut) - 1] & 0xC0) != 0xC0); // does not end on a lead byte
    }

    if (!failures)
        printf("text_test: ok\n");
    return failures ? 1 : 0;
}
