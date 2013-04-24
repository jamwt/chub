import socket

ctypedef long long int int64_t
ctypedef unsigned char uint8_t
cdef extern from "hubio.h":

    ctypedef struct hub_state_t:
        pass

    ctypedef void(*pyhub_run_callable)(object hub, object cb)
    ctypedef void(*pyhub_accept_callable)(
        object hub, object cb, int fd,
        char *host, int port)

    hub_state_t *hub_state_new(object, pyhub_run_callable, pyhub_accept_callable)
    void hub_run(hub_state_t *hs) nogil
    void hub_schedule(hub_state_t *hs, object p)
    int64_t hub_start_timer(hub_state_t *hs, double t, object cb)
    void hub_add_acceptor(hub_state_t *hs, int fd, object cb)
    void hub_register(hub_state_t *hs, int fd, object reader, object cb)
    void hub_read_set_sentinel(hub_state_t *hs, int fd, uint8_t *sentinel, int length)
    uint8_t *hub_check(hub_state_t *hs, int fd, int *size)
    void hub_reset_stream(hub_state_t *hs, int fd)
    void hub_read_set_any(hub_state_t *hs, int fd)
    void hub_read_set_bytes(hub_state_t *hs, int fd, int bytes)
    void hub_read_set_none(hub_state_t *hs, int fd)

cdef void call_object(object hub, object cb) with gil:
    # XXX handle error
    cb()
    
cdef void accept_fd(object hub, object cb, int fd, char *s, int port) with gil:
    new_sock = socket.fromfd(fd, socket.AF_INET, socket.SOCK_STREAM)
    # XXX handle error
    cb(new_sock, s, port)

cdef class Chub(object):
    cdef hub_state_t *hs

    def __init__(self):
        self.hs = hub_state_new(self, call_object, accept_fd)

    def run(self):
        with nogil:
            hub_run(self.hs)

    def schedule(self, cb):
        hub_schedule(self.hs, cb)

    def timer(self, d, cb):
        cdef int64_t bi
        bi = hub_start_timer(self.hs, d, cb)
        return long(bi)

    def do_connect(self, fd, callback):
        pass

    def do_accepts(self, s, on_accept):
        hub_add_acceptor(self.hs, s.fileno(), on_accept)

    def register(self, s, on_ready, on_close):
        hub_register(self.hs, s.fileno(), on_ready, on_close)

    def check(self, s):
        cdef int osize
        cdef uint8_t *r
        fd = s.fileno()
        r = hub_check(self.hs, fd, &osize)

        if r == NULL:
            hub_reset_stream(self.hs, fd)
        else:
            return r[:osize]

    def read_until_sentinel(self, s, sentinel):
        if type(sentinel) is unicode:
            sentinel = sentinel.encode('utf-8')
        hub_read_set_sentinel(self.hs, s.fileno(), sentinel, 
            len(sentinel))
        return self.check(s)

    def read_anything(self, s):
        hub_read_set_any(self.hs, s.fileno())
        return self.check(s)

    def read_bytes(self, s, count):
        hub_read_set_bytes(self.hs, s.fileno(), count)
        return self.check(s)

    def read_none(self, s, count):
        hub_read_set_none(self.hs, s.fileno())
        return self.check(s)

    def write(self, fd, ob, front):
        pass

    def close(self, fd):
        pass

    def shutdown(self, fd):
        pass
