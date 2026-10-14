#include "emotion_chat.h"

#include <stdio.h>
#include <string.h>

static int expect_body(const char *label,
                       const EmotionChatConfig *config,
                       const EmotionChatSession *session,
                       const char *prompt,
                       const char *expected) {
    EmotionBuffer body;
    EmotionStatus status;
    int failed = 0;

    emotion_buffer_init(&body, EMOTION_MEMORY_PROTOCOL);
    status = emotion_chat_build_request(config, session,
                                        emotion_sv_from_cstr(prompt), &body);
    if (status != EMOTION_OK) {
        fprintf(stderr, "%s: build failed: %s\n", label, emotion_status_string(status));
        failed = 1;
    } else if (body.len != strlen(expected) ||
               memcmp(body.data, expected, body.len) != 0) {
        fprintf(stderr, "%s: unexpected body\nactual:   %.*s\nexpected: %s\n",
                label, (int)body.len, (const char *)body.data, expected);
        failed = 1;
    }
    emotion_buffer_destroy(&body);
    return failed;
}

int main(void) {
    EmotionChatConfig config = {
        .endpoint = "https://api.openai.com/v1/responses",
        .model = "gpt-6-luna",
        .api_key = "test",
        .instructions = "Be helpful.",
        .max_output_tokens = 128u,
        .max_response_bytes = 1024u
    };
    EmotionChatSession session = {{0}};
    int failed = 0;

    emotion_memory_reset();

    failed |= expect_body(
        "first request", &config, &session, "hello \"world\"\n",
        "{\"model\":\"gpt-6-luna\",\"input\":\"hello \\\"world\\\"\\n\","
        "\"stream\":true,\"store\":true,\"max_output_tokens\":128,"
        "\"instructions\":\"Be helpful.\"}");

    (void)snprintf(session.previous_response_id,
                   sizeof(session.previous_response_id), "%s", "resp_123");
    failed |= expect_body(
        "continued request", &config, &session, "next",
        "{\"model\":\"gpt-6-luna\",\"input\":\"next\","
        "\"stream\":true,\"store\":true,\"max_output_tokens\":128,"
        "\"instructions\":\"Be helpful.\",\"previous_response_id\":\"resp_123\"}");

    return failed;
}
