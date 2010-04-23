#include <cppunit/Test.h>
#include <cppunit/TestAssert.h>
#include "pthread_util_test.h"

CPPUNIT_TEST_SUITE_REGISTRATION(PthreadUtilTest);

void 
PthreadUtilTest::setUp()
{
}

void 
PthreadUtilTest::tearDown()
{
}

struct TestThread {
    TestThread(LockWrappedQueue<int> *q_, bool rdr) : q(q_), reader(rdr) {}
    void start();
    void Run();
    void join();

    LockWrappedQueue<int> *q;
    bool reader;
    pthread_t tid;
};

static void *Runner(struct TestThread *thread)
{
    thread->Run();
    return NULL;
}

void TestThread::start()
{
    (void)pthread_create(&tid, NULL, (void*(*)(void*))&Runner, this);
}

void TestThread::Run()
{
    if (reader) {
        int last = 0;
        while (last != 7) {
            int next;
            q->pop(next);
            CPPUNIT_ASSERT(next == (last + 1));
            last = next;
        }
    } else {
        for (int i = 1; i <= 7; ++i) {
            q->push(i);
            
            // sleep a bit; make the reader wait
            struct timeval to = {1, 0};
            (void)select(0, NULL, NULL, NULL, &to);
        }
    }
}

void TestThread::join()
{
    (void)pthread_join(tid, NULL);
}

void 
PthreadUtilTest::testQueue()
{
    LockWrappedQueue<int> q;
    TestThread reader(&q, true);
    TestThread writer(&q, false);
    reader.start();
    sleep(1);
    writer.start();
    writer.join();
    reader.join();
}
