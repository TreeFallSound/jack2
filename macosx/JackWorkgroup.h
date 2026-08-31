/*
Copyright (C) 2026 Treefall Sound

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

*/

/*
    CoreAudio workgroup membership for JACK client realtime threads.

    Every CoreAudio device publishes an os_workgroup_t via
    kAudioDevicePropertyIOThreadOSWorkgroup. Threads that join it are scheduled
    by the kernel as co-deadline with that device's IO thread, which is
    materially stronger protection against unrelated CPU pressure (WindowServer,
    a hypervisor) than plain time-constraint realtime alone.

    jackd's coreaudio backend cycle runs on an AudioUnit render callback, so
    CoreAudio has already placed *that* thread in the backend device's
    workgroup. It does not propagate to the graph's client threads, which jack2
    spawns itself -- so every JACK client (netmanager's per-slave clients
    included) runs unprotected and can miss the cycle deadline under load.
    This module closes that gap.

    Two undocumented constraints shape the API:

      1. os_workgroup_join returns EINVAL on a thread that is not realtime by
         Mach's definition. THREAD_TIME_CONSTRAINT_POLICY must already be set.
         Call this only after JackThread::AcquireSelfRealTime has run.
      2. The join is per-thread and must happen ON the thread that will
         participate. A different thread cannot join on its behalf.

    The join token is kept in thread-local storage so callers need no state and
    no Apple headers leak into the cross-platform sources.
*/

#ifndef __JackWorkgroup__
#define __JackWorkgroup__

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*!
\brief Join the calling thread to the CoreAudio workgroup of device_id.

\param device_id An AudioDeviceID, valid process-wide. 0 means "not published
       yet" and is treated as a no-op failure.
\return 0 on success, non-zero on any failure (no device, property absent,
        OS too old, already joined, join refused).

Never fatal: a failure means the thread keeps the scheduling it already had.
*/
int JackWorkgroupJoinSelfForDevice(uint32_t device_id);

/*!
\brief Leave the workgroup this thread joined, if any. Idempotent.

Must run on the thread that joined, and must run before that thread ends.
Membership does NOT drop by itself: a thread that ends while it is a member
makes libdispatch raise EXC_BREAKPOINT in _os_workgroup_tsd_cleanup, which
stops the process. JackPosixThread::ThreadHandler calls this from a
cancellation handler for that reason.
*/
void JackWorkgroupLeaveSelf(void);

#ifdef __cplusplus
}
#endif

#endif
