CppUnit::Test *
PendingIROBLatticeTest::suite()
{
    CppUnit::TestSuite *testSuite = new CppUnit::TestSuite("PendingIROBLattice_Test");
    testSuite->addTest(new CppUnit::TestCaller<LatticeTest>(
                           "testLatticeStructure", 
                           &LatticeTest::testLatticeStructure));
    return testSuite;
}

void
LatticeTest::setUp()
{
    pirobs = new PendingIROBLattice();

    irob_id_t id = 1;
    pirob_array[0] = new PendingIROB(0, 0, NULL, 0, 0);
    pirob_array[1] = new PendingIROB(1, 1, &id, 0, 0);
    pirob_array[2] = new PendingIROB(2, 1, &id, 0, 0);
    pirob_array[3] = new PendingIROB(3, 0, NULL, 0, 0);
    pirob_array[4] = new PendingIROB(4, 20, new char[20], 0, 0);
    pirob_array[5] = new PendingIROB(5, 0, NULL, 0, 0);
    pirob_array[6] = new PendingIROB(6, 0, NULL, 0, 0);
    id = 6;
    pirob_array[7] = new PendingIROB(7, 1, &id, 0, 0);
    pirob_array[8] = new PendingIROB(8, 20, new char[20], 0, 0);
    pirob_array[9] = new PendingIROB(9, 20, new char[20], 0, 0);
}

void
LatticeTest::tearDown()
{
    for (int i = 0; i < 10; i++) {
        delete pirob_array[i];
    }

    pirobs->clear();
    delete pirobs;
}

void
LatticeTest::assert_insert(PendingIROB *pirob)
{
    CPPUNIT_ASSERT(pirobs->insert(pirob) == true);
    CPPUNIT_ASSERT(pirobs->find(pirob->id) == pirob);
}

void 
LatticeTest::testLatticeStructure()
{
    for (int i = 0; i < 10; i++) {
        assert_insert(pirob_array[i]);
    }

    bool dep_matrix[10][10];
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            dep_matrix[i][j] = false;
        }
    }
    
    int dep_pairs[11][2] = {
        {1,0}, {2,0}, {4,1}, {4,2}, {4,3}, 
        {5,4}, {6,4}, {7,6}, {8,5}, {8,7}, {9,8}
    };
    
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            CPPUNIT_ASSERT(pirob_array[i]->depends_on(pirob_array[j]) 
                           == dep_matrix[i][j]);
        }
    }
}
