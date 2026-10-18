#pragma once

#include <gio/gio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called on the GLib thread for each request. `users` holds PolkitUnixUser
 * references (free with g_ptr_array_unref). Complete `task` with
 * g_task_return_boolean / g_task_return_new_error. */
typedef void (*m3_polkit_begin_fn)(GTask* task, const char* action_id, const char* message, const char* cookie,
                                   GPtrArray* users, GCancellable* cancellable);

/* Registers the agent for this session. Returns NULL (and an error string
 * to g_free) on failure, e.g. when another agent is registered. */
void* m3_polkit_register(m3_polkit_begin_fn begin, char** error_out);

#ifdef __cplusplus
}
#endif
