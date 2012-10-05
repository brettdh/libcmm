#include <stdio.h>
#include <libcmm.h>
#include <libcmm_irob.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <android/log.h>
static void DEBUG_LOG(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "AndroidSimpleSender", fmt, ap);
    va_end(ap);
}

static const int CHUNK_SIZE = 40;
static const short LISTEN_PORT = 4242;
static mc_socket_t shared_sock = -1;
static pthread_mutex_t socket_lock = PTHREAD_MUTEX_INITIALIZER;

class ReplyThread {
    JNIEnv *jenv;
    jobject jobj;
    jclass cls;
    jmethodID mid;
    pthread_t tid;
    
  public:
    ReplyThread(JNIEnv *jenv_, jobject jobj_) : jenv(jenv_), jobj(jobj_) {
        cls = jenv->GetObjectClass(jobj);
        mid = jenv->GetMethodID(cls, "displayResponse", 
                                "(Ljava/lang/String;Z)V");
        if (mid == NULL) {
             /* method not found */
            jclass Exception = jenv->FindClass("java/lang/Exception");
            jenv->ThrowNew(Exception, "displayResponse method not found!");
        }
        tid = pthread_self();
    }
    
    void join() {
        pthread_join(tid, NULL);
    }

    void run() {
        while (1) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(shared_sock, &readfds);
            int s_rc = cmm_select(shared_sock+1, &readfds, NULL, NULL, NULL);
            if (s_rc < 0) {
                DEBUG_LOG("cmm_select failed: %s\n", strerror(errno));
                break;
            }
            
            char buf[CHUNK_SIZE];
            memset(buf, 0, CHUNK_SIZE);
            DEBUG_LOG("Receiving reply\n");
            
            u_long labels = 0;
            int rc = cmm_read(shared_sock, buf, CHUNK_SIZE, &labels);
            if (rc != CHUNK_SIZE) {
                DEBUG_LOG("cmm_read: %s\n", strerror(errno));
                break;
            }
            bool foreground = (labels & CMM_LABEL_ONDEMAND);
            DEBUG_LOG("Received %s msg (labels %d)\n",
                      foreground ? "FG" : "BG", labels);
            
            buf[CHUNK_SIZE-1] = '\0';
            DEBUG_LOG("Echo: %*s\n", (int)(CHUNK_SIZE - 1), buf);
            
            //Tell Android activity about the reply message
            jstring responseString = jenv->NewStringUTF(buf);
            jenv->CallVoidMethod(jobj, mid, responseString, foreground);
        }
    }
};


static ReplyThread *replyThread = NULL;

int srv_connect(const char *hostname, short port)
{
    int rc;
    
    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(LISTEN_PORT);

    struct hostent *hp = gethostbyname(hostname);
    if (hp == NULL) {
        DEBUG_LOG("Failed to lookup hostname %s\n", hostname);
        return -1;
    }
    memcpy(&srv_addr.sin_addr, hp->h_addr, hp->h_length);
    
    shared_sock = cmm_socket(PF_INET, SOCK_STREAM, 0);
    if (shared_sock < 0) {
        DEBUG_LOG("socket: %s\n", strerror(errno));
        return -1;
    }

    int type = ALWAYS_REDUNDANT;
    rc = cmm_setsockopt(shared_sock, SOL_SOCKET, SO_CMM_REDUNDANCY_STRATEGY,
                        &type, sizeof(type));
    if (rc < 0) {
        DEBUG_LOG("cmm_setsockopt: %s\n", strerror(errno));
        return -1;
    }

  conn_retry:
    DEBUG_LOG("Attempting to connect to %s:%d\n", 
              hostname, LISTEN_PORT);
    rc = cmm_connect(shared_sock, (struct sockaddr*)&srv_addr,
                     (socklen_t)sizeof(srv_addr));
    if (rc < 0) {
        if (errno == EINTR) {
            goto conn_retry;
        } else {
            DEBUG_LOG("connect: %s\n", strerror(errno));
            DEBUG_LOG("Connection failed\n");
            cmm_close(shared_sock);
            shared_sock = -1;
        }
    } else {
        DEBUG_LOG("Connected\n");
    }
    return rc;
}

static int send_message(bool fg, int seqno)
{
    int rc;

    DEBUG_LOG("Attempting to send message\n");
    
    char buf[CHUNK_SIZE];
    memset(buf, 0, CHUNK_SIZE);
    snprintf(buf, CHUNK_SIZE-1, "%s_%04d", fg ? "FG" : "BG", seqno);
    
     // should have no contention; triggered by button-press
    pthread_mutex_lock(&socket_lock);
    u_long labels = fg ? CMM_LABEL_ONDEMAND : CMM_LABEL_BACKGROUND;
    labels |= CMM_LABEL_SMALL;
    rc = cmm_write_with_deps(shared_sock, buf, CHUNK_SIZE, 0, NULL,
                             labels, NULL, NULL, NULL);
    pthread_mutex_unlock(&socket_lock);
    if (rc < 0) {
        DEBUG_LOG("cmm_send: %s\n", strerror(errno));
    } else {
        DEBUG_LOG("message sent\n");
    }
    return rc;
}


// JNI methods

/*                                                                                                                                      
 * Class:     edu_umich_intnw_simplesender_SimpleSender                                                                                 
 * Method:    connect                                                                                                                   
 * Signature: (Ljava/lang/String;S)V                                                                                                    
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_simplesender_SimpleSender_connect(JNIEnv *jenv, 
                                                       jobject jobj,
                                                       jstring hostname,
                                                       jshort port)
{
    if (shared_sock == -1) {
        const char *str = jenv->GetStringUTFChars(hostname, NULL);
        if (str == NULL) {
            DEBUG_LOG("Got null hostname string!\n");
            jclass Exception = jenv->FindClass("java/lang/InvalidArgumentException");
            jenv->ThrowNew(Exception, "hostname cannot be null");
        }
        int rc = srv_connect(str, port);
        if (rc < 0) {
            jclass Exception = jenv->FindClass("java/lang/Exception");
            jenv->ThrowNew(Exception, "Failed to connect!");
        }
        if (str) {
            jenv->ReleaseStringUTFChars(hostname, str);
        }
    } else {
        DEBUG_LOG("SimpleSender.connect: already connected\n");
    }
}

/*                                                                                                                                      
 * Class:     edu_umich_intnw_simplesender_SimpleSender                                                                                 
 * Method:    disconnect                                                                                                                
 * Signature: ()V                                                                                                                       
 */
JNIEXPORT void JNICALL
Java_edu_umich_intnw_simplesender_SimpleSender_disconnect(JNIEnv *jenv, 
                                                          jobject jobj)
{
    if (shared_sock != -1) {
        cmm_shutdown(shared_sock, SHUT_RDWR);
        replyThread->join();
        delete replyThread;
        replyThread = NULL;
        
        cmm_close(shared_sock);
        shared_sock = -1;
    } else {
        DEBUG_LOG("SimpleSender.disconnect: not connected\n");
    }
}

/*                                                                                                                                      
 * Class:     edu_umich_intnw_simplesender_SimpleSender                                                                                 
 * Method:    sendFG                                                                                                                    
 * Signature: (I)V                                                                                                                      
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_simplesender_SimpleSender_sendFG(JNIEnv *jenv, 
                                                      jobject jobj, 
                                                      jint seqno)
{
    if (shared_sock != -1) {
        int rc = send_message(true, seqno);
        if (rc < 0) {
            jclass Exception = jenv->FindClass("java/lang/Exception");
            jenv->ThrowNew(Exception, "Failed to send FG message");
        }
    } else {
        DEBUG_LOG("sendFG: can't send message: not connected\n");
    }
}

/*                                                                                                                                      
 * Class:     edu_umich_intnw_simplesender_SimpleSender                                                                                 
 * Method:    sendBG                                                                                                                    
 * Signature: (I)V                                                                                                                      
 */
JNIEXPORT void JNICALL 
Java_edu_umich_intnw_simplesender_SimpleSender_sendBG(JNIEnv *jenv, 
                                                      jobject jobj, 
                                                      jint seqno)
{
    if (shared_sock != -1) {
        int rc = send_message(false, seqno);
        if (rc < 0) {
            jclass Exception = jenv->FindClass("java/lang/Exception");
            jenv->ThrowNew(Exception, "Failed to send BG message");
        }
    } else {
        DEBUG_LOG("sendBG: can't send message: not connected\n");
    }
}

// This is called in a new thread.
JNIEXPORT void JNICALL
Java_edu_umich_intnw_simplesender_SimpleSender_runReplyThread(JNIEnv *jenv, 
                                                              jobject jobj)
{
    replyThread = new ReplyThread(jenv, jobj);
    replyThread->run();
}

#ifdef __cplusplus
}
#endif
