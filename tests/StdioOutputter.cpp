#include <cppunit/Exception.h>
#include <cppunit/SourceLine.h>
#include <cppunit/TestFailure.h>
#include <cstdio>
#include "StdioOutputter.h"
#include <cppunit/TestResultCollector.h>

using namespace CppUnit;

StdioOutputter::StdioOutputter( TestResultCollector *result )
    : m_result( result )
{
}


StdioOutputter::~StdioOutputter()
{
}


void 
StdioOutputter::write() 
{
  printHeader();
  std::printf("\n");
  printFailures();
  std::printf("\n");
}


void 
StdioOutputter::printFailures()
{
  TestResultCollector::TestFailures::const_iterator itFailure = m_result->failures().begin();
  int failureNumber = 1;
  while ( itFailure != m_result->failures().end() ) 
  {
    std::printf("\n");
    printFailure( *itFailure++, failureNumber++ );
  }
}


void 
StdioOutputter::printFailure( TestFailure *failure,
                             int failureNumber )
{
  printFailureListMark( failureNumber );
  std::printf(" ");
  printFailureTestName( failure );
  std::printf(" ");
  printFailureType( failure );
  std::printf(" ");
  printFailureLocation( failure->sourceLine() );
  std::printf("\n");
  printFailureDetail( failure->thrownException() );
  std::printf("\n");
}


void 
StdioOutputter::printFailureListMark( int failureNumber )
{
  std::printf("%d)",failureNumber);
}


void 
StdioOutputter::printFailureTestName( TestFailure *failure )
{
  std::printf("test: %s", failure->failedTestName().c_str());
}


void 
StdioOutputter::printFailureType( TestFailure *failure )
{
  std::printf("(%s)", (failure->isError() ? "E" : "F"));
}


void 
StdioOutputter::printFailureLocation( SourceLine sourceLine )
{
  if ( !sourceLine.isValid() )
    return;

  std::printf("line: %d %s", 
              sourceLine.lineNumber(),
              sourceLine.fileName().c_str());
}


void 
StdioOutputter::printFailureDetail( Exception *thrownException )
{
  std::printf("%s\n", thrownException->message().shortDescription().c_str());
  std::printf("%s", thrownException->message().details().c_str());
}


void 
StdioOutputter::printHeader()
{
  if ( m_result->wasSuccessful() )
    std::printf("\nOK (%d tests)\n", m_result->runTests());
  else
  {
    std::printf("\n");
    printFailureWarning();
    printStatistics();
  }
}


void 
StdioOutputter::printFailureWarning()
{
  std::printf("!!!FAILURES!!!\n");
}


void 
StdioOutputter::printStatistics()
{
  std::printf("Test Results:\n");

  std::printf("Run:  %d   Failures: %d   Errors: %d\n",
              m_result->runTests(),
              m_result->testFailures(),
              m_result->testErrors());
}
