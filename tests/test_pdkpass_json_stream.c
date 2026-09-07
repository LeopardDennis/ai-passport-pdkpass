#include "pdkpass_json_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned count;
static bool accept(const char *json, void *context)
{
    (void)context;
    assert(json[0] == '{');
    count++;
    return true;
}
static bool reject(const char *json, void *context)
{ (void)json; (void)context; return false; }

int main(void)
{
    const char *json = " [ {\"name\":\"escaped \\\" } [\",\"nested\":[{}]}, {\"second\":2} ] \n";
    char buffer[128];
    for (size_t split = 0; split <= strlen(json); split++) {
        pdkpass_json_stream_t stream;
        count = 0;
        pdkpass_json_stream_init(&stream, buffer, sizeof(buffer), accept, NULL);
        assert(pdkpass_json_stream_feed(&stream, json, split));
        assert(pdkpass_json_stream_feed(&stream, json + split, strlen(json) - split));
        assert(pdkpass_json_stream_done(&stream));
        assert(count == 2);
    }
    const char *bad[] = {"[{},]", "[{}]x", "[{}", "{}", "[1]", "[{}{}]", "[[{}]]"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        pdkpass_json_stream_t stream;
        pdkpass_json_stream_init(&stream, buffer, sizeof(buffer), accept, NULL);
        bool fed = pdkpass_json_stream_feed(&stream, bad[i], strlen(bad[i]));
        assert(!fed || !pdkpass_json_stream_done(&stream));
    }
    pdkpass_json_stream_t stream;
    pdkpass_json_stream_init(&stream, buffer, 2, accept, NULL);
    assert(!pdkpass_json_stream_feed(&stream, "[{}]", 4));
    pdkpass_json_stream_init(&stream, buffer, sizeof(buffer), reject, NULL);
    assert(!pdkpass_json_stream_feed(&stream, "[{}]", 4));
    pdkpass_json_stream_init(&stream, buffer, sizeof(buffer), accept, NULL);
    assert(pdkpass_json_stream_feed(&stream, "[]", 2));
    assert(pdkpass_json_stream_done(&stream));
    pdkpass_json_stream_init(&stream, buffer, sizeof(buffer), accept, NULL);
    const char *deep = "[{{{{{{{{{{{{{{{{{";
    assert(!pdkpass_json_stream_feed(&stream, deep, strlen(deep)));
    puts("JSON stream boundaries: PASS");
    return 0;
}
