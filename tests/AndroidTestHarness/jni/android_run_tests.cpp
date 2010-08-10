#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/TestListener.h>
#include <cppunit/TestRunner.h>
#include <cppunit/Test.h>
#include <cppunit/TestFailure.h>
#include <cppunit/TestResult.h>

#include <unistd.h>
#include <jni.h>

using CppUnit::TestFactoryRegistry;
using CppUnit::TestListener;
using CppUnit::TestRunner;
using CppUnit::Test;
using CppUnit::TestResult;
using CppUnit::TestFailure;

bool g_receiver = false;
char *g_hostname = (char*)"meatball.eecs.umich.edu";

#ifdef __cplusplus
extern "C" {
#endif    

#include <android/log.h>
static void DEBUG(const char *fmt, ...)
{
    return;
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "AndroidTestHarness", fmt, ap);
    va_end(ap);
}

class AndroidTestListener : public TestListener {
    JNIEnv *jenv;
    jobject jobj;
    bool lastTestFailed;
  public:
    AndroidTestListener(JNIEnv *jenv_, jobject jobj_) 
        : jenv(jenv_), jobj(jobj_), lastTestFailed(false) {
        DEBUG("Created test listener\n");
    }
    
    virtual ~AndroidTestListener() {
        DEBUG("Destroyed test listener\n");
    }
    
    virtual void startTest(Test *test) {
        DEBUG("Starting new test\n");
        lastTestFailed = false;
        upcall("addTest", "(Ljava/lang/String;)V", test->getName().c_str());
    }
    
    virtual void addFailure(const TestFailure &failure) {
        lastTestFailed = true;
    }
    
    virtual void endTest(Test *test) {
        if (lastTestFailed) {
            DEBUG("Current test failed\n");
            upcall("testFailure", "(Ljava/lang/String;Ljava/lang/String;)V",
                   test->getName().c_str(), "oops!");
        } else {
            DEBUG("Current test passed\n");
            upcall("testSuccess", "(Ljava/lang/String;)V", 
                   test->getName().c_str());
        }
    }
    
    virtual void startSuite(Test *suite) {}
    
    virtual void endSuite(Test *suite) {}
    
  private:
    void upcall(const char *methodName, const char *methodSignature,
                const char *testName, const char *message = NULL) {
        jclass cls = jenv->GetObjectClass(jobj);
        jmethodID mid = jenv->GetMethodID(cls, methodName, methodSignature);
        if (mid == NULL) {
            return; /* method not found */
        }

        if (!jenv) {
            DEBUG("ERROR: jenv is NULL!\n");
            throw -1;
        } else {
            DEBUG("DEBUG: jenv:%p jobj: %p method: %s mid: %p\n",
                  jenv, jobj, methodName, mid); 
            DEBUG("DEBUG: testName: %s message: %s\n", testName, message);
        }
        
        jstring testName_jstr = jenv->NewStringUTF(testName);
        if (message) {
            jstring message_jstr = jenv->NewStringUTF(message);
            jenv->CallVoidMethod(jobj, mid, testName_jstr, message_jstr);
        } else {
            jenv->CallVoidMethod(jobj, mid, testName_jstr);
        }
    }
};


JNIEXPORT void JNICALL 
Java_edu_umich_intnw_androidtestharness_AndroidTestHarness_runTests(
    JNIEnv *jenv, jobject obj)
{
    // XXX: if I want to run any other set of tests, I can't use the registry.
    // ...unless I make a separate .apk for each test harness.
    try {
        TestFactoryRegistry& registry = TestFactoryRegistry::getRegistry();
        TestResult result;
        result.addListener(new AndroidTestListener(jenv, obj));
        
        TestRunner runner;
        runner.addTest(registry.makeTest());
        
        runner.run(result);
    } catch (int e) {
        DEBUG("Running tests failed!\n");
    }
}

#ifdef __cplusplus
}
#endif
