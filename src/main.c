#include "emotion_chat.h"

#include "emotion/ui.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROMPT_CAP 4096u
#define DEFAULT_ENDPOINT "https://api.openai.com/v1/responses"
#define DEFAULT_MODEL "gpt-6-luna"
#define DEFAULT_MAX_OUTPUT_TOKENS 1024u
#define DEFAULT_MAX_RESPONSE_BYTES (1024u * 1024u)
#define DEFAULT_MEMORY_BUDGET (8u * 1024u * 1024u)

static const char *env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : fallback;
}

static unsigned parse_token_limit(void) {
    const char *text = getenv("EMOTION_CHAT_MAX_OUTPUT_TOKENS");
    char *end = NULL;
    unsigned long value;

    if (text == NULL || text[0] == '\0') return DEFAULT_MAX_OUTPUT_TOKENS;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 16ul || value > 8192ul)
        return DEFAULT_MAX_OUTPUT_TOKENS;
    return (unsigned)value;
}

static void print_label(EmotionUi *ui, const char *label, EmotionUiColor color) {
    emotion_ui_set_style(ui, color, EMOTION_UI_COLOR_DEFAULT, true, false, false);
    fputs(label, ui->out);
    emotion_ui_reset_style(ui);
    emotion_ui_flush(ui);
}

static void print_help(void) {
    puts("Commands:");
    puts("  /help  show this help");
    puts("  /new   start a new response chain");
    puts("  /quit  exit Emotion Chat");
}

static void discard_line_remainder(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

static bool read_prompt(char *buffer, size_t capacity) {
    size_t len;
    if (fgets(buffer, (int)capacity, stdin) == NULL) return false;
    len = strlen(buffer);
    if (len != 0u && buffer[len - 1u] == '\n') {
        buffer[--len] = '\0';
        if (len != 0u && buffer[len - 1u] == '\r') buffer[len - 1u] = '\0';
    } else if (!feof(stdin)) {
        discard_line_remainder();
        buffer[0] = '\0';
        fputs("prompt too long; maximum is 4095 bytes\n", stderr);
    }
    return true;
}

static bool join_arguments(int argc, char **argv, char *out, size_t capacity) {
    size_t used = 0u;
    int i;

    if (argc < 2 || capacity == 0u) return false;
    out[0] = '\0';
    for (i = 1; i < argc; ++i) {
        size_t len = strlen(argv[i]);
        size_t extra = len + (i > 1 ? 1u : 0u);
        if (extra > capacity - 1u - used) return false;
        if (i > 1) out[used++] = ' ';
        memcpy(out + used, argv[i], len);
        used += len;
        out[used] = '\0';
    }
    return used != 0u;
}

static int run_prompt(const EmotionChatConfig *config,
                      EmotionChatSession *session,
                      EmotionUi *ui,
                      const char *prompt) {
    char error[256];
    EmotionStatus status;

    print_label(ui, "assistant> ", EMOTION_UI_COLOR_GREEN);
    status = emotion_chat_stream(config, session, emotion_sv_from_cstr(prompt),
                                 stdout, error, sizeof(error));
    fputc('\n', stdout);
    if (status != EMOTION_OK) {
        fprintf(stderr, "emotion-chat: %s%s%s\n",
                emotion_status_string(status),
                error[0] != '\0' ? ": " : "",
                error);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    EmotionChatConfig config;
    EmotionChatSession session = {{0}};
    EmotionUi ui;
    char prompt[PROMPT_CAP];
    const char *api_key = getenv("OPENAI_API_KEY");

    if (api_key == NULL || api_key[0] == '\0') {
        fputs("emotion-chat: OPENAI_API_KEY is not set\n", stderr);
        return 2;
    }

    emotion_memory_reset();
    emotion_memory_set_budget(DEFAULT_MEMORY_BUDGET);
    emotion_ui_init(&ui, stdout);

    config.endpoint = env_or_default("EMOTION_CHAT_ENDPOINT", DEFAULT_ENDPOINT);
    config.model = env_or_default("EMOTION_CHAT_MODEL", DEFAULT_MODEL);
    config.api_key = api_key;
    config.instructions = env_or_default(
        "EMOTION_CHAT_INSTRUCTIONS",
        "Be concise and helpful. You are responding through a low-memory terminal client.");
    config.max_output_tokens = parse_token_limit();
    config.max_response_bytes = DEFAULT_MAX_RESPONSE_BYTES;

    if (argc > 1) {
        if (!join_arguments(argc, argv, prompt, sizeof(prompt))) {
            fputs("emotion-chat: prompt is empty or too long\n", stderr);
            return 2;
        }
        return run_prompt(&config, &session, &ui, prompt);
    }

    emotion_ui_set_style(&ui, EMOTION_UI_COLOR_CYAN, EMOTION_UI_COLOR_DEFAULT,
                         true, false, false);
    fputs("Emotion Chat", stdout);
    emotion_ui_reset_style(&ui);
    printf("  model=%s  max_output_tokens=%u\n", config.model, config.max_output_tokens);
    puts("Type /help for commands. Ctrl-D or /quit exits.\n");

    for (;;) {
        print_label(&ui, "you> ", EMOTION_UI_COLOR_CYAN);
        if (!read_prompt(prompt, sizeof(prompt))) {
            fputc('\n', stdout);
            break;
        }
        if (prompt[0] == '\0') continue;
        if (strcmp(prompt, "/quit") == 0 || strcmp(prompt, "/exit") == 0) break;
        if (strcmp(prompt, "/help") == 0) {
            print_help();
            continue;
        }
        if (strcmp(prompt, "/new") == 0) {
            emotion_chat_session_reset(&session);
            puts("Started a new chat.\n");
            continue;
        }
        (void)run_prompt(&config, &session, &ui, prompt);
    }

    return 0;
}
