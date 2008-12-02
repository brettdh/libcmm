Note: this API description is current as of:
$Date$

-------------

libcmm: Connection Manager Manager [sic]


Libcmm provides wrapper functions for the standard socket library
to enable application deal with diverse networks. Libcmm uses the
notion of labels to represent networks. The main idea is for 
applications to describe their intent about a network activity
to the operating system so that the OS can match a packet with 
a suitable network: say a slow but reliable GPRS connection vs. 
a fast but intermittent WiFi connection for a smartphone in motion.
For example, an application might have an ondemand user-triggered 
network activity that has to happen as soon as possible, as well 
as a background sychornization that can wait for some time. In 
the previous case, the application will use a label like 'ONDEMAND' 
with such packets, and in the later uses a label like 'BACKGROUND'. 
As such, applications act up on MC_Sockets (Multi Colored Sockets), 
that can take labels to describe intent.

Each of the standard functions such as cmm_connect, cmm_send, 
cmm_writev etc. take custom resume handler and argument (hereafter
referred to as a "thunk"), which come in to play if the operation
is deferred due to no suitable network being available at the time.
The handler argument (a void*) must point to global or heap-allocated
state, and it must be freed (if necessary) by the handler.  If the 
handler is not stored, as in scenario 1 below, then the caller must 
free the argument.

cmm_connect is a special case. It takes two handler functions: one to 
perform any application level teardown nessasary whenever a connection 
needs to be closed, and another to perform any application level setup 
that needs to be performed whenever a new connection is established. As
long as the application does not specifically cmm_close an MC_Socket, 
it can assume the socket is available, even in the face of networks 
going in and out of range. 

Given a cmm_send (or similar) with arbitrary labels, one of two scenarios 
can happen:
 
1) If a matching network is available, then the underlying system call
   succeeds and the thunk is ignored. The caller should free its 
   argument. 

2) If no matching network is currently available, then the thunk is
   stored for later invocation.  The caller should treat its argument
   pointer as dangling, since the handler function now owns the
   argument and will free it upon execution.  In this scenario, the
   cmm_ function will return CMM_DEFERRED.

The cmm_ functions return CMM_FAILED to indicate a real network
failure (i.e. not due to networks being unavailable). Libcmm needs
a Connection Scout that scouts available networks as they come and
 go, and classifies them to the OS according to the labels. Currently,
 the "connection scout," a user-level process, monitors the network 
and sends updates through POSIX message queues; see mq_overview(7). 
Applications that link with libcmm will automatically subscribe to 
the connection scout upon startup and unsubscribe upon exit.  When 
a network comes up, the connection scout sends SIGHUP to all of the 
subscriber processes, invoking the signal handler defined in libcmm, 
which invokes any thunks that are waiting to complete.

Thunks may be cancelled (though this is as yet untested) with the
cmm_thunk_cancel function.  It flags all thunks matching the handler
function pointer as cancelled and, optionally, invokes the deleter
function on their stored arguments.  cmm_thunk_cancel is currently
ineffective if called from within a thunk being executed by the signal
handler; this is just a bug, rather than a hard requirement of the
API.
