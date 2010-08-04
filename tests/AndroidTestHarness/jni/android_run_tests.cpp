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

class AndroidTestListener : public TestListener {
    JNIEnv *jenv;
    jobject jobj;
    bool lastTestFailed;
  public:
    AndroidTestListener(JNIEnv *jenv_, jobject jobj_) 
        : jenv(jenv_), jobj(jobj_), lastTestFailed(false) {}
    
    virtual ~AndroidTestListener() {}
    
    virtual void startTest(Test *test) {
        lastTestFailed = false;
        upcall("addTest", "(Ljava/lang/String;)V", test->getName().c_str());
    }
    
    virtual void addFailure(const TestFailure &failure) {
        lastTestFailed = true;
    }
    
    virtual void endTest(Test *test) {
        if (lastTestFailed) {
            upcall("testSuccess", "(Ljava/lang/String;Ljava/lang/String;)V",
                   test->getName().c_str(), "oops!");
        } else {
            upcall("startTest", "(Ljava/lang/String;)V", 
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

        if (message) {
            jenv->CallVoidMethod(jobj, mid, testName, message);
        } else {
            jenv->CallVoidMethod(jobj, mid, testName);
        }
    }
};


JNIEXPORT void JNICALL 
Java_edu_umich_intnw_AndroidTestHarness_runTests(JNIEnv *jenv, jobject obj)
{
    // XXX: if I want to run any other set of tests, I can't use the registry.
    // ...unless I make a separate .apk for each test harness.
    TestFactoryRegistry& registry = TestFactoryRegistry::getRegistry();
    TestResult result;
    result.addListener(new AndroidTestListener(jenv, obj));

    TestRunner runner;
    runner.addTest(registry.makeTest());
    
    runner.run(result);
}

#ifdef __cplusplus
}
#endif
