cdef extern from "ev.h":
    ctypedef struct ev_async:
        void *data

    cdef struct ev_loop:
        pass

    ctypedef void(*ev_async_callback)(ev_loop *, ev_async *, int revents)

    ev_loop *ev_loop_new(int backend)
    void ev_async_init(ev_async *, ev_async_callback)
    void ev_async_start(ev_loop *, ev_async *)
    void ev_async_send(ev_loop *, ev_async *)


    void ev_run(ev_loop *, int flags) nogil

cdef void cb_wake_fired(ev_loop *loop, ev_async *a, int revents) with gil:
    ch = <object *>a.data
    cdef i
    cdef l = 
    print 'wake fired'

cdef class Chub(object):
    cdef ev_loop *cloop
    cdef ev_async cthread_wake

    def __init__(self):
        self.cloop = ev_loop_new(0)
        ev_async_init(&self.cthread_wake, cb_wake_fired)
        self.cthread_wake.data = <void *>self
        ev_async_start(self.cloop, &self.cthread_wake)

    def run(self):
        with nogil:
            ev_run(self.cloop, 0)

    def schedule(self):
        ev_async_send(self.cloop, &self.cthread_wake)
