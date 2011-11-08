#include <jni.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

static void jniThrowException(JNIEnv *jenv, const char *className, const char *msg)
{
    jclass exceptionClass = jenv->FindClass(className);
    if (exceptionClass) {
        jenv->ThrowNew(exceptionClass, msg);
    }
}

static void jniThrowNullPointerException(JNIEnv *jenv, const char *msg)
{
    jniThrowException(jenv, "java/lang/NullPointerException", msg);
}

static void jniThrowIOException(JNIEnv *jenv, const char *msg)
{
    jniThrowException(jenv, "java/io/IOException", msg);
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
        jniThrowIOException(jenv, "Failed to read bytes from multisocket");
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

#ifdef __cplusplus
}
#endif
