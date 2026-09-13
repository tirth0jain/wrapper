// Host test for the REAL webplayback_reason() text, extracted from
// lite/apple_api.cpp at test time by scripts/test-webplayback-reason.sh (so it
// tests the shipped source, not a hand copy). Bodies are verbatim Apple
// responses captured from the seedbox wrapper log on 2026-09-11/13.
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"

// ---- verbatim extraction marker (filled in by the harness) ----
// EXTRACTED_SOURCE_HERE

static int failures = 0;

static void check(const char* what, const std::string& got, const std::string& want) {
    if (got == want) {
        printf("ok   %-46s -> \"%s\"\n", what, got.c_str());
    } else {
        printf("FAIL %-46s -> \"%s\" (wanted \"%s\")\n", what, got.c_str(), want.c_str());
        failures++;
    }
}

int main() {
    // Apple 3082: explicit-content restriction (the dialog the addon keys on).
    const std::string body3082 =
        "{\"failureType\":\"3082\",\n"
        "\"dialog\":{\"defaultButton\":\"ok\", \"message\":\"To play this item, go to iTunes or your mobile device settings and remove the explicit content restriction for Apple Music.\", \"m-allowed\":false, \"explanation\":\" \", \"initialCheckboxValue\":true, \"okButtonString\":\"OK\"}}";
    check("3082 explicit-content dialog", webplayback_reason(body3082, 200),
          "failureType=3082: To play this item, go to iTunes or your mobile device settings and remove the explicit content restriction for Apple Music.");

    // Apple 3076: catalogue miss (live 2026-09-11, adamId 6807476076).
    const std::string body3076 =
        "{\"failureType\":\"3076\", \n"
        "\"dialog\":{\"defaultButton\":\"ok\", \"message\":\"This song is currently unavailable.\", \"m-allowed\":false, \"explanation\":\" \", \"initialCheckboxValue\":true, \"okButtonString\":\"OK\"}}";
    check("3076 song unavailable", webplayback_reason(body3076, 200),
          "failureType=3076: This song is currently unavailable.");

    // 2002 session-ended shape: no dialog, customerMessage instead.
    const std::string body2002 =
        "{\"failureType\":\"2002\",\"customerMessage\":\"Your session has ended. Please sign in again.\"}";
    check("2002 customerMessage fallback", webplayback_reason(body2002, 200),
          "failureType=2002: Your session has ended. Please sign in again.");

    // Non-JSON body (an HTML error page): fall back to the http status.
    check("non-JSON body -> http status", webplayback_reason("<html>nope</html>", 503), "http status 503");
    check("empty body, status 200", webplayback_reason("", 200), "");

    // Newlines/tabs must never survive: amdl prints the message inside its own
    // single log line and the addon parses that line.
    const std::string multiline =
        "{\"failureType\":\"3082\",\"dialog\":{\"message\":\"line one\nline\ttwo\"}}";
    check("newlines collapsed", webplayback_reason(multiline, 200), "failureType=3082: line one line two");

    if (failures) { printf("%d case(s) FAILED\n", failures); return 1; }
    printf("all webplayback_reason cases passed\n");
    return 0;
}
