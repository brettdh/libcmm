Note: this API description is current as of:
$Date$

-------------

libcmm: Connection Manager Manager [sic]

Libcmm provides wrapper functions for the standard socket library
functions, such as connect, send, writev.  Each of these takes a
custom resume handler and argument (hereafter referred to as a
"thunk"), which come into play if the operation is deferred due to no
suitable network being available at the time.

The handler argument (a void*) must point to global or heap-allocated
state, and it must be freed (if necessary) by the handler.  If the
handler is not stored, as in scenario 1 below, then the caller must
free the argument.

cmm_connect is a special case.  It takes two handler functions: one to
be called after socket() and before connect(), and another to be
called after connect() but before any calls to cmm_send or
similar. The first is useful for setting any socket options that are
ineffective if set after connect(); the second is useful for any
application-level connection setup that is required.  Both handler
functions are passed the socket, the labels of this reconnection, and
the void* argument specified by the initial call to cmm_connect.

Any time that the socket needs to be reconnected due to switching
labels or a previously unavailable network becoming available, libcmm
closes and recreates the socket, calls the pre_reconnect handler,
connect()s the socket, and finally calls the post_reconnect handler
before calling any application thunks.  Thus applications can treat
the socket as always connected, as long as cmm_send (e.g.) functions
do not fail (see below).

Given a cmm_send (or similar) with arbitrary labels, the three
following scenarios invoke different behavior:
 
1) If a matching network is available and connected (i.e. the last
   send on this socket had the same label*), then the underlying system
   call succeeds and the thunk is ignored.  The caller should
   free its argument.
2) If a matching network is available but not connected (i.e. the last
   send on this socket had different labels*), then the thunk is
   executed immediately.  Since the handler is required to free its
   argument, the caller should treat its pointer as dangling upon
   return from the cmm_ call.  In this scenario, the cmm_ function
   will return CMM_THUNK_EXECUTED.
3) If no matching network is currently available, then the thunk is
   stored for later invocation.  The caller should treat its argument
   pointer as dangling, since the handler function now owns the
   argument and will free it upon execution.  In this scenario, the
   cmm_ function will return CMM_DEFERRED.

(*note: I think this is broken in the presence of multiple networks with the
 same labels.  It should instead check whether the network in question
 is the one that this socket is connected on.)

The cmm_ functions return CMM_FAILED to indicate a real network
failure (i.e. not due to switching networks or networks being
unavailable).

Currently, the "connection scout," a user-level process, monitors the
network and sends updates through POSIX message queues; see
mq_overview(7). Applications that link with libcmm will automatically
subscribe to the connection scout upon startup and unsubscribe upon
exit.  When a network comes up, the connection scout sends SIGHUP to
all of the subscriber processes, invoking the signal handler defined
in libcmm, which invokes any thunks that are waiting to complete.

Thunks may be cancelled (though this is as yet untested) with the
cmm_thunk_cancel function.  It flags all thunks matching the handler
function pointer as cancelled and, optionally, invokes the deleter
function on their stored arguments.  cmm_thunk_cancel is currently
ineffective if called from within a thunk being executed by the signal
handler; this is just a bug, rather than a hard requirement of the
API.
