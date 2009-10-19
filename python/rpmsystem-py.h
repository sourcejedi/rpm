#ifndef H_SYSTEM_PYTHON
#define	H_SYSTEM_PYTHON

#if defined(__APPLE__)
#include <sys/types.h>
#endif

#include <Python.h>
#include <structmember.h>

#include "../system.h"

#if ((PY_MAJOR_VERSION << 8) | (PY_MINOR_VERSION << 0)) < 0x0205
typedef ssize_t Py_ssize_t;
typedef Py_ssize_t (*lenfunc)(PyObject *);
#endif  

/* Compatibility macros for Python < 2.6 */
#ifndef PyVarObject_HEAD_INIT
#define PyVarObject_HEAD_INIT(type, size) \
	PyObject_HEAD_INIT(type) size,
#endif 

#ifndef Py_TYPE
#define Py_TYPE(o) ((o)->ob_type)
#endif


#endif	/* H_SYSTEM_PYTHON */
