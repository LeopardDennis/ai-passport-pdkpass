#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef bool (*pdkpass_json_item_fn)(const char *json, void *context);
typedef struct {
    char *buffer;
    size_t capacity, length;
    unsigned depth;
    unsigned state;
    bool quoted, escaped, failed;
    pdkpass_json_item_fn item;
    void *context;
} pdkpass_json_stream_t;

void pdkpass_json_stream_init(pdkpass_json_stream_t *stream, char *buffer,
                              size_t capacity, pdkpass_json_item_fn item,
                              void *context);
bool pdkpass_json_stream_feed(pdkpass_json_stream_t *stream,
                              const char *data, size_t length);
bool pdkpass_json_stream_done(const pdkpass_json_stream_t *stream);
