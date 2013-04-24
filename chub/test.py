from time import sleep
from functools import partial
import thread
import socket
from chub.chub import Chub
c = Chub()

thread.start_new_thread(c.run, ())

def hello():
    print "hello other world!"
    res = c.timer(4, hello)
    print res

for x in xrange(5):
    print 'try'
    c.schedule(hello)

sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setblocking(0)
sock.bind(('', 4478))
sock.listen(512)

def on_close(sock):
    print 'close!'
    pass

def on_ready(sock):
    while True:
        got = c.read_bytes(sock, 100)
        if not got:
            break
        print 'got', got

def on_accept(sock, host, port):
    print 'accepted', sock, 'on', host, 'at', port
    c.register(sock, partial(on_ready, sock), partial(on_close, sock))
    while True:
        got = c.read_anything(sock)
        if not got:
            break
        print 'got', got

c.do_accepts(sock, on_accept)

sleep(30)

