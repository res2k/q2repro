/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2024 Andrey Nazarov
Copyright (C) 2024 Frank Richter

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "common/intreadwrite.h"
#include "common/msg.h"
#include "common/q2proto_shared.h"

#include "q2proto/q2proto.h"

#if USE_ZLIB
#include <zlib.h>

#define MAX_DEFLATED_SIZE   0x10000

// FIXME: need a mechanism to clean up the stream(s)?
static struct {
    /// Buffer to receive inflated data
    byte buffer[MAX_DEFLATED_SIZE];
    /// zlib stream
    z_stream z;
    /// Whether the deflate stream ended
    bool stream_end;
} io_inflate;

// Sizebuf with inflated data
static sizebuf_t msg_inflate;
// Sizebuf to buffer data to deflate
static sizebuf_t msg_deflate;
static byte deflate_buf[MAX_DEFLATED_SIZE];

#endif // USE_ZLIB

q2protoio_ioarg_t default_q2protoio_ioarg = {.sz_read = &msg_read, .sz_write = &msg_write, .max_msg_len = 1384 /* conservative default */ };

#if USE_ZLIB
static q2protoio_ioarg_t inflate_q2protoio_ioarg = {.sz_read = &msg_inflate, .sz_write = &msg_write};
static q2protoio_ioarg_t deflate_q2protoio_ioarg = {.sz_write = &msg_deflate};

// I/O arg: read from inflated data
#define IOARG_INFLATE      ((uintptr_t)&inflate_q2protoio_ioarg)
// I/O arg: write w/ deflate
#define IOARG_DEFLATE      ((uintptr_t)&deflate_q2protoio_ioarg)
#endif // USE_ZLIB

static byte* io_read_data(uintptr_t io_arg, size_t len, size_t *readcount)
{
#if !USE_ZLIB
    Q_assert(io_arg == _Q2PROTO_IOARG_DEFAULT);
#else
    Q_assert(io_arg == _Q2PROTO_IOARG_DEFAULT || io_arg == IOARG_INFLATE);
#endif
    sizebuf_t *sz = ((q2protoio_ioarg_t*)io_arg)->sz_read;

    if (readcount) {
        len = min(len, sz->cursize - sz->readcount);
        *readcount = len;
        return SZ_ReadData(sz, len);
    } else
        return (byte*)SZ_ReadData(sz, len);
}

uint8_t q2protoio_read_u8(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 1, NULL);
    return buf ? (uint8_t)buf[0] : (uint8_t)-1;
}

uint16_t q2protoio_read_u16(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 2, NULL);
    return buf ? (uint16_t)RL16(buf) : (uint16_t)-1;
}

uint32_t q2protoio_read_u32(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 4, NULL);
    return buf ? (uint32_t)RL32(buf) : (uint32_t)-1;
}

uint64_t q2protoio_read_u64(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 8, NULL);
    return buf ? (uint64_t)RL64(buf) : (uint64_t)-1;
}

q2proto_string_t q2protoio_read_string(uintptr_t io_arg)
{
    q2proto_string_t str = {.str = NULL, .len = 0};
    str.str = (const char*)io_read_data(io_arg, 0, NULL);
    while (1) {
        byte *c = io_read_data(io_arg, 1, NULL);
        if (!c || *c == 0) {
            break;
        }
        str.len++;
    }
    return str;
}

const void* q2protoio_read_raw(uintptr_t io_arg, size_t size, size_t* readcount)
{
    return io_read_data(io_arg, size, readcount);
}

size_t q2protoio_read_available(uintptr_t io_arg)
{
    const q2protoio_ioarg_t *io_data = (const q2protoio_ioarg_t *)io_arg;
    sizebuf_t *sz = io_data->sz_read;
    return sz->cursize - sz->readcount;
}

#if USE_ZLIB

static inline void q2p_inflate_deflate_error(const char* message, int z_error)
{
    Com_Error(ERR_DROP, "%s (%d)", message, z_error);
}

#define Q2PROTO_INFLATE_IMPL_HELPER_API static inline

#include "q2proto/q2proto_deflate_impl_helper.inc"
#include "q2proto/q2proto_inflate_impl_helper.inc"

q2proto_error_t q2protoio_inflate_begin(uintptr_t io_arg, q2proto_inflate_deflate_header_mode_t header_mode, uintptr_t* inflate_io_arg)
{
    if (io_arg != _Q2PROTO_IOARG_DEFAULT) {
        Com_Error(ERR_DROP, "%s: recursively entered", __func__);
    }

    q2proto_error_t err = q2proto_inflate_impl_helper_begin(header_mode, &io_inflate.z);
    io_inflate.stream_end = false;

    *inflate_io_arg = IOARG_INFLATE;
    return err;
}

q2proto_error_t q2protoio_inflate_data(uintptr_t io_arg, uintptr_t inflate_io_arg, size_t compressed_size)
{
    Q_assert(io_arg == _Q2PROTO_IOARG_DEFAULT);
    Q_assert(inflate_io_arg == IOARG_INFLATE);

    byte *in_data;
    if (compressed_size == (size_t)-1)
        in_data = io_read_data(io_arg, SIZE_MAX, &compressed_size);
    else
        in_data = io_read_data(io_arg, compressed_size, NULL);

    unsigned long uncompressed_size = 0;
    q2proto_error_t err = q2proto_inflate_impl_helper_data(&io_inflate.z, in_data, (uint32_t)compressed_size,
                                                           io_inflate.buffer, sizeof(io_inflate.buffer),
                                                           &uncompressed_size, &io_inflate.stream_end);

    SZ_InitRead(&msg_inflate, io_inflate.buffer, sizeof(io_inflate.buffer));
    msg_inflate.cursize = uncompressed_size;

    return err;
}

q2proto_error_t q2protoio_inflate_stream_ended(uintptr_t inflate_io_arg, bool *stream_end)
{
    Q_assert(inflate_io_arg == IOARG_INFLATE);
    *stream_end = io_inflate.stream_end;
    return Q2P_ERR_SUCCESS;
}

q2proto_error_t q2protoio_inflate_end(uintptr_t inflate_io_arg)
{
    Q_assert(inflate_io_arg == IOARG_INFLATE);
    q2proto_error_t err = q2proto_inflate_impl_helper_end(&io_inflate.z);
    if (err != Q2P_ERR_SUCCESS)
        return err;
    return msg_inflate.readcount < msg_inflate.cursize ? Q2P_ERR_MORE_DATA_DEFLATED : Q2P_ERR_SUCCESS;
}

static void* deflate_args_zalloc(voidpf opaque, uInt items, uInt size)
{
    return Z_TagMalloc((size_t)items * size, (memtag_t)(intptr_t)opaque);
}

static void deflate_args_zfree(voidpf opaque, voidpf address)
{
    Z_Free(address);
}

void Q2PROTO_deflate_args_init(q2protoio_deflate_args_t *deflate_args, byte *buffer, unsigned buffer_size, memtag_t z_stream_tag)
{
    q2proto_deflate_impl_helper_alloc_t alloc = {.zalloc = deflate_args_zalloc, .zfree = deflate_args_zfree, .alloc_arg = (voidpf)z_stream_tag};
    q2proto_deflate_impl_helper_init(&deflate_args->defl, &alloc, buffer, buffer_size);
}

void Q2PROTO_deflate_args_destroy(q2protoio_deflate_args_t *deflate_args)
{
    q2proto_deflate_impl_helper_destroy(&deflate_args->defl);
}

q2proto_error_t q2protoio_deflate_begin(q2protoio_deflate_args_t* deflate_args, size_t max_deflated, q2proto_inflate_deflate_header_mode_t header_mode, uintptr_t *deflate_io_arg)
{
    Q_assert(!deflate_q2protoio_ioarg.deflate);

    q2proto_error_t err = q2proto_deflate_impl_helper_begin(&deflate_args->defl, header_mode, deflate_buf);
    if (err != Q2P_ERR_SUCCESS)
        return err;

    SZ_InitWrite(&msg_deflate, deflate_buf, MAX_MSGLEN);

    deflate_q2protoio_ioarg.max_msg_len = max_deflated;
    deflate_q2protoio_ioarg.deflate = deflate_args;
    *deflate_io_arg = IOARG_DEFLATE;

    return Q2P_ERR_SUCCESS;
}

q2proto_error_t q2protoio_deflate_get_data(uintptr_t deflate_io_arg, q2proto_deflate_stream_mode_t stream_mode, size_t *in_size, const void **out, size_t *out_size)
{
    q2protoio_ioarg_t *io_data = (q2protoio_ioarg_t *)deflate_io_arg;
    q2protoio_deflate_args_t *deflate_args = io_data->deflate;

    q2proto_error_t err = q2proto_deflate_impl_helper_get_data(&deflate_args->defl, msg_deflate.cursize, stream_mode, in_size, out, out_size, deflate_buf);
    if (err != Q2P_ERR_SUCCESS)
        return err;

    SZ_Clear(&msg_deflate);

    return Q2P_ERR_SUCCESS;
}

q2proto_error_t q2protoio_deflate_end(uintptr_t deflate_io_arg)
{
    q2protoio_ioarg_t *io_data = (q2protoio_ioarg_t *)deflate_io_arg;

    io_data->deflate = NULL;

    return Q2P_ERR_SUCCESS;
}
#endif // USE_ZLIB

static void* io_reserve_data(uintptr_t io_arg, size_t size)
{
    sizebuf_t *sz = ((q2protoio_ioarg_t*)io_arg)->sz_write;
    return SZ_GetSpace(sz, size);
}

void q2protoio_write_u8(uintptr_t io_arg, uint8_t x)
{
    byte* buf = io_reserve_data(io_arg, 1);
    buf[0] = x;
}

void q2protoio_write_u16(uintptr_t io_arg, uint16_t x)
{
    byte* buf = io_reserve_data(io_arg, 2);
    WL16(buf, x);
}

void q2protoio_write_u32(uintptr_t io_arg, uint32_t x)
{
    byte* buf = io_reserve_data(io_arg, 4);
    WL32(buf, x);
}

void q2protoio_write_u64(uintptr_t io_arg, uint64_t x)
{
    byte* buf = io_reserve_data(io_arg, 8);
    WL64(buf, x);
}

void* q2protoio_write_reserve_raw(uintptr_t io_arg, size_t size)
{
    return io_reserve_data(io_arg, size);
}

void q2protoio_write_raw(uintptr_t io_arg, const void* data, size_t size, size_t *written)
{
    q2protoio_ioarg_t *io_data = (q2protoio_ioarg_t *)io_arg;
    sizebuf_t *sz = io_data->sz_write;

    if (io_data->deflate && written)
    {
        // Deflating as much as possble: write data in a loop
        const byte *data_bytes = data;
        size_t in_remaining = size;
        size_t out_remaining = q2protoio_write_available(io_arg);
        size_t in_consumed = 0;
        while (in_remaining > 0 && out_remaining > 0)
        {
            size_t chunk_size = min(in_remaining, out_remaining);
            void* p = SZ_GetSpace(sz, chunk_size);
            memcpy(p, data_bytes, chunk_size);
            in_consumed += chunk_size;
            data_bytes += chunk_size;
            in_remaining -= chunk_size;
            out_remaining = q2protoio_write_available(io_arg);
        }
        *written = in_consumed;
    }
    else
    {
        // Simple case: not deflating, or requiring all data to be deflated
        size_t buf_remaining = sz->maxsize - sz->cursize;
        size_t write_size = written ? min(buf_remaining, size) : size;
        void* p = SZ_GetSpace(sz, write_size);
        memcpy(p, data, write_size);
        if (written)
            *written = write_size;
    }
}

size_t q2protoio_write_available(uintptr_t io_arg)
{
    const q2protoio_ioarg_t *io_data = (const q2protoio_ioarg_t *)io_arg;
    sizebuf_t *sz = io_data->sz_write;
#if USE_ZLIB
    if (io_data->deflate)
    {
        return q2proto_deflate_impl_helper_remaining(&io_data->deflate->defl, sz->cursize, io_data->max_msg_len);
    }
    else
#endif // USE_ZLIB
    {
        return io_data->max_msg_len - min(sz->cursize, io_data->max_msg_len);
    }
}

bool nonfatal_client_read_errors = false;

q2proto_error_t q2protoerr_client_read(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    if (nonfatal_client_read_errors)
        Com_WPrintf("%s\n", buf);
    else
        Com_Error(ERR_DROP, "%s", buf);
    return err;
}

q2proto_error_t q2protoerr_client_write(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    Com_EPrintf("client write error: %s\n", buf);
    return err;
}

q2proto_error_t q2protoerr_server_write(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    Com_EPrintf("server write error: %s\n", buf);
    return err;
}

q2proto_error_t q2protoerr_server_read(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    Com_EPrintf("server read error: %s\n", buf);
    return err;
}

#if Q2PROTO_SHOWNET
extern cvar_t   *cl_shownet;

bool q2protodbg_shownet_check(uintptr_t io_arg, int level)
{
    return cl_shownet->integer > level;
}

void q2protodbg_shownet(uintptr_t io_arg, int level, int offset, const char *msg, ...)
{
    if (cl_shownet->integer > level)
    {
        q2protoio_ioarg_t *io_data = (q2protoio_ioarg_t *)io_arg;
        char buf[256];
        va_list argptr;

    #if USE_ZLIB
        bool is_deflate = io_arg == IOARG_INFLATE;
    #else
        bool is_deflate = false;
    #endif
        const char *offset_suffix = is_deflate ? "[z]" : "";

        va_start(argptr, msg);
        Q_vsnprintf(buf, sizeof(buf), msg, argptr);
        va_end(argptr);

        Com_LPrintf(PRINT_DEVELOPER, "%3u%s:%s\n", io_data->sz_read->readcount + offset, offset_suffix, buf);
    }
}
#endif
