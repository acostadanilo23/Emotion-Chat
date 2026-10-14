#ifndef EMOTION_CHAT_H
#define EMOTION_CHAT_H

#include <stddef.h>
#include <stdio.h>

#include "emotion/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EMOTION_CHAT_RESPONSE_ID_CAP 128u

typedef struct EmotionChatConfig {
    const char *endpoint;
    const char *model;
    const char *api_key;
    const char *instructions;
    unsigned max_output_tokens;
    size_t max_response_bytes;
} EmotionChatConfig;

typedef struct EmotionChatSession {
    char previous_response_id[EMOTION_CHAT_RESPONSE_ID_CAP];
} EmotionChatSession;

void emotion_chat_session_reset(EmotionChatSession *session);

EmotionStatus emotion_chat_build_request(const EmotionChatConfig *config,
                                         const EmotionChatSession *session,
                                         EmotionStringView prompt,
                                         EmotionBuffer *out_body);

EmotionStatus emotion_chat_stream(const EmotionChatConfig *config,
                                  EmotionChatSession *session,
                                  EmotionStringView prompt,
                                  FILE *output,
                                  char *error_out,
                                  size_t error_out_size);

#ifdef __cplusplus
}
#endif

#endif
