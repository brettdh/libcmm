#ifndef STDIOOUTPUTTER_H_INCL
#define STDIOOUTPUTTER_H_INCL

#include <cppunit/Outputter.h>

namespace CppUnit {
    class Exception;
    class SourceLine;
    class TestResultCollector;
    class TestFailure;
}

/*! \brief Prints a TestResultCollector to a text stream.
 * \ingroup WritingTestResult
 */
class  StdioOutputter : public CppUnit::Outputter
{
public:
  StdioOutputter( CppUnit::TestResultCollector *result );

  /// Destructor.
  virtual ~StdioOutputter();

  void write();
  virtual void printFailures();
  virtual void printHeader();

  virtual void printFailure( CppUnit::TestFailure *failure,
                             int failureNumber );
  virtual void printFailureListMark( int failureNumber );
  virtual void printFailureTestName( CppUnit::TestFailure *failure );
  virtual void printFailureType( CppUnit::TestFailure *failure );
  virtual void printFailureLocation( CppUnit::SourceLine sourceLine );
  virtual void printFailureDetail( CppUnit::Exception *thrownException );
  virtual void printFailureWarning();
  virtual void printStatistics();

protected:
  CppUnit::TestResultCollector *m_result;

private:
  /// Prevents the use of the copy constructor.
  StdioOutputter( const StdioOutputter &copy );

  /// Prevents the use of the copy operator.
  void operator =( const StdioOutputter &copy );
};

#endif  // STDIOOUTPUTTER_H_INCL
