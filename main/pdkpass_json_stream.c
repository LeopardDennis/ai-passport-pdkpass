#include "pdkpass_json_stream.h"
#include <ctype.h>
#include <string.h>

// Only an outer JSON array of objects is accepted. Objects are delivered one
// at a time; the consumer validates their JSON before accepting any data.
void pdkpass_json_stream_init(pdkpass_json_stream_t *s, char *buffer,
                              size_t capacity, pdkpass_json_item_fn item,
                              void *context)
{
    memset(s, 0, sizeof(*s));
    s->buffer = buffer;
    s->capacity = capacity;
    s->item = item;
    s->context = context;
}

bool pdkpass_json_stream_feed(pdkpass_json_stream_t *s,
                              const char *data, size_t length)
{
    if (!s || !s->buffer || s->capacity < 2 || !s->item || s->failed) return false;
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        if (s->depth == 0) {
            if (isspace((unsigned char)c)) continue;
            if (s->state == 0 && c == '[') { s->state = 1; continue; }
            if ((s->state == 1 || s->state == 3) && c == ']') {
                s->state = 4;
                continue;
            }
            if (s->state == 3 && c == ',') { s->state = 2; continue; }
            if ((s->state != 1 && s->state != 2) || c != '{') goto fail;
            s->length = 0;
            s->quoted = false;
            s->escaped = false;
        }
        if (s->length + 1 >= s->capacity) goto fail;
        s->buffer[s->length++] = c;
        if (s->quoted) {
            if (s->escaped) s->escaped = false;
            else if (c == '\\') s->escaped = true;
            else if (c == '"') s->quoted = false;
        } else if (c == '"') s->quoted = true;
        else if (c == '{' || c == '[') {
            if (s->depth >= 16U) goto fail;
            s->depth++;
        }
        else if (c == '}' || c == ']') {
            if (--s->depth == 0) {
                s->buffer[s->length] = '\0';
                if (!s->item(s->buffer, s->context)) goto fail;
                s->state = 3;
            }
        }
    }
    return true;
fail:
    s->failed = true;
    return false;
}

bool pdkpass_json_stream_done(const pdkpass_json_stream_t *s)
{
    return s && !s->failed && s->state == 4 && s->depth == 0;
}
