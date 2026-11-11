#pragma once

// The C ABI a non-C++ host (Julia, and anything else with a C FFI) uses to serve a Paperboat
// backend. The engine stays in C++: the host describes its block and station object types as
// JSON, then answers the events the engine raises while a run executes.
//
//   config  backend JSON: descriptor, port, run settings, block/object definitions
//   next    the next event to answer: {"seq", "id", "event", "payload"}
//   reply   the event's result JSON, by seq
//
// The engine runs on its own threads; `next` and `reply` carry events to and from the host's
// thread, so the host's runtime is only ever entered from that thread. A host loop is:
//
//   handle = pb_host_new(config, 0);
//   while ((event = pb_host_next(handle))) pb_host_reply(handle, event.seq, answer(event));
//   pb_host_free(handle);

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pb_host pb_host;

// Serve `config` (run_only = 0) or run its starter graph once, headless (run_only = 1).
// NULL on failure; `pb_host_last_error` says why.
pb_host* pb_host_new(const char* config, int run_only);

// The next event, as JSON, valid until the next call; NULL when the host is done.
const char* pb_host_next(pb_host* host);

// Answer event `seq` with a JSON object. Non-zero when the event is gone (a timeout).
int pb_host_reply(pb_host* host, long long seq, const char* reply);

// Stop the server or run and wake every waiting event.
void pb_host_stop(pb_host* host);

// Stop (if needed) and free the host. Every event must have been answered.
void pb_host_free(pb_host* host);

// Why the last pb_host_new failed; "" when it didn't.
const char* pb_host_last_error(void);

#ifdef __cplusplus
}
#endif
