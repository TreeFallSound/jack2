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

#include "JackWorkgroup.h"
#include "JackError.h"

#include <CoreAudio/CoreAudio.h>
#include <os/workgroup.h>

/*
    Per-thread membership state. A JACK client thread joins once, in
    SetupRealTime, and holds the membership for its whole life.
*/
static __thread os_workgroup_t gWorkgroup = NULL;
static __thread os_workgroup_join_token_s gJoinToken;
static __thread int gJoined = 0;

extern "C" int JackWorkgroupJoinSelfForDevice(uint32_t device_id)
{
    if (gJoined) {
        return EALREADY;
    }
    if (device_id == 0) {
        // The backend has not published its device yet, or the backend is not
        // coreaudio. Nothing to join; the caller keeps plain realtime.
        return ENODEV;
    }

    if (__builtin_available(macOS 11.0, *)) {
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyIOThreadOSWorkgroup,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        os_workgroup_t wg = NULL;
        UInt32 size = sizeof(wg);
        OSStatus err = AudioObjectGetPropertyData((AudioObjectID)device_id,
                                                  &addr, 0, NULL, &size, &wg);
        if (err != noErr || wg == NULL) {
            jack_error("JackWorkgroup: device %u has no IOThreadOSWorkgroup (OSStatus = %d)",
                       (unsigned)device_id, (int)err);
            return err != noErr ? (int)err : ENOENT;
        }

        // Requires THREAD_TIME_CONSTRAINT_POLICY on this thread already; the
        // caller guarantees it by joining after AcquireSelfRealTime.
        int rc = os_workgroup_join(wg, &gJoinToken);
        if (rc != 0) {
            jack_error("JackWorkgroup: os_workgroup_join failed rc = %d", rc);
            os_release(wg);
            return rc;
        }

        gWorkgroup = wg;
        gJoined = 1;
        jack_info("JackWorkgroup: realtime thread joined the workgroup of device %u",
                  (unsigned)device_id);
        return 0;
    }

    return ENOTSUP;
}

extern "C" void JackWorkgroupLeaveSelf(void)
{
    if (!gJoined) {
        return;
    }
    if (__builtin_available(macOS 11.0, *)) {
        os_workgroup_leave(gWorkgroup, &gJoinToken);
        os_release(gWorkgroup);
    }
    gWorkgroup = NULL;
    gJoined = 0;
}
