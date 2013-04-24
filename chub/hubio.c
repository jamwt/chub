#include "hubio.h"
#include <string.h>

extern void *memmem(const void *haystack, size_t haystacklen,
                    const void *needle, size_t needlelen);

void hub_thread_cb(struct ev_loop *loop, ev_async *a, int events) {
    hub_state_t *hs = (hub_state_t *)a->data;
    pthread_mutex_lock(&hs->scheduled_lock);
    kvec_t(PyObject *) jobs;
    jobs.n = hs->scheduled.n;
    jobs.m = hs->scheduled.m;
    jobs.a = hs->scheduled.a;
    kv_init(hs->scheduled);
    hs->timers = kh_init(timer_hash);
    hs->sockets = kh_init(socket_hash);
    pthread_mutex_unlock(&hs->scheduled_lock);

    int i;
    for (i=0; i < kv_size(jobs); i++) {
        PyObject *j = kv_A(jobs, i);
        hs->run_cb(hs->hub, j);
    }
    /* decref in GIL */
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    for (i=0; i < kv_size(jobs); i++) {
        PyObject *j = kv_A(jobs, i);
        Py_DECREF(j);
    }
    PyGILState_Release(gstate);
    kv_destroy(jobs);
}

hub_state_t *hub_state_new(PyObject *hub, 
    pyhub_run_callable run_cb,
    pyhub_accept_callable accept_cb) {
    hub_state_t *hs = calloc(1, sizeof(hub_state_t));
    hs->hub = hub;
    hs->run_cb = run_cb;
    hs->accept_cb = accept_cb;
    kv_init(hs->scheduled);

    hs->loop = ev_loop_new(0);

    ev_async_init(&hs->wake, hub_thread_cb);
    hs->wake.data = hs;
    ev_async_start(hs->loop, &hs->wake);

    pthread_mutex_init(&hs->scheduled_lock, NULL);

    return hs;
}

void hub_run(hub_state_t *hs) {
    ev_loop(hs->loop, 0);
}

void hub_schedule(hub_state_t *hs, PyObject *p) {
    /* Note -- could be run on _not_ diesel thread */
    pthread_mutex_lock(&hs->scheduled_lock);
    Py_INCREF(p); // should be okay, we have GIL
    kv_push(PyObject *, hs->scheduled, p);
    pthread_mutex_unlock(&hs->scheduled_lock);
    ev_async_send(hs->loop, &hs->wake);
}

/* Timers */
void hub_timer_cb(
    struct ev_loop *loop,
    ev_timer *t,
    int revents) {

    hub_timer_t *tm = (hub_timer_t *)t->data;
    hub_state_t *hs = tm->hs;
    hs->run_cb(hs->hub, tm->cb);

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    Py_DECREF(tm->cb);
    PyGILState_Release(gstate);

    khiter_t k = kh_get(timer_hash, hs->timers, tm->id);
    assert(k != kh_end(hs->timers));
    kh_del(timer_hash, hs->timers, k);

    free(tm);
}

/* note - timer activity must be called on diesel thread */
int64_t hub_start_timer(hub_state_t *hs, double t, PyObject *cb) {
    hub_timer_t *tm = malloc(sizeof(hub_timer_t));
    ev_timer_init(&tm->t,
        hub_timer_cb,
        t, 0);
    ev_timer_start(hs->loop, &tm->t);
    tm->cb = cb;
    tm->hs = hs;
    tm->id = hs->timer_id++;
    tm->t.data = tm;

    int ret;
    khiter_t k = kh_put(timer_hash, hs->timers, tm->id, &ret);
    kh_val(hs->timers, k) = tm;
    Py_INCREF(tm->cb);

    return tm->id;
}

/* Accept work */

typedef struct hub_acceptor_t {
    hub_state_t *hs;
    PyObject *cb;
} hub_acceptor_t;

void hub_accept_cb(struct ev_loop *loop,
    ev_io *io, int revents) {

    hub_acceptor_t *a = (hub_acceptor_t *)io->data;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(struct sockaddr_in);

    int r = accept(io->fd, (struct sockaddr *)&addr, &addrlen);

    char adds[INET6_ADDRSTRLEN] = {0};
    inet_ntop(addr.sin_family,
        &addr.sin_addr,
        adds,INET6_ADDRSTRLEN);

    if (r>=0) {
        a->hs->accept_cb(
            a->hs->hub, a->cb, r,
            adds,
            addr.sin_port);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        assert(0); // bad errno!
    }
}

void hub_add_acceptor(hub_state_t *hs, int fd, PyObject *cb) {
    ev_io *accept_io = malloc(sizeof(ev_io));

    hub_acceptor_t *a = calloc(1, sizeof(hub_acceptor_t));
    a->hs = hs;
    a->cb = cb;
    Py_INCREF(cb);
    ev_io_init(accept_io,
    hub_accept_cb, fd, EV_READ);
    accept_io->data = a;

    ev_io_start(hs->loop, accept_io);
}

/* Socket (stream) work */

#define READSIZ (64 * 1024)

void hub_buf_reset(hub_socket_t *sock) {
    if (sock->buffer_consumed > READSIZ) {
        nitro_buffer_t *n = nitro_buffer_new();

        int delt = sock->buf->size - sock->buffer_consumed;
        if (delt) {
            nitro_buffer_append(n,
                sock->buf->area + sock->buffer_consumed,
                delt);
        }
        nitro_buffer_destroy(sock->buf);
        sock->buf = n;
    }
}

void hub_reset_stream(hub_state_t *hs, int fd) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));

    hub_socket_t *sock = kh_val(hs->sockets, k);

    hub_buf_reset(sock);
}

void hub_read_set_sentinel(hub_state_t *hs, int fd,
    uint8_t *sentinel, int length) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));

    hub_socket_t *sock = kh_val(hs->sockets, k);

    sock->mode = READ_MODE_SENTINEL;
    assert(length < 10);
    memcpy(sock->sentinel, sentinel, length);
    sock->count = length;
}

void hub_read_set_any(hub_state_t *hs, int fd) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));

    hub_socket_t *sock = kh_val(hs->sockets, k);

    sock->mode = READ_MODE_ANY;
}

void hub_read_set_bytes(hub_state_t *hs, int fd, int bytes) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));

    hub_socket_t *sock = kh_val(hs->sockets, k);

    sock->mode = READ_MODE_COUNT;
    sock->count = bytes;
}

void hub_read_set_none(hub_state_t *hs, int fd) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));

    hub_socket_t *sock = kh_val(hs->sockets, k);

    sock->mode = READ_MODE_NONE;
}

inline uint8_t *hub_check_read(hub_socket_t *sock,
    int *size, int consume) {

    uint8_t *out = NULL;
    int wholesize = 0, outsize = 0;
    char *ptr = nitro_buffer_data(sock->buf, &wholesize);
    ptr += sock->buffer_consumed;
    wholesize -= sock->buffer_consumed;
    if (sock->cached_outsize) {
        outsize = sock->cached_outsize;
        sock->cached_outsize = 0;
    } else if (sock->mode == READ_MODE_ANY) {
        outsize = wholesize;
    } else if (sock->mode == READ_MODE_COUNT) {
        if (wholesize >= sock->count) {
            outsize = sock->count;
        }
    } else if (sock->mode == READ_MODE_SENTINEL) {
        char *found = memmem(ptr, wholesize,
        sock->sentinel, sock->count);
        if (found) {
            found += sock->count;
            outsize = found - ptr;
        }
    }

    *size = outsize;
    if (outsize) {
        out = (uint8_t *)ptr;
        if (consume) {
            sock->buffer_consumed += outsize;
        }
    }
    return out;
}

uint8_t *hub_check(hub_state_t *hs, int fd, int *size) {
    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k != kh_end(hs->sockets));
    hub_socket_t *sock = kh_val(hs->sockets, k);

    return hub_check_read(sock, size, 1);
}

static void hub_remove_sock(hub_state_t *hs, hub_socket_t *sock);

void hub_stream_read_cb(struct ev_loop *loop,
    ev_io *io, int revents) {

    hub_socket_t *sock = (hub_socket_t *)io->data;
    hub_state_t *hs = sock->hs;

    int proposal = READSIZ;
    char *target = nitro_buffer_prepare(sock->buf, &proposal);
    int bytes = read(io->fd, target, proposal);

    if (bytes > 0) {
        nitro_buffer_extend(sock->buf, bytes);
        if (hub_check_read(sock, &sock->cached_outsize, 0)) {
            hs->run_cb(hs->hub, sock->ready_cb);
        }
    } else if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }
        perror("unusual read:");
        assert(0);
    } else {
        /* handle close */
        hs->run_cb(hs->hub, sock->close_cb);
        hub_remove_sock(hs, sock);
    }
}

static void hub_remove_sock(hub_state_t *hs, hub_socket_t *sock) {
    /* Stop listeners, remove from hash, destroy */
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
    Py_DECREF(sock->close_cb);
    Py_DECREF(sock->ready_cb);
    PyGILState_Release(gstate);
    ev_io_stop(hs->loop, &sock->ior);
    ev_io_stop(hs->loop, &sock->iow);
    nitro_buffer_destroy(sock->buf);

    khiter_t k = kh_get(socket_hash, hs->sockets, sock->fd);
    assert(k != kh_end(hs->sockets));
    kh_del(socket_hash, hs->sockets, k);

    close(sock->fd);
    free(sock);
}

void hub_register(hub_state_t *hs, int fd,
    PyObject *reader, PyObject *cb) {
    hub_socket_t *sock = calloc(1, sizeof(hub_socket_t));
    sock->fd = fd;
    sock->close_cb = cb;
    Py_INCREF(cb);
    sock->ready_cb = reader;
    Py_INCREF(reader);
    sock->hs = hs;
    sock->buf = nitro_buffer_new();

    ev_io_init(&sock->ior,
        hub_stream_read_cb,
        sock->fd,
        EV_READ);
    sock->ior.data = sock;
    ev_io_start(hs->loop, &sock->ior);

    khiter_t k = kh_get(socket_hash, hs->sockets, fd);
    assert(k == kh_end(hs->sockets));

    int ret;
    k = kh_put(socket_hash, hs->sockets, fd, &ret);
    kh_val(hs->sockets, k) = sock;
}
