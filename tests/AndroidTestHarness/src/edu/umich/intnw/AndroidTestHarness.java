package edu.umich.intnw;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.AbsListView;
import android.widget.ExpandableListView;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.view.Gravity;
import android.widget.BaseExpandableListAdapter;

import java.util.Map;
import java.util.HashMap;
import java.util.List;
import java.util.ArrayList;

public class AndroidTestHarness extends Activity
{
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        ExpandableListView list = 
            (ExpandableListView) findViewById(R.id.test_list);
        list.setAdapter(mAdapter);
        
        Button startTests = (Button) findViewById(R.id.start_tests);
        startTests.setOnClickListener(mStartTestsListener);
    }
    
    static {
        System.loadLibrary("run_remote_tests");
    }
    
    OnClickListener mStartTestsListener = new OnClickListener() {
        public void onClick(View v) {
            runTests();
        }
    };
    
    public native void runTests();
    
    public void addTest(String testName) {
        mAdapter.addTest(testName);
    }
    
    public void testSuccess(String testName) {
        mAdapter.testSuccess(testName);
    }
    
    public void testFailure(String testName, String failureMessage) {
        mAdapter.testFailure(testName, failureMessage);
    }
    
    public TestListenerAdapter mAdapter = new TestListenerAdapter();
    
    enum TestStatus {
        RUNNING, SUCCESS, FAILURE
    };
    public class TestListenerAdapter extends BaseExpandableListAdapter {
        private class TestResult {
            public TestStatus status;
            public String name;
            public String message;
            
            public TestResult(String name_) {
                status = TestStatus.RUNNING;
                name = name_;
                message = new String();
            }
            
            public void markSuccess() {
                status = TestStatus.SUCCESS;
            }
            
            public void markFailure(String msg) {
                status = TestStatus.FAILURE;
                message = msg;
            }
            
            @Override
            public String toString() {
                return name;
            }
        }
        
        private Map<String, Integer> testIDs = new HashMap<String, Integer>();
        private List<TestResult> testResults = new ArrayList<TestResult>();
        
        // my interface
        
        public void addTest(String testName) {
            testIDs.put(testName, testResults.size());
            testResults.add(new TestResult(testName));
            notifyDataSetChanged();
        }
        
        public void testSuccess(String testName) {
            int id = testIDs.get(testName);
            testResults.get(id).markSuccess();
            notifyDataSetChanged();
        }
        
        public void testFailure(String testName, String failureMessage) {
            int id = testIDs.get(testName);
            testResults.get(id).markFailure(failureMessage);
            notifyDataSetChanged();
        }
        
        // Adapter support functions
        
        public Object getChild(int groupPosition, int childPosition) {
            // only one child for testResults.
            return testResults.get(groupPosition).message;
        }

        public long getChildId(int groupPosition, int childPosition) {
            return childPosition;
        }

        public int getChildrenCount(int groupPosition) {
            // only one child for testResults.
            TestResult result = (TestResult) getGroup(groupPosition);
            return (result.status == TestStatus.FAILURE) ? 1 : 0;
        }

        public TextView getGenericView() {
            // Layout parameters for the ExpandableListView
            AbsListView.LayoutParams lp = new AbsListView.LayoutParams(
                    ViewGroup.LayoutParams.FILL_PARENT, 64);

            TextView textView = new TextView(AndroidTestHarness.this);
            textView.setLayoutParams(lp);
            // Center the text vertically
            textView.setGravity(Gravity.CENTER_VERTICAL | Gravity.LEFT);
            // Set the text starting position
            textView.setPadding(36, 0, 0, 0);
            return textView;
        }

        public View getChildView(int groupPosition, int childPosition, 
                                 boolean isLastChild,
                                 View convertView, ViewGroup parent) {
            TextView textView = getGenericView();
            textView.setText(getChild(groupPosition, 
                                      childPosition).toString());
            return textView;
        }

        public Object getGroup(int groupPosition) {
            return testResults.get(groupPosition);
        }

        public int getGroupCount() {
            return testResults.size();
        }

        public long getGroupId(int groupPosition) {
            return groupPosition;
        }

        public View getGroupView(int groupPosition, boolean isExpanded,
                                 View convertView, ViewGroup parent) {
            TextView textView = getGenericView();
            TestResult result = (TestResult) getGroup(groupPosition);
            textView.setText(result.toString());
            /* TODO: 
                if (result.status == SUCCESS) {}
                    textView.setBackgroundColor(green);
                } else if (result.status == FAILURE) {
                    textView.setBackgroundColor(red);
                }
             */
            return textView;
        }
        
        public boolean isChildSelectable(int groupPosition, 
                                         int childPosition) {
            return true;
        }

        public boolean hasStableIds() {
            return true;
        }
    }
}
