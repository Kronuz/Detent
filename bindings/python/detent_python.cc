// Python binding: a C extension over libdetent_reader's C ABI (docs/design/client-local-source.md D1-D3).
// It reimplements nothing: every lookup is one call into the reader core, which returns the caller's
// fallback on any error. Only opening fails loudly (ReaderError).
//
// Lifetime: the object is the handle. There is no close(): a close that raced a lookup would be undefined,
// so the handle closes when the last reference goes. The type cannot be subclassed and is opened in
// tp_new, so it never changes afterwards. Lookups keep the GIL; open and refresh release it around their I/O.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "detent/reader.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

// Status, ReaderError and Lookup are small pure-Python types built at import.
const char *kPrelude = R"PY(
import enum


class Status(enum.IntEnum):
@STATUS_MEMBERS@

    @classmethod
    def _missing_(cls, value):
        # The C ABI may add statuses without a version bump: keep the integer.
        member = int.__new__(cls, value)
        member._name_ = "UNKNOWN_%d" % value
        member._value_ = value
        return member


class ReaderError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class Lookup:
    __slots__ = ("status", "value")

    def __init__(self, status, value):
        self.status = status
        self.value = value

    def __bool__(self):
        return self.status == Status.OK

    def __eq__(self, other):
        return isinstance(other, Lookup) and (self.status, self.value) == (other.status, other.value)

    def __repr__(self):
        return "Lookup(status=%r, value=%r)" % (self.status, self.value)
)PY";

// Status's members take their values from reader.h, so a renumbered header cannot drift from them.
constexpr struct {
    const char *name;
    detent_status value;
} kStatuses[] = {{"OK", DETENT_OK},
                 {"MISSING", DETENT_MISSING},
                 {"INVALID_NAME", DETENT_INVALID_NAME},
                 {"NOT_OPENED", DETENT_NOT_OPENED},
                 {"SOURCE_FAILURE", DETENT_SOURCE_FAILURE},
                 {"UNAVAILABLE", DETENT_UNAVAILABLE},
                 {"UNTRUSTED", DETENT_UNTRUSTED},
                 {"INVALID_ARGUMENT", DETENT_INVALID_ARGUMENT},
                 {"NO_MEMORY", DETENT_NO_MEMORY},
                 {"IO", DETENT_IO}};

std::string prelude() {
    std::string members;
    for (const auto &status : kStatuses)
        members += "    " + std::string(status.name) + " = " + std::to_string(int(status.value)) + "\n";
    std::string text = kPrelude;
    const std::string marker = "@STATUS_MEMBERS@\n";
    text.replace(text.find(marker), marker.size(), members);
    return text;
}

PyObject *status_type = nullptr;
PyObject *error_type = nullptr;
PyObject *lookup_type = nullptr;

struct ReaderObject {
    PyObject_HEAD
    detent_reader *handle;
};

PyObject *status_object(detent_status status) {
    return PyObject_CallFunction(status_type, "i", static_cast<int>(status));
}

void raise_open(detent_status status) {
    PyObject *code = status_object(status);
    if (!code)
        return;
    const std::string message = std::string("detent_open: ") + detent_status_name(status);
    PyObject *error = PyObject_CallFunction(error_type, "Os", code, message.c_str());
    Py_DECREF(code);
    if (error) {
        PyErr_SetObject(error_type, error);
        Py_DECREF(error);
    }
}

// A str argument (or None when `optional`) as a detent_str borrowed from the object.
bool text_arg(PyObject *object, const char *what, bool optional, detent_str *out) {
    if (optional && object == Py_None) {
        *out = {nullptr, 0};
        return true;
    }
    if (!PyUnicode_Check(object)) {
        PyErr_Format(PyExc_TypeError, "%s must be str%s", what, optional ? " or None" : "");
        return false;
    }
    Py_ssize_t size = 0;
    const char *data = PyUnicode_AsUTF8AndSize(object, &size);
    if (!data)
        return false;
    *out = {data, static_cast<size_t>(size)};
    return true;
}

PyObject *reader_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static const char *keywords[] = {"store", "publisher_uid", "namespaces", "mapping_bytes", nullptr};
    PyObject *store = nullptr, *uid_object = nullptr, *namespaces = nullptr, *mapping_object = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|OO", const_cast<char **>(keywords), &store, &uid_object,
                                     &namespaces, &mapping_object))
        return nullptr;
    std::uint64_t mapping_bytes = 0;
    if (mapping_object) {
        if (!PyLong_Check(mapping_object) || PyBool_Check(mapping_object)) {
            PyErr_SetString(PyExc_TypeError, "mapping_bytes must be int");
            return nullptr;
        }
        mapping_bytes = PyLong_AsUnsignedLongLong(mapping_object);
        if (PyErr_Occurred())
            return nullptr;  // OverflowError: negative or above 64 bits
    }
    PyObject *path = nullptr;
    if (!PyUnicode_FSConverter(store, &path))
        return nullptr;
    if (!PyLong_Check(uid_object) || PyBool_Check(uid_object)) {
        Py_DECREF(path);
        PyErr_SetString(PyExc_TypeError, "publisher_uid must be int");
        return nullptr;
    }
    const unsigned long long uid = PyLong_AsUnsignedLongLong(uid_object);
    if (PyErr_Occurred() || uid > std::numeric_limits<std::uint32_t>::max()) {
        Py_DECREF(path);
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_OverflowError, "publisher_uid does not fit in 32 bits");
        return nullptr;
    }
    // Keep the namespace strings alive until detent_open has copied what it needs.
    PyObject *sequence = nullptr;
    std::vector<detent_str> names;
    if (namespaces && namespaces != Py_None) {
        if (PyUnicode_Check(namespaces) || PyBytes_Check(namespaces)) {
            Py_DECREF(path);
            PyErr_SetString(PyExc_TypeError, "namespaces must be an iterable of str, not a single string");
            return nullptr;
        }
        sequence = PySequence_Fast(namespaces, "namespaces must be an iterable of str");
        if (!sequence) {
            Py_DECREF(path);
            return nullptr;
        }
        for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(sequence); ++i) {
            detent_str name;
            if (!text_arg(PySequence_Fast_GET_ITEM(sequence, i), "namespaces item", false, &name)) {
                Py_DECREF(sequence);
                Py_DECREF(path);
                return nullptr;
            }
            names.push_back(name);
        }
    }
    if (detent_abi_version() != DETENT_ABI_VERSION) {
        Py_XDECREF(sequence);
        Py_DECREF(path);
        PyErr_SetString(PyExc_ImportError, "libdetent_reader ABI version differs from this extension's header");
        return nullptr;
    }
    detent_open_options options{sizeof(detent_open_options),
                                {PyBytes_AS_STRING(path), static_cast<size_t>(PyBytes_GET_SIZE(path))},
                                names.data(),
                                names.size(),
                                static_cast<std::uint32_t>(uid),
                                mapping_bytes};
    detent_reader *handle = nullptr;
    detent_status status;
    Py_BEGIN_ALLOW_THREADS
    status = detent_open(&options, &handle);
    Py_END_ALLOW_THREADS
    Py_XDECREF(sequence);
    Py_DECREF(path);
    if (status != DETENT_OK) {
        raise_open(status);
        return nullptr;
    }
    auto *self = reinterpret_cast<ReaderObject *>(type->tp_alloc(type, 0));
    if (!self) {
        detent_close(handle);
        return nullptr;
    }
    self->handle = handle;
    return reinterpret_cast<PyObject *>(self);
}

void reader_dealloc(PyObject *object) {
    auto *self = reinterpret_cast<ReaderObject *>(object);
    detent_close(self->handle);
    Py_TYPE(object)->tp_free(object);
}

// Parses (name, fallback, hash=None, switch_value=None) from a fastcall vector.
struct Call {
    detent_str name{}, hash{nullptr, 0}, switch_value{nullptr, 0};
    PyObject *fallback = nullptr;
};

bool parse_call(PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames, Call *out) {
    static const char *const names[4] = {"name", "fallback", "hash", "switch_value"};
    if (nargs > 4) {
        PyErr_Format(PyExc_TypeError, "takes at most 4 arguments (%zd given)", nargs);
        return false;
    }
    PyObject *values[4] = {nullptr, nullptr, nullptr, nullptr};
    for (Py_ssize_t i = 0; i < nargs; ++i)
        values[i] = args[i];
    if (kwnames) {
        for (Py_ssize_t k = 0; k < PyTuple_GET_SIZE(kwnames); ++k) {
            const char *key = PyUnicode_AsUTF8(PyTuple_GET_ITEM(kwnames, k));
            if (!key)
                return false;
            int slot = -1;
            for (int i = 0; i < 4; ++i)
                if (std::string(names[i]) == key)
                    slot = i;
            if (slot < 0) {
                PyErr_Format(PyExc_TypeError, "unexpected keyword argument '%s'", key);
                return false;
            }
            if (values[slot]) {
                PyErr_Format(PyExc_TypeError, "argument '%s' given twice", key);
                return false;
            }
            values[slot] = args[nargs + k];
        }
    }
    for (int i = 0; i < 2; ++i)
        if (!values[i]) {
            PyErr_Format(PyExc_TypeError, "missing argument '%s'", names[i]);
            return false;
        }
    if (!text_arg(values[0], "name", false, &out->name))
        return false;
    out->fallback = values[1];
    if (values[2] && !text_arg(values[2], "hash", true, &out->hash))
        return false;
    if (values[3] && !text_arg(values[3], "switch_value", true, &out->switch_value))
        return false;
    return true;
}

// An int fallback (not bool) as int64; raises TypeError or OverflowError.
bool integer_fallback(PyObject *object, std::int64_t *out) {
    if (!PyLong_Check(object) || PyBool_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "fallback must be int");
        return false;
    }
    const long long value = PyLong_AsLongLong(object);
    if (value == -1 && PyErr_Occurred())
        return false;  // OverflowError: outside int64
    *out = value;
    return true;
}

PyObject *reader_get(PyObject *object, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    Call call;
    std::int64_t fallback = 0;
    if (!parse_call(args, nargs, kwnames, &call) || !integer_fallback(call.fallback, &fallback))
        return nullptr;
    std::int64_t out = fallback;
    detent_get(reinterpret_cast<ReaderObject *>(object)->handle, call.name, call.hash, call.switch_value, fallback,
               &out);
    return PyLong_FromLongLong(out);
}

// The knob as a flag: the fallback unless the value is 0 or 1, as C++'s get_as<bool>.
PyObject *reader_get_bool(PyObject *object, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    Call call;
    if (!parse_call(args, nargs, kwnames, &call))
        return nullptr;
    if (!PyBool_Check(call.fallback)) {
        PyErr_SetString(PyExc_TypeError, "fallback must be bool");
        return nullptr;
    }
    const std::int64_t fallback = call.fallback == Py_True;
    std::int64_t out = fallback;
    detent_get(reinterpret_cast<ReaderObject *>(object)->handle, call.name, call.hash, call.switch_value, fallback,
               &out);
    return PyBool_FromLong(out == 0 || out == 1 ? out : fallback);
}

PyObject *reader_lookup(PyObject *object, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    Call call;
    std::int64_t fallback = 0;
    if (!parse_call(args, nargs, kwnames, &call) || !integer_fallback(call.fallback, &fallback))
        return nullptr;
    std::int64_t out = fallback;
    const detent_status status = detent_get(reinterpret_cast<ReaderObject *>(object)->handle, call.name, call.hash,
                                            call.switch_value, fallback, &out);
    PyObject *code = status_object(status);
    if (!code)
        return nullptr;
    PyObject *value = PyLong_FromLongLong(out);
    if (!value) {
        Py_DECREF(code);
        return nullptr;
    }
    PyObject *result = PyObject_CallFunctionObjArgs(lookup_type, code, value, nullptr);
    Py_DECREF(code);
    Py_DECREF(value);
    return result;
}

PyObject *reader_refresh(PyObject *object, PyObject *) {
    detent_reader *handle = reinterpret_cast<ReaderObject *>(object)->handle;
    detent_status status;
    Py_BEGIN_ALLOW_THREADS
    status = detent_refresh(handle);
    Py_END_ALLOW_THREADS
    return status_object(status);
}

PyObject *reader_revision(PyObject *object, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames) {
    if (nargs != 1 || kwnames) {
        PyErr_SetString(PyExc_TypeError, "revision(namespace) takes one positional argument");
        return nullptr;
    }
    detent_str name;
    if (!text_arg(args[0], "namespace", false, &name))
        return nullptr;
    std::uint64_t out = 0;
    if (detent_revision(reinterpret_cast<ReaderObject *>(object)->handle, name, &out) != DETENT_OK)
        Py_RETURN_NONE;
    return PyLong_FromUnsignedLongLong(out);
}

PyMethodDef reader_methods[] = {
    {"get", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(reader_get)), METH_FASTCALL | METH_KEYWORDS,
     "get(name, fallback, hash=None, switch_value=None) -> int"},
    {"get_bool", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(reader_get_bool)),
     METH_FASTCALL | METH_KEYWORDS, "get_bool(name, fallback, hash=None, switch_value=None) -> bool"},
    {"lookup", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(reader_lookup)),
     METH_FASTCALL | METH_KEYWORDS, "lookup(name, fallback, hash=None, switch_value=None) -> Lookup"},
    {"refresh", reader_refresh, METH_NOARGS, "refresh() -> Status"},
    {"revision", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(reader_revision)),
     METH_FASTCALL | METH_KEYWORDS, "revision(namespace) -> int | None"},
    {nullptr, nullptr, 0, nullptr}};

PyTypeObject reader_type = {PyVarObject_HEAD_INIT(nullptr, 0)};

int module_exec(PyObject *module) {
    if (detent_abi_version() != DETENT_ABI_VERSION) {
        PyErr_SetString(PyExc_ImportError, "libdetent_reader ABI version differs from this extension's header");
        return -1;
    }
    PyObject *globals = PyModule_GetDict(module);
    if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0)
        return -1;
    PyObject *ran = PyRun_String(prelude().c_str(), Py_file_input, globals, globals);
    if (!ran)
        return -1;
    Py_DECREF(ran);
    status_type = PyDict_GetItemString(globals, "Status");
    error_type = PyDict_GetItemString(globals, "ReaderError");
    lookup_type = PyDict_GetItemString(globals, "Lookup");
    if (!status_type || !error_type || !lookup_type)
        return -1;
    reader_type.tp_name = "detent.Reader";
    reader_type.tp_basicsize = sizeof(ReaderObject);
    reader_type.tp_flags = Py_TPFLAGS_DEFAULT;  // not BASETYPE: no subclassing
    reader_type.tp_doc = "Reader(store, publisher_uid, namespaces=(), mapping_bytes=0): a lock-free reader over an agent's store.";
    reader_type.tp_new = reader_new;
    reader_type.tp_dealloc = reader_dealloc;
    reader_type.tp_methods = reader_methods;
    if (PyType_Ready(&reader_type) < 0)
        return -1;
    Py_INCREF(&reader_type);
    if (PyModule_AddObject(module, "Reader", reinterpret_cast<PyObject *>(&reader_type)) < 0) {
        Py_DECREF(&reader_type);
        return -1;
    }
    return 0;
}

PyModuleDef_Slot module_slots[] = {
    {Py_mod_exec, reinterpret_cast<void *>(module_exec)},
#ifdef Py_mod_multiple_interpreters
    {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
#endif
    {0, nullptr}};

PyModuleDef module_def = {PyModuleDef_HEAD_INIT, "detent", "Detent reader binding (libdetent_reader).", 0, nullptr,
                          module_slots, nullptr, nullptr, nullptr};

}  // namespace

PyMODINIT_FUNC PyInit_detent(void) { return PyModuleDef_Init(&module_def); }
