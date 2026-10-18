#include "services/Polkit.h"

#include "core/EventLoop.h"
#include "core/Log.h"

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include "polkit/listener.h"

#include <polkitagent/polkitagent.h>
#include <thread>

namespace m3 {

namespace {

// ── GLib thread state ──

GMainContext* g_ctx = nullptr;

struct Pending {
    int id;
    GTask* task;
    GCancellable* cancellable;
    std::string cookie, user;
    PolkitIdentity* identity = nullptr;
    PolkitAgentSession* session = nullptr;
    Polkit::Request view; // mirrored to the event loop
    gulong cancelHandler = 0;
};

std::map<int, Pending*> g_pending;
int g_nextId = 1;

void publish(const Pending& p, bool isNew = false, bool done = false) {
    Polkit::Request copy = p.view;
    EventLoop::get().post([copy, isNew, done] { Polkit::get().apply(copy, isNew, done); });
}

void finish(Pending* p, bool ok) {
    if (p->session) {
        g_signal_handlers_disconnect_by_data(p->session, p);
        g_object_unref(p->session);
        p->session = nullptr;
    }
    if (p->cancelHandler) g_cancellable_disconnect(p->cancellable, p->cancelHandler);
    if (ok)
        g_task_return_boolean(p->task, TRUE);
    else
        g_task_return_new_error(p->task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED, "Authentication dismissed");
    g_object_unref(p->task);
    if (p->identity) g_object_unref(p->identity);
    publish(*p, false, true);
    g_pending.erase(p->id);
    delete p;
}

void startSession(Pending* p);

void onRequest(PolkitAgentSession*, const gchar* request, gboolean echo, gpointer data) {
    auto* p = static_cast<Pending*>(data);
    std::string prompt = request ? request : "Password:";
    while (!prompt.empty() && (prompt.back() == ' ' || prompt.back() == ':')) prompt.pop_back();
    p->view.prompt = prompt.empty() ? "Password" : prompt;
    p->view.echo = echo;
    p->view.busy = false;
    publish(*p);
}

void onShowError(PolkitAgentSession*, const gchar* text, gpointer data) {
    auto* p = static_cast<Pending*>(data);
    p->view.error = text ? text : "";
    publish(*p);
}

void onShowInfo(PolkitAgentSession*, const gchar* text, gpointer data) {
    auto* p = static_cast<Pending*>(data);
    p->view.info = text ? text : "";
    publish(*p);
}

void onCompleted(PolkitAgentSession*, gboolean gained, gpointer data) {
    auto* p = static_cast<Pending*>(data);
    if (gained) return finish(p, true);
    // Wrong password: a fresh session for another try
    p->view.error = "That didn't work. Try again.";
    p->view.busy = false;
    g_signal_handlers_disconnect_by_data(p->session, p);
    g_object_unref(p->session);
    p->session = nullptr;
    startSession(p);
}

void startSession(Pending* p) {
    p->session = polkit_agent_session_new(p->identity, p->cookie.c_str());
    g_signal_connect(p->session, "request", G_CALLBACK(onRequest), p);
    g_signal_connect(p->session, "show-error", G_CALLBACK(onShowError), p);
    g_signal_connect(p->session, "show-info", G_CALLBACK(onShowInfo), p);
    g_signal_connect(p->session, "completed", G_CALLBACK(onCompleted), p);
    polkit_agent_session_initiate(p->session);
    publish(*p);
}

void onCancelled(GCancellable*, gpointer data) {
    // polkitd withdrew the request (the caller gave up)
    auto* p = static_cast<Pending*>(data);
    g_main_context_invoke(g_ctx, [](gpointer d) -> gboolean {
        const int id = GPOINTER_TO_INT(d);
        if (auto it = g_pending.find(id); it != g_pending.end()) {
            if (it->second->session) polkit_agent_session_cancel(it->second->session);
            finish(it->second, false);
        }
        return G_SOURCE_REMOVE;
    }, GINT_TO_POINTER(p->id));
}

void begin(GTask* task, const char* action, const char* message, const char* cookie, GPtrArray* users,
           GCancellable* cancellable) {
    auto* p = new Pending{g_nextId++, task, cancellable, cookie ? cookie : "", {}, nullptr, nullptr, {}, 0};
    // Prefer authenticating as ourselves when we are among the identities
    const uid_t me = getuid();
    for (guint i = 0; i < users->len; ++i) {
        auto* u = POLKIT_UNIX_USER(g_ptr_array_index(users, i));
        if (!p->identity || static_cast<uid_t>(polkit_unix_user_get_uid(u)) == me) {
            if (p->identity) g_object_unref(p->identity);
            p->identity = POLKIT_IDENTITY(g_object_ref(u));
            const char* name = polkit_unix_user_get_name(u);
            p->user = name ? name : std::to_string(polkit_unix_user_get_uid(u));
        }
    }
    g_ptr_array_unref(users);
    p->view.id = p->id;
    p->view.action = action ? action : "";
    p->view.message = message ? message : "";
    p->view.user = p->user;
    g_pending[p->id] = p;
    if (!p->identity) return finish(p, false);
    if (cancellable) p->cancelHandler = g_cancellable_connect(cancellable, G_CALLBACK(onCancelled), p, nullptr);
    publish(*p, true);
    startSession(p);
}

struct Answer {
    int id;
    std::string text;
    bool cancel;
};

gboolean deliver(gpointer data) {
    std::unique_ptr<Answer> a(static_cast<Answer*>(data));
    auto it = g_pending.find(a->id);
    if (it == g_pending.end()) return G_SOURCE_REMOVE;
    Pending* p = it->second;
    if (a->cancel) {
        if (p->session) polkit_agent_session_cancel(p->session);
        finish(p, false);
    } else if (p->session) {
        p->view.busy = true;
        p->view.error.clear();
        publish(*p);
        polkit_agent_session_response(p->session, a->text.c_str());
    }
    std::fill(a->text.begin(), a->text.end(), '\0');
    return G_SOURCE_REMOVE;
}

} // namespace

Polkit& Polkit::get() {
    static Polkit instance;
    return instance;
}

void Polkit::start() {
    if (m_started) return;
    m_started = true;
    std::thread([] {
        g_ctx = g_main_context_new();
        g_main_context_push_thread_default(g_ctx);
        char* error = nullptr;
        if (!m3_polkit_register(begin, &error)) {
            const std::string why = error ? error : "";
            g_free(error);
            EventLoop::get().post([why] { log::warn("polkit agent not registered: {}", why); });
            return;
        }
        EventLoop::get().post([] { log::info("polkit: authentication agent registered"); });
        GMainLoop* loop = g_main_loop_new(g_ctx, FALSE);
        g_main_loop_run(loop);
    }).detach();
}

const Polkit::Request* Polkit::find(int id) const {
    auto it = m_requests.find(id);
    return it != m_requests.end() ? &it->second : nullptr;
}

void Polkit::apply(const Request& r, bool isNew, bool done) {
    if (done) {
        m_requests.erase(r.id);
        finished.emit(r.id);
        return;
    }
    m_requests[r.id] = r;
    if (isNew)
        requested.emit(r.id);
    else
        updated.emit(r.id);
}

void Polkit::respond(int id, const std::string& answer) {
    if (g_ctx) g_main_context_invoke(g_ctx, deliver, new Answer{id, answer, false});
}

void Polkit::cancel(int id) {
    if (g_ctx) g_main_context_invoke(g_ctx, deliver, new Answer{id, {}, true});
}

} // namespace m3
