#include "emotion_chat.h"

#include <stdio.h>
#include <string.h>

static EmotionStatus append_bytes(EmotionBuffer *out, const char *text) {
    return emotion_buffer_append(out, text, strlen(text));
}

static EmotionStatus append_json_string(EmotionBuffer *out, EmotionStringView value) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    EmotionStatus status;

    status = emotion_buffer_append_byte(out, '"');
    if (status != EMOTION_OK) return status;

    for (i = 0u; i < value.len; ++i) {
        unsigned char c = (unsigned char)value.data[i];
        const char *escape = NULL;
        char encoded[6];

        switch (c) {
            case '"': escape = "\\\""; break;
            case '\\': escape = "\\\\"; break;
            case '\b': escape = "\\b"; break;
            case '\f': escape = "\\f"; break;
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            default: break;
        }

        if (escape != NULL) {
            status = append_bytes(out, escape);
        } else if (c < 0x20u) {
            encoded[0] = '\\';
            encoded[1] = 'u';
            encoded[2] = '0';
            encoded[3] = '0';
            encoded[4] = hex[(c >> 4) & 0x0fu];
            encoded[5] = hex[c & 0x0fu];
            status = emotion_buffer_append(out, encoded, sizeof(encoded));
        } else {
            status = emotion_buffer_append_byte(out, c);
        }

        if (status != EMOTION_OK) return status;
    }

    return emotion_buffer_append_byte(out, '"');
}

void emotion_chat_session_reset(EmotionChatSession *session) {
    if (session != NULL) session->previous_response_id[0] = '\0';
}

EmotionStatus emotion_chat_build_request(const EmotionChatConfig *config,
                                         const EmotionChatSession *session,
                                         EmotionStringView prompt,
                                         EmotionBuffer *out_body) {
    char token_buffer[32];
    int token_len;
    EmotionStatus status;

    if (config == NULL || session == NULL || out_body == NULL ||
        config->model == NULL || config->model[0] == '\0' ||
        prompt.data == NULL || prompt.len == 0u ||
        config->max_output_tokens < 16u) {
        return EMOTION_ERR_INVALID_ARGUMENT;
    }

    emotion_buffer_clear(out_body);

    status = append_bytes(out_body, "{\"model\":");
    if (status != EMOTION_OK) return status;
    status = append_json_string(out_body, emotion_sv_from_cstr(config->model));
    if (status != EMOTION_OK) return status;

    status = append_bytes(out_body, ",\"input\":");
    if (status != EMOTION_OK) return status;
    status = append_json_string(out_body, prompt);
    if (status != EMOTION_OK) return status;

    status = append_bytes(out_body, ",\"stream\":true,\"store\":true,\"max_output_tokens\":");
    if (status != EMOTION_OK) return status;

    token_len = snprintf(token_buffer, sizeof(token_buffer), "%u", config->max_output_tokens);
    if (token_len <= 0 || (size_t)token_len >= sizeof(token_buffer)) return EMOTION_ERR_STATE;
    status = emotion_buffer_append(out_body, token_buffer, (size_t)token_len);
    if (status != EMOTION_OK) return status;

    if (config->instructions != NULL && config->instructions[0] != '\0') {
        status = append_bytes(out_body, ",\"instructions\":");
        if (status != EMOTION_OK) return status;
        status = append_json_string(out_body, emotion_sv_from_cstr(config->instructions));
        if (status != EMOTION_OK) return status;
    }

    if (session->previous_response_id[0] != '\0') {
        status = append_bytes(out_body, ",\"previous_response_id\":");
        if (status != EMOTION_OK) return status;
        status = append_json_string(out_body,
                                    emotion_sv_from_cstr(session->previous_response_id));
        if (status != EMOTION_OK) return status;
    }

    return emotion_buffer_append_byte(out_body, '}');
}
