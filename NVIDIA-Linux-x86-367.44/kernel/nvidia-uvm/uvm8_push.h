/*******************************************************************************
    Copyright (c) 2015 NVIDIA Corporation

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

        The above copyright notice and this permission notice shall be
        included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

*******************************************************************************/

#ifndef __UVM8_PUSH_H__
#define __UVM8_PUSH_H__

#include "uvm8_forward_decl.h"
#include "uvm8_hal_types.h"
#include "uvm8_channel.h"
#include "uvm8_push_macros.h"
#include "uvm8_tracker.h"
#include "nvtypes.h"

// Space uvm_push_end() takes in the pushbuffer.
// Currently uvm_push_end() does ce_hal->semaphore_release() that takes (4 + 2)
// * 4 space. This is mostly useful for tests and there is also a test that
// verifies that it's correct.
#define UVM_PUSH_END_SIZE 24

// The max amount of inline push data is limited by how much space can be jumped
// over with a single NOOP method.
#define UVM_PUSH_INLINE_DATA_MAX_SIZE (UVM_METHOD_COUNT_MAX * UVM_METHOD_SIZE)

typedef enum
{
    // By default all CE transfers are not pipelined.
    // This flag indicates that next CE transfer should be pipelined
    UVM_PUSH_FLAG_CE_NEXT_PIPELINED,

    // By default all CE operations include a membar sys after any transfer and
    // before a semaphore operation.
    // This flag indicates that next operation should use no membar at all.
    UVM_PUSH_FLAG_CE_NEXT_MEMBAR_NONE,

    // By default all CE operations include a membar sys after any transfer and
    // before a semaphore operation.
    // This flag indicates that next operation should use a membar gpu instead.
    UVM_PUSH_FLAG_CE_NEXT_MEMBAR_GPU,

    UVM_PUSH_FLAG_COUNT,
} uvm_push_flag_t;

struct uvm_push_struct
{
    // Location of the first method of the push
    NvU32 *begin;

    // Location of the next method to be written
    NvU32 *next;

    // The GPU the push is being done on
    uvm_gpu_t *gpu;

    // The channel the push is being done on or has been finished on
    uvm_channel_t *channel;

    // The tracking value when the push completes on the GPU on the channel
    // above. It will be 0 for an on-going push.
    NvU64 channel_tracking_value;

    // Index for the push info stored within the channel.
    // Only valid for an on-going push (after uvm_push_begin*(), but before
    // uvm_push_end()).
    NvU32 push_info_index;

    // A bitmap of flags from uvm_push_flag_t
    DECLARE_BITMAP(flags, UVM_PUSH_FLAG_COUNT);
};

struct uvm_push_info_struct
{
    // Filename where the push was started
    const char *filename;

    // Line number where the push was started
    int line;

    // Function where the push was started
    const char *function;

    // Description of the push created from the uvm_push_begin*() format and arguments.
    char description[128];

    // Procedure to be called when the corresponding push is complete.
    // This procedure is called with the UVM_LOCK_ORDER_CHANNEL spin lock held.
    void (*on_complete)(void *);
    void *on_complete_data;
};

typedef struct
{
    // The push the inline data is part of
    uvm_push_t *push;

    // Location of the next data to be written
    char *next_data;
} uvm_push_inline_data_t;

// Internal implementation used by uvm_push_begin() and uvm_push_begin_acquire()
NV_STATUS __uvm_push_begin_acquire(uvm_channel_manager_t *manager, uvm_channel_type_t type, uvm_tracker_t *deps, uvm_push_t *push);

// Internal implementation used by uvm_push_begin_on_channel()
NV_STATUS __uvm_push_begin_on_channel(uvm_channel_t *channel, uvm_push_t *push);

// Is tracking push descriptions enabled?
bool uvm_push_info_is_tracking_descriptions(void);

// Internal helper to fill info push info as part of beginning a push.
void __uvm_push_fill_info(uvm_push_t *push, const char *filename, const char *function, int line, const char *format, va_list args);

// Internal helper for uvm_push_begin() and uvm_push_begin_acquire()
__attribute__ ((format(printf, 8, 9)))
static NV_STATUS __uvm_push_begin_acquire_with_info(uvm_channel_manager_t *manager, uvm_channel_type_t type, uvm_tracker_t *tracker,
        uvm_push_t *push,
        const char *filename, const char *function, int line, const char *format, ...)
{
    va_list args;

    NV_STATUS status = __uvm_push_begin_acquire(manager, type, tracker, push);
    if (status != NV_OK)
        return status;

    va_start(args, format);
    __uvm_push_fill_info(push, filename, function, line, format, args);
    va_end(args);

    return NV_OK;
}

// Internal helper for uvm_push_begin_on_channel()
__attribute__ ((format(printf, 6, 7)))
static NV_STATUS __uvm_push_begin_on_channel_with_info(uvm_channel_t *channel,
        uvm_push_t *push,
        const char *filename, const char *function, int line, const char *format, ...)
{
    va_list args;

    NV_STATUS status = __uvm_push_begin_on_channel(channel, push);
    if (status != NV_OK)
        return status;

    va_start(args, format);
    __uvm_push_fill_info(push, filename, function, line, format, args);
    va_end(args);

    return NV_OK;
}

// Begin a push on a channel of channel_type type
// Picks the first available channel. If all channels of the given type are
// busy, spin waits for one to become available.
//
// Notably channel_type can be UVM_CHANNEL_TYPE_ANY and that will pick any
// available channel.
//
// Notably requires a description of the push to be provided. This is currently
// unused, but will be in the future for tracking push history.
//
// Locking: on success acquires the concurrent push semaphore until uvm_push_end()
#define uvm_push_begin(manager, type, push, format, ...)                            \
    __uvm_push_begin_acquire_with_info((manager), (type), NULL, (push), \
        __FILE__, __FUNCTION__, __LINE__, (format), ##__VA_ARGS__)

// Begin a push on a channel of channel_type type with dependencies in the tracker
// This is equivalent to starting a push and acquiring the tracker, but in the
// future it will have the ability to pick the channel to do a push on in a
// smarter way based on its dependencies.
//
// Same as for uvm_push_acquire_tracker(), the tracker can be NULL. In this case
// this will be equivalent to just uvm_push_begin().
//
// Locking: on success acquires the concurrent push semaphore until uvm_push_end()
#define uvm_push_begin_acquire(manager, type, tracker, push, format, ...)       \
    __uvm_push_begin_acquire_with_info((manager), (type), (tracker), (push),    \
        __FILE__, __FUNCTION__, __LINE__, (format), ##__VA_ARGS__)

// Begin a push on a specific channel
// If the channel is busy, spin wait for it to become available.
//
// Locking: on success acquires the concurrent push semaphore until uvm_push_end()
#define uvm_push_begin_on_channel(channel, push, format, ...)                 \
    __uvm_push_begin_on_channel_with_info((channel), (push),                  \
        __FILE__, __FUNCTION__, __LINE__, (format), ##__VA_ARGS__)

// End a push
// Finishes the push and submits the methods to the GPU.
//
// This will always release the channel tracking semaphore with CE and that
// release can be affected by setting the push flags (commonly
// UVM_PUSH_FLAGS_CE_NEXT_FLUSH) prior to calling uvm_push_end().
//
// Notably doesn't wait for the push to complete on the GPU and is also
// guaranteed not to block waiting on any other GPU work to complete. The only
// contention that can happen is with other CPU threads updating channel and/or
// pushbuffer state, but all of these updates are expected to be fast.
//
// Completion of the push on the GPU can be tracked with a tracker by using
// uvm_tracker_add_push() or can be waited on directly with uvm_push_wait().
// Also see uvm_push_end_and_wait() that combines ending and waiting for a push.
//
// Locking: releases the concurrent push semaphore acquired in uvm_push_begin*()
void uvm_push_end(uvm_push_t *push);

// Wait for a push to complete its execution on the GPU.
//
// The push has to be finished prior to calling this function.
// Notably currently this will only check for errors on the channel the push has
// been made on while waiting for it to complete.
NV_STATUS uvm_push_wait(uvm_push_t *push);

// End a push and wait for it to complete execution on the GPU
// Shortcut for uvm_push_end() and uvm_push_wait().
NV_STATUS uvm_push_end_and_wait(uvm_push_t *push);

// Get the tracker entry tracking the push
// The push has to be finished before calling this function.
void uvm_push_get_tracker_entry(uvm_push_t *push, uvm_tracker_entry_t *entry);

// Acquire all the entries in the tracker.
// Subsequently pushed GPU work will not start before all the work tracked by
// tracker is complete.
// Notably a NULL tracker is handled the same way as an empty tracker.
void uvm_push_acquire_tracker(uvm_push_t *push, uvm_tracker_t *tracker);

// Acquire a single tracker entry
// Subsequently pushed GPU work will not start before the work tracked by
// tracker entry is complete.
void uvm_push_acquire_tracker_entry(uvm_push_t *push, uvm_tracker_entry_t *tracker_entry);

// Set a push flag
void uvm_push_set_flag(uvm_push_t *push, uvm_push_flag_t flag);

// Get and reset (if set) a push flag
bool uvm_push_get_and_reset_flag(uvm_push_t *push, uvm_push_flag_t flag);

// Get the size of the push so far
static NvU32 uvm_push_get_size(uvm_push_t *push)
{
    return (push->next - push->begin) * sizeof(*push->next);
}

// Check whether the push still has free_space bytes available to be pushed
static bool uvm_push_has_space(uvm_push_t *push, NvU32 free_space)
{
    return (UVM_MAX_PUSH_SIZE - uvm_push_get_size(push)) >= free_space;
}

// Fake push begin and end
//
// These do just enough for inline push data and uvm_push_get_gpu() to work.
// Used by tests that run on fake GPUs without a channel manager (see
// uvm8_page_tree_test.c for an example).
NV_STATUS uvm_push_begin_fake(uvm_gpu_t *gpu, uvm_push_t *push);
void uvm_push_end_fake(uvm_push_t *push);

// Begin an inline data fragment in the push
//
// The inline data will be ignored by the GPU, but can be referenced from
// subsequent commands via its GPU virtual address that's returned by
// uvm_push_inline_data_end().
// Up to UVM_PUSH_INLINE_DATA_MAX_SIZE bytes can be added inline in the push
// with various helpers below. The start of the data is guaranteed to be
// initially aligned to UVM_METHOD_SIZE (4).
// While an inline data fragment is on-going (after inline_data_begin() but
// before inline_data_end()) no other commands should be issued in the push.
//
// Also see uvm_push_get_single_inline_buffer() for a simple way of adding a
// specified amount of data in one step.
void uvm_push_inline_data_begin(uvm_push_t *push, uvm_push_inline_data_t *data);

// End an line data fragment in the push
//
// Returns back the GPU address of the beginning of the inline data fragment.
uvm_gpu_address_t uvm_push_inline_data_end(uvm_push_inline_data_t *data);

// Get the current size of the on-going inline data fragment.
//
// Can only be used while an inline data fragment is on-going.
size_t uvm_push_inline_data_size(uvm_push_inline_data_t *data);

// Get a buffer of size bytes of inline data in the push
//
// Returns the CPU pointer to the beginning of the new size bytes of data that
// the caller is supposed to write. The buffer can be accessed as long as the
// push is on-going.
void *uvm_push_inline_data_get(uvm_push_inline_data_t *data, size_t size);

// Same as uvm_push_inline_data_get() but provides the specified alignment.
void *uvm_push_inline_data_get_aligned(uvm_push_inline_data_t *data, size_t size, size_t alignment);

// Get a single buffer of size bytes of inline data in the push
//
// Returns the CPU pointer to the beginning of the buffer. The buffer can be
// accessed as long as the push is on-going. Also returns the GPU address of the
// buffer that can be accessed by commands in the same push.
//
// This is a wrapper around uvm_push_inline_data_begin() and
// uvm_push_inline_data_end() so see their comments for more details.
void *uvm_push_get_single_inline_buffer(uvm_push_t *push, size_t size, uvm_gpu_address_t *gpu_address);

// Same as uvm_push_get_single_inline_buffer() but provides the specified alignment.
void *uvm_push_get_single_inline_buffer_aligned(uvm_push_t *push, size_t size, size_t alignment, uvm_gpu_address_t *gpu_address);

// Helper that copies size bytes of data from src into the inline data fragment
static void uvm_push_inline_data_add(uvm_push_inline_data_t *data, const void *src, size_t size)
{
    memcpy(uvm_push_inline_data_get(data, size), src, size);
}

// Helper that copies 4 bytes of data given by value into the inline data fragment
static void uvm_push_inline_data_add_4(uvm_push_inline_data_t *data, NvU32 value)
{
    uvm_push_inline_data_add(data, &value, sizeof(value));
}

// Helper that copies 8 bytes of data given by value into the inline data fragment
static void uvm_push_inline_data_add_8(uvm_push_inline_data_t *data, NvU64 value)
{
    uvm_push_inline_data_add(data, &value, sizeof(value));
}

// Push an operation releasing a timestamp into the pushbuffer.
//
// Returns the CPU pointer into the pushbuffer where the timestamp is going to
// be written. The timestamp can be accessed from the on_complete callback of
// the push.
NvU64 *uvm_push_timestamp(uvm_push_t *push);

static uvm_gpu_t *uvm_push_get_gpu(uvm_push_t *push)
{
    UVM_ASSERT(push->gpu);

    return push->gpu;
}

// Retrieve the push info object for a push that has already started
static uvm_push_info_t *uvm_push_info_from_push(uvm_push_t *push)
{
    uvm_channel_t *channel = push->channel;

    UVM_ASSERT(push->channel != NULL);
    UVM_ASSERT(push->channel_tracking_value == 0);

    UVM_ASSERT_MSG(push->push_info_index < channel->channel_info.numGpFifoEntries, "index %u\n", push->push_info_index);

    return &push->channel->push_infos[push->push_info_index];
}

#endif // __UVM8_PUSH_H__