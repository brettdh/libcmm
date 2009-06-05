Note: this API description is current as of: 

$Date$

-------------

libcmm: Connection Manager Manager [sic]


Libcmm provides wrapper functions for the standard socket library to
enable application deal with diverse networks. Libcmm uses the notion
of labels to represent networks. The main idea is for applications to
describe their intent about a network activity to the operating system
so that the OS can match a packet with a suitable network: say a slow
but reliable GPRS connection vs.  a fast but intermittent WiFi
connection for a smartphone in motion.  For example, an application
might have an ondemand user-triggered network activity that has to
happen as soon as possible, as well as a background sychornization
that can wait for some time. In the previous case, the application
will use a label like 'ONDEMAND' with such packets, and in the later
uses a label like 'BACKGROUND'.  As such, applications act upon
multi-sockets, socket-like network endpoints, the operations on which
include labels to describe intent.

Applications initially use connection-oriented sockets for one (or
both) of two tasks: 1) accepting and 2) initiating connections.  After
this initial step, communication on the connection is a symmetric
matter of sending and receiving bytes with the relevant system calls.
In order to use multi-sockets, developers should understand the
analogues to these operations that are meaningful on multi-sockets,
which we describe here.  For brevity, we refer to the connection
initiator as the "client" and the connection acceptor as the "server";
this does not imply or require a strict client-server application
design.

1) Accepting connections

After creating a socket with the socket() system call, the server will
assign a local address to the socket using the bind() system call.
The server then indicates its willingness to accept connections by
calling listen() followed by accept().

THIS PART OF THE API IS NOT FINALIZED; HERE'S ONE POSSIBLE DESIGN.
In the case of multi-sockets, the listening socket is a regular TCP
socket. The server calls cmm_listen to create the connection backlog
and register the listener socket with libcmm.  Following cmm_listen,
a cmm_accept call on the listener socket returns a multi-socket file
descriptor.  Further cmm_accept calls on this listener socket will
return the same multi-socket file descriptor, internally accepting
the new physical connection and adding it to the multi-socket.

2) Initiating connections

After creating a socket with the socket() system call, the client
connects to the server using the connect() system call.  Similarly,
clients use cmm_connect for this purpose on multi-sockets; see below
for additional details.

Each of the standard functions such as cmm_connect, cmm_send,
cmm_writev etc. take custom resume handler and argument (hereafter
referred to as a "thunk"), which come in to play if the operation is
deferred due to no suitable network being available at the time.  The
handler argument (a void*) must point to global or heap-allocated
state, and it must be freed (if necessary) by the handler.  If the
handler is not stored, as in scenario 1 below, then the caller must
free the argument.

cmm_connect is a special case. It takes two handler functions: one to
perform any application level teardown nessasary whenever a connection
needs to be closed, and another to perform any application level setup
that needs to be performed whenever a new connection is
established. As long as the application does not specifically
cmm_close an MC_Socket, it can assume the socket is available, even in
the face of networks going in and out of range.

Given a cmm_send (or similar) with arbitrary labels, one of two
scenarios can happen:
 
1) If a matching network is available, then the underlying system call
   succeeds and the thunk is ignored. The caller should free its
   argument.

2) If no matching network is currently available, then the thunk is
   stored for later invocation.  The caller should treat its argument
   pointer as dangling, since the handler function now owns the
   argument and will free it upon execution.  In this scenario, the
   cmm_ function will return CMM_DEFERRED.

The cmm_ functions return CMM_FAILED to indicate a real network
failure (i.e. not due to networks being unavailable). Libcmm needs a
Connection Scout that scouts available networks as they come and go,
and classifies them to the OS according to the labels. Currently, the
"connection scout," a user-level process, monitors the network and
sends updates through POSIX message queues; see mq_overview(7).
Applications that link with libcmm will automatically subscribe to the
connection scout upon startup and unsubscribe upon exit.  When a
network comes up, the connection scout sends a signal to all of the
subscriber processes, invoking the signal handler defined in libcmm,
which invokes any thunks that are waiting to complete.

Libcmm also provldes several simple wrapper functions which invoke the
expected operation as needed on the underlying OS sockets (one or
more).  Examples include cmm_select, cmm_setsockopt, cmm_getpeername,
cmm_read, etc.

We provide two functions to help applications deal with failed network
operations.  Firstly, the cmm_check_label function returns 0 if the
given label is available; if the label is unavailable, it returns
CMM_DEFERRED and registers the supplied thunk (or CMM_FAILED if no
thunk is supplied).  If an operation fails in the middle of its system
call (for example, due to a network becoming unavailable), we simply
pass the error back to the caller.  This error must be dealt with by
the application, since a portion of the original message buffer may
have been sent.  Applications can use cmm_check_label to inform their
error-handling code.  Secondly, the cmm_reset function restores an
mc_socket to the state just after cmm_connect was called, before any
network operations have occurred.  The next cmm_send (or similar) will
attempt to connect the socket and run the application-specific
label-up callback.  This is useful when recovering from an error in a
non-deferrable function - cmm_read, for example.

Thunks may be cancelled with the cmm_thunk_cancel function.  It flags
all thunks matching the handler function pointer as cancelled and,
optionally, invokes the deleter function on their stored arguments.

Describing label preferences:
[Could be done manually or by the connection scout]
The API reads in a configuration file to determine if some interfaces are
'superior' than others. This helps the API to dynamically choose a better
interface if multiple are available. Check the sample configuration file
for format.
	 
