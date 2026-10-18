/*
 * A PolkitAgentListener subclass in C (GObject boilerplate), forwarding
 * authentication requests to C++ through the callbacks in listener.h.
 */
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include "polkit/listener.h"

#include <polkitagent/polkitagent.h>

typedef struct {
    PolkitAgentListener parent;
} M3Listener;

typedef struct {
    PolkitAgentListenerClass parent_class;
} M3ListenerClass;

G_DEFINE_TYPE(M3Listener, m3_listener, POLKIT_AGENT_TYPE_LISTENER)

static m3_polkit_begin_fn g_begin;

static void initiate(PolkitAgentListener* listener, const gchar* action_id, const gchar* message, const gchar* icon_name,
                     PolkitDetails* details, const gchar* cookie, GList* identities, GCancellable* cancellable,
                     GAsyncReadyCallback callback, gpointer user_data) {
    (void)details;
    (void)icon_name;
    GTask* task = g_task_new(listener, cancellable, callback, user_data);
    /* Identities the user may authenticate as: unix users only */
    GPtrArray* users = g_ptr_array_new_with_free_func(g_object_unref);
    for (GList* l = identities; l; l = l->next)
        if (POLKIT_IS_UNIX_USER(l->data)) g_ptr_array_add(users, g_object_ref(l->data));
    g_begin(task, action_id, message, cookie, users, cancellable);
}

static gboolean initiate_finish(PolkitAgentListener* listener, GAsyncResult* res, GError** error) {
    (void)listener;
    return g_task_propagate_boolean(G_TASK(res), error);
}

static void m3_listener_init(M3Listener* self) {
    (void)self;
}

static void m3_listener_class_init(M3ListenerClass* klass) {
    PolkitAgentListenerClass* l = POLKIT_AGENT_LISTENER_CLASS(klass);
    l->initiate_authentication = initiate;
    l->initiate_authentication_finish = initiate_finish;
}

void* m3_polkit_register(m3_polkit_begin_fn begin, char** error_out) {
    g_begin = begin;
    GError* error = NULL;
    PolkitSubject* subject = polkit_unix_session_new_for_process_sync(getpid(), NULL, &error);
    if (!subject) {
        /* No logind session (tests, odd setups): register for this process */
        g_clear_error(&error);
        subject = polkit_unix_process_new_for_owner(getpid(), 0, getuid());
    }
    PolkitAgentListener* listener = g_object_new(m3_listener_get_type(), NULL);
    gpointer handle = polkit_agent_listener_register(listener, POLKIT_AGENT_REGISTER_FLAGS_NONE, subject,
                                                     "/dev/material3ui/PolicyKit1/AuthenticationAgent", NULL, &error);
    g_object_unref(subject);
    if (!handle) {
        if (error_out) *error_out = g_strdup(error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(listener);
        return NULL;
    }
    return listener;
}
