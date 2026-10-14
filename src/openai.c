#include "emotion_chat.h"

#include "emotion/auth.h"
#include "emotion/data.h"
#include "emotion/http.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef EmotionStatus (*FieldSink)(EmotionStringView value, void *userdata);

typedef struct JsonFieldContext {
    const char *target;
    bool capture_next_string;
    bool found;
    FieldSink sink;
    void *userdata;
} JsonFieldContext;

typedef struct StreamContext {
    FILE *output;
    char pending_response_id[EMOTION_CHAT_RESPONSE_ID_CAP];
    char diagnostic[256];
    bool completed;
    bool failed;
} StreamContext;

static bool sv_equal_cstr(EmotionStringView value, const char *text) {
    size_t len = strlen(text);
    return value.len == len && memcmp(value.data, text, len) == 0;
}

static void set_error(char *out, size_t out_size, const char *message) {
    if (out == NULL || out_size == 0u) return;
    if (message == NULL) message = "unknown error";
    (void)snprintf(out, out_size, "%s", message);
}

static EmotionStatus field_key(EmotionStringView key, void *userdata) {
    JsonFieldContext *ctx = (JsonFieldContext *)userdata;
    ctx->capture_next_string = sv_equal_cstr(key, ctx->target);
    return EMOTION_OK;
}

static EmotionStatus field_string(EmotionStringView value, void *userdata) {
    JsonFieldContext *ctx = (JsonFieldContext *)userdata;
    EmotionStatus status = EMOTION_OK;
    if (ctx->capture_next_string && !ctx->found) {
        status = ctx->sink(value, ctx->userdata);
        if (status == EMOTION_OK) ctx->found = true;
    }
    ctx->capture_next_string = false;
    return status;
}

static EmotionStatus parse_string_field(EmotionStringView json,
                                        const char *field,
                                        FieldSink sink,
                                        void *userdata) {
    EmotionJsonCallbacks callbacks = {0};
    EmotionJsonParser *parser;
    JsonFieldContext context;
    EmotionStatus status;

    callbacks.key = field_key;
    callbacks.string = field_string;
    context.target = field;
    context.capture_next_string = false;
    context.found = false;
    context.sink = sink;
    context.userdata = userdata;

    parser = emotion_json_parser_create(&callbacks, &context);
    if (parser == NULL) return EMOTION_ERR_OUT_OF_MEMORY;
    emotion_json_parser_set_max_depth(parser, 64u);
    emotion_json_parser_set_token_limit(parser, 64u * 1024u);

    status = emotion_json_feed(parser, (const uint8_t *)json.data, json.len);
    if (status == EMOTION_OK) status = emotion_json_finish(parser);
    emotion_json_parser_destroy(parser);

    if (status != EMOTION_OK) return status;
    return context.found ? EMOTION_OK : EMOTION_ERR_NOT_FOUND;
}

typedef struct CopySinkContext {
    char *out;
    size_t capacity;
} CopySinkContext;

static EmotionStatus copy_sink(EmotionStringView value, void *userdata) {
    CopySinkContext *ctx = (CopySinkContext *)userdata;
    if (ctx->capacity == 0u || value.len >= ctx->capacity) return EMOTION_ERR_LIMIT;
    memcpy(ctx->out, value.data, value.len);
    ctx->out[value.len] = '\0';
    return EMOTION_OK;
}

static EmotionStatus output_sink(EmotionStringView value, void *userdata) {
    FILE *output = (FILE *)userdata;
    if (value.len == 0u) return EMOTION_OK;
    if (fwrite(value.data, 1u, value.len, output) != value.len) return EMOTION_ERR_IO;
    fflush(output);
    return EMOTION_OK;
}

static EmotionStatus capture_field(EmotionStringView json,
                                   const char *field,
                                   char *out,
                                   size_t capacity) {
    CopySinkContext context = {out, capacity};
    return parse_string_field(json, field, copy_sink, &context);
}

static EmotionStatus on_sse_event(const EmotionSseEvent *event, void *userdata) {
    StreamContext *ctx = (StreamContext *)userdata;
    char event_type[64] = {0};
    EmotionStatus status;

    if (event->event.len != 0u) {
        if (event->event.len >= sizeof(event_type)) return EMOTION_ERR_LIMIT;
        memcpy(event_type, event->event.data, event->event.len);
        event_type[event->event.len] = '\0';
    } else {
        status = capture_field(event->data, "type", event_type, sizeof(event_type));
        if (status != EMOTION_OK) return status;
    }

    if (strcmp(event_type, "response.created") == 0) {
        status = capture_field(event->data, "id", ctx->pending_response_id,
                               sizeof(ctx->pending_response_id));
        return status == EMOTION_ERR_NOT_FOUND ? EMOTION_OK : status;
    }

    if (strcmp(event_type, "response.output_text.delta") == 0 ||
        strcmp(event_type, "response.refusal.delta") == 0) {
        status = parse_string_field(event->data, "delta", output_sink, ctx->output);
        return status == EMOTION_ERR_NOT_FOUND ? EMOTION_OK : status;
    }

    if (strcmp(event_type, "response.completed") == 0) {
        ctx->completed = true;
        return EMOTION_OK;
    }

    if (strcmp(event_type, "response.failed") == 0 ||
        strcmp(event_type, "response.incomplete") == 0 ||
        strcmp(event_type, "error") == 0) {
        ctx->failed = true;
        status = capture_field(event->data, "message", ctx->diagnostic,
                               sizeof(ctx->diagnostic));
        if (status == EMOTION_ERR_NOT_FOUND) {
            (void)snprintf(ctx->diagnostic, sizeof(ctx->diagnostic), "%s", event_type);
            return EMOTION_OK;
        }
        return status;
    }

    return EMOTION_OK;
}

static EmotionStatus on_http_body(const uint8_t *data, size_t len, void *userdata) {
    return emotion_sse_feed((EmotionSseParser *)userdata, data, len);
}

EmotionStatus emotion_chat_stream(const EmotionChatConfig *config,
                                  EmotionChatSession *session,
                                  EmotionStringView prompt,
                                  FILE *output,
                                  char *error_out,
                                  size_t error_out_size) {
    EmotionBuffer body;
    EmotionBuffer auth_value;
    EmotionHttpHeader headers[3];
    EmotionHttpRequest request;
    EmotionHttpResponse response = {0};
    EmotionSseParser sse;
    StreamContext stream = {0};
    EmotionStatus status;

    if (config == NULL || session == NULL || config->endpoint == NULL ||
        config->api_key == NULL || config->api_key[0] == '\0' || output == NULL) {
        set_error(error_out, error_out_size, "invalid chat configuration");
        return EMOTION_ERR_INVALID_ARGUMENT;
    }

    emotion_buffer_init(&body, EMOTION_MEMORY_PROTOCOL);
    emotion_buffer_init(&auth_value, EMOTION_MEMORY_AUTH);

    status = emotion_chat_build_request(config, session, prompt, &body);
    if (status != EMOTION_OK) {
        set_error(error_out, error_out_size, emotion_status_string(status));
        goto cleanup;
    }

    status = emotion_auth_bearer_value(emotion_sv_from_cstr(config->api_key), &auth_value);
    if (status == EMOTION_OK) status = emotion_buffer_append_byte(&auth_value, 0u);
    if (status != EMOTION_OK) {
        set_error(error_out, error_out_size, emotion_status_string(status));
        goto cleanup;
    }

    headers[0].name = "Authorization";
    headers[0].value = (const char *)auth_value.data;
    headers[1].name = "Content-Type";
    headers[1].value = "application/json";
    headers[2].name = "Accept";
    headers[2].value = "text/event-stream";

    stream.output = output;
    emotion_sse_parser_init(&sse, on_sse_event, &stream);
    emotion_sse_parser_set_line_limit(&sse, 128u * 1024u);

    emotion_http_request_init(&request);
    request.method = "POST";
    request.url = config->endpoint;
    request.headers = headers;
    request.header_count = 3u;
    request.body = body.data;
    request.body_len = body.len;
    request.max_header_bytes = 16u * 1024u;
    request.max_body_bytes = config->max_response_bytes;
    request.max_redirects = 2u;
    request.connect_timeout_ms = 10000;
    request.io_timeout_ms = 120000;
    request.on_body = on_http_body;
    request.userdata = &sse;

    status = emotion_http_execute(&request, &response);
    if (status == EMOTION_OK) status = emotion_sse_finish(&sse);
    emotion_sse_parser_destroy(&sse);

    if (status != EMOTION_OK) {
        set_error(error_out, error_out_size, emotion_status_string(status));
        goto cleanup;
    }

    if (response.status_code < 200 || response.status_code >= 300) {
        char message[64];
        (void)snprintf(message, sizeof(message), "OpenAI HTTP status %d", response.status_code);
        set_error(error_out, error_out_size, message);
        status = EMOTION_ERR_IO;
        goto cleanup;
    }

    if (stream.failed) {
        set_error(error_out, error_out_size,
                  stream.diagnostic[0] != '\0' ? stream.diagnostic : "OpenAI response failed");
        status = EMOTION_ERR_STATE;
        goto cleanup;
    }

    if (!stream.completed) {
        set_error(error_out, error_out_size, "stream ended without response.completed");
        status = EMOTION_ERR_STATE;
        goto cleanup;
    }

    if (stream.pending_response_id[0] != '\0') {
        (void)snprintf(session->previous_response_id,
                       sizeof(session->previous_response_id), "%s",
                       stream.pending_response_id);
    }

    set_error(error_out, error_out_size, "");
    status = EMOTION_OK;

cleanup:
    if (auth_value.data != NULL && auth_value.capacity != 0u)
        memset(auth_value.data, 0, auth_value.capacity);
    emotion_buffer_destroy(&auth_value);
    emotion_buffer_destroy(&body);
    return status;
}
