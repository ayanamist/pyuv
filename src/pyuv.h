
#ifndef PYUV_H
#define PYUV_H

/* python */
#include "Python.h"
#include "structmember.h"

/* system */
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/* libuv */
#include "uv.h"


/* Custom types */
typedef int Bool;
#define True  1
#define False 0


/* Utility macros */
#define SELF_LOOP self->loop->uv_loop
#define SERVER_LOOP self->server->loop->uv_loop

#define RAISE_ERROR(loop, exc, ret)                 \
    do {                                            \
        uv_err_t err = uv_last_error(loop);         \
        PyErr_SetString(exc, uv_strerror(err));     \
        return ret;                                 \
    } while (0)                                     \


/* Python types definitions */
/* Loop */
typedef struct {
    PyObject_HEAD
    uv_loop_t *uv_loop;
    int is_default;
} Loop;

static PyTypeObject LoopType;

/* Async */
typedef struct {
    PyObject_HEAD
    Loop *loop;
    PyObject *callback;
    PyObject *data;
    uv_async_t *uv_async;
} Async;

static PyTypeObject AsyncType;

/* Timer */
typedef struct {
    PyObject_HEAD
    Loop *loop;
    PyObject *callback;
    PyObject *data;
    uv_timer_t *uv_timer;
    int64_t timeout;
    int64_t repeat;
} Timer;

static PyTypeObject TimerType;

/* TCPServer */
typedef struct {
    PyObject_HEAD
    Loop *loop;
    PyObject *listen_address;
    PyObject *on_new_connection_cb;
    uv_tcp_t *uv_tcp_server;
    char *listen_ip;
    int listen_port;
    int address_type;
} TCPServer;

static PyTypeObject TCPServerType;

/* TCPConnection */
typedef struct {
    PyObject_HEAD
    TCPServer *server;
    PyObject *on_read_cb;
    PyObject *on_write_cb;
    PyObject *on_close_cb;
    uv_stream_t *uv_stream;
} TCPConnection;

static PyTypeObject TCPConnectionType;

/* UDPServer */
typedef struct {
    PyObject_HEAD
    Loop *loop;
    PyObject *listen_address;
    PyObject *on_read_cb;
    PyObject *on_write_cb;
    uv_udp_t *uv_udp_server;
    char *listen_ip;
    int listen_port;
    int address_type;
} UDPServer;

static PyTypeObject UDPServerType;

#endif

