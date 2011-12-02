#include <jni.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "debug.h"

#include <map>
using std::map;

#include <pthread.h>
#include <signal.h>

static void jniThrowExceptionVaList(JNIEnv *jenv, const char *className, const char *fmt, 
                                    va_list ap)
{
    const size_t MAXCHARS = 256;
    char buf[MAXCHARS + 1];
    vsnprintf(buf, MAXCHARS, fmt, ap);

    jclass exceptionClass = jenv->FindClass(className);
    if (exceptionClass) {
        jenv->ThrowNew(exceptionClass, buf);
    }

}

static void jniThrowException(JNIEnv *jenv, const char *className, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void jniThrowException(JNIEnv *jenv, const char *className, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jniThrowExceptionVaList(jenv, className, fmt, ap);
    va_end(ap);
}

static void jniThrowNullPointerException(JNIEnv *jenv, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void jniThrowNullPointerException(JNIEnv *jenv, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jniThrowExceptionVaList(jenv, "java/lang/NullPointerException", fmt, ap);
    va_end(ap);
}

static void jniThrowIOException(JNIEnv *jenv, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void jniThrowIOException(JNIEnv *jenv, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jniThrowExceptionVaList(jenv, "java/io/IOException", fmt, ap);
    va_end(ap);
}

static void jniThrowSocketException(JNIEnv *jenv, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void jniThrowSocketException(JNIEnv *jenv, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jniThrowExceptionVaList(jenv, "java/net/SocketException", fmt, ap);
    va_end(ap);
}

/* LICENSE INFO:
 * from the AOSP - specifically, Apache harmony project.
 * android-source/libcore/luni/src/main/native/NetworkUtilities.cpp
 */
static bool byteArrayToSocketAddress(JNIEnv* env, jbyteArray byteArray, int port, sockaddr_storage* ss) {
    if (byteArray == NULL) {
        jniThrowNullPointerException(env, NULL);
        return false;
    }

    // Convert the IP address bytes to the proper IP address type.
    size_t addressLength = env->GetArrayLength(byteArray);
    memset(ss, 0, sizeof(*ss));
    if (addressLength == 4) {
        // IPv4 address.
        sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(ss);
        sin->sin_family = AF_INET;
        sin->sin_port = htons(port);
        jbyte* dst = reinterpret_cast<jbyte*>(&sin->sin_addr.s_addr);
        env->GetByteArrayRegion(byteArray, 0, 4, dst);
    } else if (addressLength == 16) {
        // IPv6 address.
        sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(ss);
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(port);
        jbyte* dst = reinterpret_cast<jbyte*>(&sin6->sin6_addr.s6_addr);
        env->GetByteArrayRegion(byteArray, 0, 16, dst);
    } else {
        // We can't throw SocketException. We aren't meant to see bad addresses, so seeing one
        // really does imply an internal error.
        // TODO: fix the code (native and Java) so we don't paint ourselves into this corner.
        char buf[64];
        snprintf(buf, sizeof(buf), "byteArrayToSocketAddress bad array length (%i)", addressLength);
        jniThrowException(env, "java/lang/IllegalArgumentException", buf);
        return false;
    }
    return true;
}

#include <libcmm.h>
#include <libcmm_irob.h>
#include <libcmm_net_restriction.h>

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_stream_socket
 * Signature: ()I
 */
JNIEXPORT jint JNICALL 
Java_edu_umich_intnw_SystemCalls_ms_1stream_1socket(JNIEnv *, jclass)
{
    return cmm_socket(PF_INET, SOCK_STREAM, 0);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_connect
 * Signature: (I[BI)V
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_SystemCalls_ms_1connect(JNIEnv *jenv, jclass,
                                             jint msock_fd, jbyteArray addrBytes, jint port)
{
    sockaddr_storage ss;
    socklen_t addrlen = sizeof(ss);
    if (!byteArrayToSocketAddress(jenv, addrBytes, port, &ss)) {
        jniThrowIOException(jenv, "Failed to convert socket address");
    }
    if (cmm_connect(msock_fd, (struct sockaddr *)&ss, addrlen) < 0) {
        jniThrowIOException(jenv, "Failed to connect multisocket");
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_close
 * Signature: (I)V
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_SystemCalls_ms_1close(JNIEnv *jenv, jclass, jint msock_fd)
{
    cmm_close(msock_fd);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_write
 * Signature: (I[BIII)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_ms_1write(JNIEnv *jenv, jclass, jint msock_fd,
                                           jbyteArray byteArray, jint offset, jint length,
                                           jint labels)
{
    size_t arrayLength = jenv->GetArrayLength(byteArray);
    assert(offset + length <= arrayLength); // checked in caller

    jbyte *realBuffer = jenv->GetByteArrayElements(byteArray, NULL);
    jint rc = cmm_write(msock_fd, realBuffer + offset, length, labels, NULL, NULL);
    if (rc != length) {
        if (rc == CMM_UNDELIVERABLE) {
            jniThrowException(jenv, "edu/umich/intnw/UndeliverableException",
                              "Data was undeliverable due to labels");
        } else {
            jniThrowIOException(jenv, "Failed to write bytes to multisocket");
        }
    }
    jenv->ReleaseByteArrayElements(byteArray, realBuffer, JNI_ABORT);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_wait_for_input
 * Signature: (I)V
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_SystemCalls_ms_1wait_1for_1input(JNIEnv *jenv, jclass, jint msock_fd, jint timeoutMillis)
{
    struct timeval timeout;
    struct timeval *ptimeout = NULL;
    if (timeoutMillis > 0) {
        timeout.tv_sec = timeoutMillis / 1000;
        timeout.tv_usec = (timeoutMillis - (timeout.tv_sec * 1000)) * 1000;
        ptimeout = &timeout;
    }
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(msock_fd, &read_fds);
    int rc = cmm_select(msock_fd + 1, &read_fds, NULL, NULL, ptimeout);
    if (rc < 0) {
        if (errno == EINTR) {
            jniThrowException(jenv, "edu/umich/intnw/MultiSocketInterruptedException",
                              "waitForInput on multisocket %d interrupted by signal", msock_fd);
        } else {
            jniThrowIOException(jenv, "waitForInput failed; error on multisocket %d: %s", 
                                msock_fd, strerror(errno));
        }
    } else if (rc == 0) {
        jniThrowException(jenv, "java/net/SocketTimeoutException",
                          "waitForInput on multisocket %d timed out", msock_fd);
    } else {
        assert(rc == 1);
    }
}


static map<jlong, pthread_t> waitingJavaThreads;
static map<jlong, struct sigaction*> default_sa_per_thread;
static pthread_mutex_t waiters_mutex = PTHREAD_MUTEX_INITIALIZER;
static const int READ_WAKEUP_SIGNAL = 43;  // as in IntNW IPCThread, this should be fine.

static void wakeup_handler(int signum)
{
    dbgprintf("In wakeup handler, pthread 0x%x\n", pthread_self());
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    install_interruption_signal_handler
 * Signature: (J)V
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_SystemCalls_install_1interruption_1signal_1handler(JNIEnv *jenv, jclass, jlong currentThreadId)
{
    pthread_mutex_lock(&waiters_mutex);
    assert(waitingJavaThreads.count(currentThreadId) == 0);
    waitingJavaThreads[currentThreadId] = pthread_self();
    assert(default_sa_per_thread.count(currentThreadId) == 0);

    struct sigaction *default_sa = new struct sigaction;
    default_sa_per_thread[currentThreadId] = default_sa;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    memset(default_sa, 0, sizeof(*default_sa));

    sa.sa_handler = wakeup_handler;
    sigaction(READ_WAKEUP_SIGNAL, &sa, default_sa);
    default_sa->sa_handler == SIG_IGN;

    // bad case: we generate an unexpected signal
    //  for a program that expects it in some context.
    // TODO: address.  Seems unlikely, though.

    dbgprintf("Added pthread 0x%x interruption handler\n", pthread_self());

    pthread_mutex_unlock(&waiters_mutex);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    remove_interruption_signal_handler
 * Signature: (Ljava/lang/Thread;)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_remove_1interruption_1signal_1handler(JNIEnv *jenv, jclass, jlong currentThreadId)
{
    pthread_mutex_lock(&waiters_mutex);
    assert(waitingJavaThreads.count(currentThreadId) == 1);
    waitingJavaThreads.erase(currentThreadId);
    assert(default_sa_per_thread.count(currentThreadId) == 1);
    struct sigaction *default_sa = default_sa_per_thread[currentThreadId];
    default_sa_per_thread.erase(currentThreadId);
    
    //sigaction(READ_WAKEUP_SIGNAL, default_sa, NULL);
    delete default_sa;
    dbgprintf("Removed pthread 0x%x interruption handler\n", pthread_self());

    pthread_mutex_unlock(&waiters_mutex);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    interrupt_waiter
 * Signature: (Ljava/lang/Thread;)V
 */
JNIEXPORT void JNICALL Java_edu_umich_intnw_SystemCalls_interrupt_1waiter(JNIEnv *jenv, jclass, jlong waitingThreadId)
{
    pthread_mutex_lock(&waiters_mutex);
    if (waitingJavaThreads.count(waitingThreadId) == 1) {
        pthread_t waitingPthread = waitingJavaThreads[waitingThreadId];
        pthread_kill(waitingPthread, READ_WAKEUP_SIGNAL);
        dbgprintf("Sent wakeup signal to waiter pthread 0x%x\n", waitingPthread);
    }
    pthread_mutex_unlock(&waiters_mutex);
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    ms_read
 * Signature: (I[BII[I)I
 */
JNIEXPORT jint JNICALL 
Java_edu_umich_intnw_SystemCalls_ms_1read(JNIEnv *jenv, jclass, 
                                          jint msock_fd, jbyteArray byteArray, 
                                          jint offset, jint length, jintArray outLabels)
{
    size_t arrayLength = jenv->GetArrayLength(byteArray);
    assert(offset + length <= arrayLength); // checked in caller

    u_long labels = 0;

    jbyte *realBuffer = jenv->GetByteArrayElements(byteArray, NULL);
    jint *plabelsArray = NULL;
    if (outLabels) {
        plabelsArray = jenv->GetIntArrayElements(outLabels, NULL);
    }

    int rc = cmm_read(msock_fd, realBuffer + offset, length, &labels);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // read expects 0 for non-blocking return.
            return 0;
        } else if (errno == ETIMEDOUT) {
            jniThrowException(jenv, "java/net/SocketTimeoutException", 
                              "Timed out waiting for bytes on multisocket");
            return -1;
        } else {
            jniThrowIOException(jenv, "Failed to read bytes from multisocket: %s",
                                strerror(errno));
            return -1;
        }
    } else if (rc == 0) {
        // Java InputStream#read expects -1 at end-of-stream.
        return -1;
    }
    
    if (outLabels) {
        assert(plabelsArray);
        *plabelsArray = labels;
        jenv->ReleaseIntArrayElements(outLabels, plabelsArray, 0);
    }
    jenv->ReleaseByteArrayElements(byteArray, realBuffer, 0);
    // Release<type>ArrayElements writes back changes, frees native array

    return rc;
}

#define JAVASOCKOPT_IP_MULTICAST_IF 16
#define JAVASOCKOPT_IP_MULTICAST_IF2 31
#define JAVASOCKOPT_IP_MULTICAST_LOOP 18
#define JAVASOCKOPT_IP_TOS 3
#define JAVASOCKOPT_MCAST_JOIN_GROUP 19
#define JAVASOCKOPT_MCAST_LEAVE_GROUP 20
#define JAVASOCKOPT_MULTICAST_TTL 17
#define JAVASOCKOPT_SO_BROADCAST 32
#define JAVASOCKOPT_SO_KEEPALIVE 8
#define JAVASOCKOPT_SO_LINGER 128
#define JAVASOCKOPT_SO_OOBINLINE  4099
#define JAVASOCKOPT_SO_RCVBUF 4098
#define JAVASOCKOPT_SO_TIMEOUT  4102
#define JAVASOCKOPT_SO_REUSEADDR 4
#define JAVASOCKOPT_SO_SNDBUF 4097
#define JAVASOCKOPT_TCP_NODELAY 1

    
static int getSockoptName(int javaSockoptName)
{
    switch (javaSockoptName) {
    case JAVASOCKOPT_SO_SNDBUF:
        return SO_SNDBUF;
    case JAVASOCKOPT_SO_RCVBUF:
        return SO_RCVBUF;
    case JAVASOCKOPT_SO_LINGER:
        return SO_LINGER;
    case JAVASOCKOPT_SO_TIMEOUT:
        return SO_RCVTIMEO;
    case JAVASOCKOPT_TCP_NODELAY:
        return TCP_NODELAY;
    default:
        return -1;
    }
}

static int
getSockoptLevel(int optname)
{
    switch (optname) {
    case TCP_NODELAY:
        return IPPROTO_TCP;
    default:
        return SOL_SOCKET;
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    setsockopt_linger
 * Signature: (IZI)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_setsockopt_1linger(JNIEnv *jenv, jclass,
                                                    jint msock_fd, jboolean on, jint timeout)
{
    struct linger linger_opt = { on, timeout };
    socklen_t optlen = sizeof(linger_opt);
    int rc = cmm_setsockopt(msock_fd, SOL_SOCKET, SO_LINGER, &linger_opt, optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to set SO_LINGER");
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    setsockopt_boolean
 * Signature: (IIZ)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_setsockopt_1boolean(JNIEnv *jenv, jclass, 
                                                     jint msock_fd, jint javaOptName,
                                                     jboolean value)
{
    int optname = getSockoptName(javaOptName);
    if (optname < 0) {
        jniThrowSocketException(jenv, "Unknown java socket option constant: %d", javaOptName);
        return;
    }
    int level = getSockoptLevel(optname);
    socklen_t optlen = sizeof(value);
    int rc = cmm_setsockopt(msock_fd, level, optname, &value, optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to set boolean sockopt");
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    setsockopt_integer
 * Signature: (III)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_setsockopt_1integer(JNIEnv *jenv, jclass, 
                                                     jint msock_fd, jint javaOptName, jint value)
{
    int optname = getSockoptName(javaOptName);
    if (optname < 0) {
        jniThrowSocketException(jenv, "Unknown java socket option constant: %d", javaOptName);
        return;
    }
    int level = getSockoptLevel(optname);
    socklen_t optlen = sizeof(value);
    int rc = cmm_setsockopt(msock_fd, level, optname, &value, optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to set integer sockopt %d: %s", optname, strerror(errno));
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    getsockopt_integer
 * Signature: (II)V
 */
JNIEXPORT jint JNICALL
Java_edu_umich_intnw_SystemCalls_getsockopt_1integer(JNIEnv *jenv, jclass, 
                                                     jint msock_fd, jint javaOptName)
{
    int optname = getSockoptName(javaOptName);
    if (optname < 0) {
        jniThrowSocketException(jenv, "Unknown java socket option constant: %d", javaOptName);
        return -1;
    }
    int level = getSockoptLevel(optname);
    jint value = 0;
    socklen_t optlen = sizeof(value);
    int rc = cmm_getsockopt(msock_fd, level, optname, &value, &optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to get integer sockopt %d: %s",
                          optname, strerror(errno));
    }
    return value;
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    set_receive_timeout
 * Signature: (II)V
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_set_1receive_1timeout(JNIEnv *jenv, jclass, 
                                                       jint msock_fd, jint timeoutMillis)
{
    struct timeval timeout = {timeoutMillis / 1000, 0};
    timeoutMillis -= (timeout.tv_sec * 1000);
    timeout.tv_usec = timeoutMillis * 1000;
    socklen_t optlen = sizeof(timeout);
    int rc = cmm_setsockopt(msock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to set receive timeout: %s", strerror(errno));
    }
}

/*
 * Class:     edu_umich_intnw_SystemCalls
 * Method:    get_receive_timeout
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL
Java_edu_umich_intnw_SystemCalls_get_1receive_1timeout(JNIEnv *jenv, jclass, 
                                                       jint msock_fd)
{
    jint timeoutMillis = 0;
    
    struct timeval timeout = {0, 0};
    socklen_t optlen = sizeof(timeout);
    int rc = cmm_getsockopt(msock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, &optlen);
    if (rc < 0) {
        jniThrowSocketException(jenv, "Failed to get receive timeout: %s", strerror(errno));
    }
    return (timeout.tv_sec * 1000) + (timeout.tv_usec / 1000);
}

JNIEXPORT jint JNICALL
Java_edu_umich_intnw_SystemCalls_getPort(JNIEnv *jenv, jclass,
                                         jint msock_fd)
{
    // XXX: only works for AF_INET sockets?
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    
    int rc = cmm_getpeername(msock_fd, (struct sockaddr *)&addr, &addrlen);
    if (rc < 0) {
        return -1;
    }
    jint remotePort = ntohs(addr.sin_port);
    return remotePort;
}

static void doShutdown(JNIEnv *jenv, jint msock_fd, int how, const char *direction)
{
    int rc = cmm_shutdown(msock_fd, how);
    if (rc < 0) {
        jniThrowSocketException(jenv, "shutdown%s failed: %s", direction, strerror(errno));
    }
}

JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_shutdownInput(JNIEnv *jenv, jclass,
                                               jint msock_fd)
{
    doShutdown(jenv, msock_fd, SHUT_RD, "Input");
}

JNIEXPORT void JNICALL
Java_edu_umich_intnw_SystemCalls_shutdownOutput(JNIEnv *jenv, jclass,
                                                jint msock_fd)
{
    doShutdown(jenv, msock_fd, SHUT_WR, "Output");
}

#ifdef __cplusplus
}
#endif
