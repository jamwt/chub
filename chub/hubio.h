#ifndef DIESEL_HUBIO_H
#define DIESEL_HUBIO_H
#include <arpa/inet.h>
#include <ev.h>
#include <pthread.h>
#include <Python.h>

#include "klib/kvec.h"
#include "klib/khash.h"
#include "buffer.h"

typedef struct hub_state_t hub_state_t;

typedef struct hub_timer_t {
    int64_t id;
    ev_timer t;
    PyObject *cb;
    hub_state_t *hs;
} hub_timer_t;

enum {
    READ_MODE_NONE,
    READ_MODE_ANY,
    READ_MODE_SENTINEL,
    READ_MODE_COUNT
};

typedef struct hub_socket_t {
    ev_io ior;
    ev_io iow;
    int fd;
    PyObject *ready_cb;
    PyObject *close_cb;
    hub_state_t *hs;
    nitro_buffer_t *buf;

    int cached_outsize;
    int buffer_consumed;
    int count;
    uint8_t sentinel[10];
    int mode;
} hub_socket_t;

KHASH_MAP_INIT_INT64(timer_hash, hub_timer_t *)
KHASH_MAP_INIT_INT(socket_hash, hub_socket_t *)

/* Callbacks */

typedef void(*pyhub_run_callable)(PyObject *hub, PyObject *cb);
typedef void(*pyhub_accept_callable)(
    PyObject *hub, PyObject *cb, int fd,
    char *host, int port);

struct hub_state_t {
    PyObject *hub;
    struct ev_loop *loop;
    struct ev_async wake;
    pyhub_run_callable run_cb;
    pyhub_accept_callable accept_cb;
    pthread_mutex_t scheduled_lock;

    kvec_t(PyObject *) scheduled;
    khash_t(timer_hash) *timers;
    khash_t(socket_hash) *sockets;
    int64_t timer_id;
};

hub_state_t *hub_state_new(PyObject *hub, 
    pyhub_run_callable run_cb,
    pyhub_accept_callable accept_cb
    );
void hub_schedule(hub_state_t *hs, PyObject *p);
void hub_run(hub_state_t *hs);
int64_t hub_start_timer(hub_state_t *hs, double t, PyObject *cb);
void hub_add_acceptor(hub_state_t *hs, int fd, PyObject *cb);
void hub_register(hub_state_t *hs, int fd, PyObject *reader, PyObject *cb);
void hub_read_set_sentinel(hub_state_t *hs, int fd, uint8_t *sentinel, int length);
void hub_read_set_any(hub_state_t *hs, int fd);
void hub_read_set_bytes(hub_state_t *hs, int fd, int bytes);
void hub_read_set_none(hub_state_t *hs, int fd);
uint8_t *hub_check(hub_state_t *hs, int fd, int *size);
void hub_reset_stream(hub_state_t *hs, int fd);

#endif /* DIESEL_HUBIO_H */
