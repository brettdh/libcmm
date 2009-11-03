#ifndef non_blocking_tests_h_incl
#define non_blocking_tests_h_incl

#include <cppunit/TestFixture.h>
#include <libcmm.h>
#include "end_to_end_tests_base.h"

class NonBlockingTestsBase {
  protected:
    void testTransferNB(EndToEndTestsBase *base);
    void testFragmentationNB(EndToEndTestsBase *base);

    void setupReceiverNB(EndToEndTestsBase *base);
    void startReceiverNB(EndToEndTestsBase *base);
    void startSenderNB(EndToEndTestsBase *base);    
};

#define DEFINE_WRAPPER_METHOD(classname, methodname)    \
    void classname::methodname(void)                    \
    {                                                   \
        NonBlockingTestsBase::methodname##NB(this);     \
    }

#define DEFINE_WRAPPER_METHODS(classname)               \
    DEFINE_WRAPPER_METHOD(classname,setupReceiver)      \
    DEFINE_WRAPPER_METHOD(classname,startReceiver)      \
    DEFINE_WRAPPER_METHOD(classname,startSender)        \
    DEFINE_WRAPPER_METHOD(classname,testTransfer)       \
    DEFINE_WRAPPER_METHOD(classname,testFragmentation)
    

#define DECLARE_WRAPPER_METHOD(methodname)      \
    virtual void methodname();

#define DECLARE_WRAPPER_METHODS()               \
    DECLARE_WRAPPER_METHOD(setupReceiver)       \
    DECLARE_WRAPPER_METHOD(startReceiver)       \
    DECLARE_WRAPPER_METHOD(startSender)         \
    DECLARE_WRAPPER_METHOD(testTransfer)        \
    DECLARE_WRAPPER_METHOD(testFragmentation)

#endif
