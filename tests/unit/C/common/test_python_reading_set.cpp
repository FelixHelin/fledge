#include <gtest/gtest.h>
#include <pythonreadingset.h>
#include <string.h>
#include <string>
#include <logger.h>
#include <pyruntime.h>

using namespace std;

namespace {

const char *script = R"(
def count(set):
    return len(set)


def make_reading_list():
    return [
        {
            'asset': 'sensor1',
            'readings': {'temperature': 25.5},
            'id': 123,
            'ts': '1970-01-01 00:00:01.000000',
            'user_ts': '1970-01-01 00:00:02.000000'
        },
        {
            'asset': 'sensor2',
            'readings': {'humidity': 60.0},
            'id': 124,
            'ts': '1970-01-01 00:00:03.000000',
            'user_ts': '1970-01-01 00:00:04.000000'
        }
    ]
)";

class  PythonReadingSetTest : public testing::Test {
 protected:
	void SetUp() override
	{
		m_python = PythonRuntime::getPythonRuntime();
	}

	void TearDown() override
	{
	}

   public:
	PythonRuntime	*m_python;

	void logErrorMessage(const char *name)
	{
		PyObject* type;
		PyObject* value;
		PyObject* traceback;


		PyErr_Fetch(&type, &value, &traceback);
		PyErr_NormalizeException(&type, &value, &traceback);

		PyObject* str_exc_value = PyObject_Repr(value);
		PyObject* pyExcValueStr = PyUnicode_AsEncodedString(str_exc_value, "utf-8", "Error ~");
		const char* pErrorMessage = value ?
					    PyBytes_AsString(pyExcValueStr) :
					    "no error description.";
		Logger::getLogger()->fatal("logErrorMessage: %s: Error '%s'", name, pErrorMessage);
		
		// Check for numpy/pandas import errors
		const char *err1 = "implement_array_function method already has a docstring";
		const char *err2 = "cannot import name 'check_array_indexer' from 'pandas.core.indexers'";

		
		std::string fcn = "";
		fcn += "def get_pretty_traceback(exc_type, exc_value, exc_tb):\n";
		fcn += "    import sys, traceback\n";
		fcn += "    lines = []\n"; 
		fcn += "    lines = traceback.format_exception(exc_type, exc_value, exc_tb)\n";
		fcn += "    output = '\\n'.join(lines)\n";
		fcn += "    return output\n";

		PyRun_SimpleString(fcn.c_str());
		PyObject* mod = PyImport_ImportModule("__main__");
		if (mod != NULL) {
			PyObject* method = PyObject_GetAttrString(mod, "get_pretty_traceback");
			if (method != NULL) {
				PyObject* outStr = PyObject_CallObject(method, Py_BuildValue("OOO", type, value, traceback));
				if (outStr != NULL) {
					PyObject* tmp = PyUnicode_AsASCIIString(outStr);
					if (tmp != NULL) {
						std::string pretty = PyBytes_AsString(tmp);
						Logger::getLogger()->fatal("%s", pretty.c_str());
						Logger::getLogger()->printLongString(pretty.c_str());
					}
					Py_CLEAR(tmp);
				}
				Py_CLEAR(outStr);
			}
			Py_CLEAR(method);
		}

		// Reset error
		PyErr_Clear();

		// Remove references
		Py_CLEAR(type);
		Py_CLEAR(value);
		Py_CLEAR(traceback);
		Py_CLEAR(str_exc_value);
		Py_CLEAR(pyExcValueStr);
		Py_CLEAR(mod);
	}

        PyObject *callPythonFunc(const char *name, PyObject *arg)
        {
                PyObject *rval = NULL;

		m_python->execute(script);
		rval = m_python->call(name, "(O)", arg);
		return rval;
	}

        PyObject *callPythonFunc2(const char *name, PyObject *arg1, PyObject *arg2)
        {
                PyObject *rval = NULL;

		m_python->execute(script);
                rval = m_python->call(name, "OO", arg1, arg2);
                return rval;
        }

        PyObject *callPythonFuncNoArgs(const char *name)
        {
                PyObject *rval = NULL;

                m_python->execute(script);
                rval = m_python->call(name, "()");
                return rval;
        }

};

TEST_F(PythonReadingSetTest, SingleReading)
{  
	vector<Reading *> *readings = new vector<Reading *>;
	long i = 1234;
	DatapointValue value(i);
	readings->push_back(new Reading("test", new Datapoint("long", value)));
	ReadingSet set(readings);
	delete readings;
	PyGILState_STATE state = PyGILState_Ensure();
	PyObject *pySet = ((PythonReadingSet *)(&set))->toPython();
	PyObject *obj = callPythonFunc("count", pySet);
	long rval = PyLong_AsLong(obj);
	PyGILState_Release(state);
	EXPECT_EQ(rval, 1);
}

TEST_F(PythonReadingSetTest, MultipleReadings)
{
        vector<Reading *> *readings = new vector<Reading *>;
	long i = 1234;
	DatapointValue value(i);
	readings->push_back(new Reading("test", new Datapoint("long", value)));
	readings->push_back(new Reading("test", new Datapoint("long", value)));
	readings->push_back(new Reading("test", new Datapoint("long", value)));
	ReadingSet set(readings);
	delete readings;
	PyGILState_STATE state = PyGILState_Ensure();
	PyObject *pySet = ((PythonReadingSet *)(&set))->toPython();
	PyObject *obj = callPythonFunc("count", pySet);
        long rval = PyLong_AsLong(obj);
        PyGILState_Release(state);
        EXPECT_EQ(rval, 3);
}

TEST_F(PythonReadingSetTest, ListPreservesMetadata)
{
        PyGILState_STATE state = PyGILState_Ensure();
        PyObject *pyList = callPythonFuncNoArgs("make_reading_list");
        ASSERT_NE(pyList, nullptr);
        PythonReadingSet set(pyList);
        Py_CLEAR(pyList);
        PyGILState_Release(state);

        const std::vector<Reading *>& readings = set.getAllReadings();
        ASSERT_EQ(readings.size(), 2);

        struct timeval ts = {0, 0};
        struct timeval uts = {0, 0};

        readings[0]->getTimestamp(&ts);
        readings[0]->getUserTimestamp(&uts);
        EXPECT_EQ(readings[0]->getId(), 123);
        EXPECT_EQ(ts.tv_sec, 1);
        EXPECT_EQ(uts.tv_sec, 2);

        readings[1]->getTimestamp(&ts);
        readings[1]->getUserTimestamp(&uts);
        EXPECT_EQ(readings[1]->getId(), 124);
        EXPECT_EQ(ts.tv_sec, 3);
        EXPECT_EQ(uts.tv_sec, 4);
}
}
