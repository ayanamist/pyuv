
static INLINE void
free_handle(uv_handle_t *handle)
{
    ASSERT(handle);
    PyMem_Free(handle);
}


static void
on_handle_close(uv_handle_t *handle)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    Handle *self;
    PyObject *result;
    ASSERT(handle);

    self = (Handle *)handle->data;
    ASSERT(self);

    if (self->on_close_cb != Py_None) {
        result = PyObject_CallFunctionObjArgs(self->on_close_cb, self, NULL);
        if (result == NULL) {
            handle_uncaught_exception(self->loop);
        }
        Py_XDECREF(result);
    }

    Py_DECREF(self->on_close_cb);
    self->on_close_cb = NULL;

    Py_DECREF(self->loop);
    self->loop = (Loop *)Py_None;
    Py_INCREF(Py_None);

    /* Refcount was increased in the caller function */
    Py_DECREF(self);

    PyGILState_Release(gstate);
}


static void
on_handle_dealloc_close(uv_handle_t *handle)
{
    PyGILState_STATE gstate = PyGILState_Ensure();

    free_handle(handle);

    PyGILState_Release(gstate);
}


static void
initialize_handle(Handle *self, Loop *loop)
{
    PyObject *tmp;
    tmp = (PyObject *)self->loop;
    Py_INCREF(loop);
    self->loop = loop;
    Py_XDECREF(tmp);
    self->initialized = True;
}


static PyObject *
Handle_func_ref(Handle *self)
{
    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);
    RAISE_IF_HANDLE_CLOSED(self, PyExc_HandleClosedError, NULL);
    uv_ref(self->uv_handle);
    Py_RETURN_NONE;
}


static PyObject *
Handle_func_unref(Handle *self)
{
    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);
    RAISE_IF_HANDLE_CLOSED(self, PyExc_HandleClosedError, NULL);
    uv_unref(self->uv_handle);
    Py_RETURN_NONE;
}


static PyObject *
Handle_func_close(Handle *self, PyObject *args)
{
    PyObject *callback = Py_None;

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);
    RAISE_IF_HANDLE_CLOSED(self, PyExc_HandleClosedError, NULL);

    if (!PyArg_ParseTuple(args, "|O:close", &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable or None is required");
        return NULL;
    }

    Py_INCREF(callback);
    self->on_close_cb = callback;

    /* Increase refcount so that object is not removed before the callback is called */
    Py_INCREF(self);

    uv_close(self->uv_handle, on_handle_close);

    Py_RETURN_NONE;
}


static PyObject *
Handle_active_get(Handle *self, void *closure)
{
    UNUSED_ARG(closure);

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);

    return PyBool_FromLong((long)uv_is_active(self->uv_handle));
}


static PyObject *
Handle_closed_get(Handle *self, void *closure)
{
    UNUSED_ARG(closure);

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);

    return PyBool_FromLong((long)uv_is_closing(self->uv_handle));
}


static PyObject *
Handle_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Handle *self = (Handle *)PyType_GenericNew(type, args, kwargs);
    if (!self) {
        return NULL;
    }
    self->initialized = False;
    self->uv_handle = NULL;
    self->weakreflist = NULL;
    return (PyObject *)self;
}


static int
Handle_tp_traverse(Handle *self, visitproc visit, void *arg)
{
    Py_VISIT(self->on_close_cb);
    Py_VISIT(self->loop);
    Py_VISIT(self->dict);
    return 0;
}


static int
Handle_tp_clear(Handle *self)
{
    Py_CLEAR(self->on_close_cb);
    Py_CLEAR(self->loop);
    Py_CLEAR(self->dict);
    return 0;
}


static void
Handle_tp_dealloc(Handle *self)
{
    ASSERT(self->uv_handle);
    self->uv_handle->data = NULL;
    if (self->initialized && !uv_is_closing(self->uv_handle)) {
        uv_close(self->uv_handle, on_handle_dealloc_close);
    } else {
        /* Refcount is increased in close(), so it's guaranteed that if we arrived here and the user had called close(),
         * the callback was already executed and it's safe to free the handle */
        free_handle(self->uv_handle);
    }
    if (self->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject *)self);
    }
    Py_TYPE(self)->tp_clear((PyObject *)self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}


static PyObject*
Handle_dict_get(Handle *self, void* c)
{
    UNUSED_ARG(c);

    if (self->dict == NULL) {
        self->dict = PyDict_New();
        if (self->dict == NULL) {
            return NULL;
        }
    }
    Py_INCREF(self->dict);
    return self->dict;
}


static int
Handle_dict_set(Handle *self, PyObject* val, void* c)
{
    PyObject* tmp;

    UNUSED_ARG(c);

    if (val == NULL) {
        PyErr_SetString(PyExc_TypeError, "__dict__ may not be deleted");
        return -1;
    }
    if (!PyDict_Check(val)) {
        PyErr_SetString(PyExc_TypeError, "__dict__ must be a dictionary");
        return -1;
    }
    tmp = self->dict;
    Py_INCREF(val);
    self->dict = val;
    Py_XDECREF(tmp);
    return 0;
}


static PyMethodDef
Handle_tp_methods[] = {
    { "ref", (PyCFunction)Handle_func_ref, METH_NOARGS, "Increase the event loop reference count." },
    { "unref", (PyCFunction)Handle_func_unref, METH_NOARGS, "Decrease the event loop reference count." },
    { "close", (PyCFunction)Handle_func_close, METH_VARARGS, "Close handle." },
    { NULL }
};


static PyMemberDef Handle_tp_members[] = {
    {"loop", T_OBJECT_EX, offsetof(Handle, loop), READONLY, "Loop where this handle belongs."},
    {NULL}
};


static PyGetSetDef Handle_tp_getsets[] = {
    {"__dict__", (getter)Handle_dict_get, (setter)Handle_dict_set, NULL},
    {"active", (getter)Handle_active_get, NULL, "Indicates if this handle is active.", NULL},
    {"closed", (getter)Handle_closed_get, NULL, "Indicates if this handle is closing or already closed.", NULL},
    {NULL}
};


static PyTypeObject HandleType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.Handle",                                                 /*tp_name*/
    sizeof(Handle),                                                /*tp_basicsize*/
    0,                                                             /*tp_itemsize*/
    (destructor)Handle_tp_dealloc,                                 /*tp_dealloc*/
    0,                                                             /*tp_print*/
    0,                                                             /*tp_getattr*/
    0,                                                             /*tp_setattr*/
    0,                                                             /*tp_compare*/
    0,                                                             /*tp_repr*/
    0,                                                             /*tp_as_number*/
    0,                                                             /*tp_as_sequence*/
    0,                                                             /*tp_as_mapping*/
    0,                                                             /*tp_hash */
    0,                                                             /*tp_call*/
    0,                                                             /*tp_str*/
    0,                                                             /*tp_getattro*/
    0,                                                             /*tp_setattro*/
    0,                                                             /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,                       /*tp_flags*/
    0,                                                             /*tp_doc*/
    (traverseproc)Handle_tp_traverse,                              /*tp_traverse*/
    (inquiry)Handle_tp_clear,                                      /*tp_clear*/
    0,                                                             /*tp_richcompare*/
    offsetof(Handle, weakreflist),                                 /*tp_weaklistoffset*/
    0,                                                             /*tp_iter*/
    0,                                                             /*tp_iternext*/
    Handle_tp_methods,                                             /*tp_methods*/
    Handle_tp_members,                                             /*tp_members*/
    Handle_tp_getsets,                                             /*tp_getsets*/
    0,                                                             /*tp_base*/
    0,                                                             /*tp_dict*/
    0,                                                             /*tp_descr_get*/
    0,                                                             /*tp_descr_set*/
    offsetof(Handle, dict),                                        /*tp_dictoffset*/
    0,                                                             /*tp_init*/
    0,                                                             /*tp_alloc*/
    Handle_tp_new,                                                 /*tp_new*/
};

