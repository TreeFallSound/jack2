# The netmanager master is not reaped when the slave goes away

## The symptom

A netJACK2 slave stops. The cause can be a clean stop, a hard kill, or a
yanked cable. The master side does not recover.

The dead master client stays registered in the server. Its ports stay
connected. Every audio cycle then waits for a packet that never comes. The
wait is `PACKET_TIMEOUT * NETWORK_DEFAULT_LATENCY` = 2 seconds. The audio
budget at 48 kHz / 128 frames is 2.67 milliseconds. The failure is permanent
and silent.

If the slave restarts, a second master is created. The graph fills with
`pistomp-01`, `pistomp-02`, and more. Each one fights for the same ports.

## The cause

There are four defects. Each one is on its own path.

### 1. `FatalRecvError` kills the RT thread

`JackNetMasterInterface::FatalRecvError` and `FatalSendError` call
`ThreadExit()`. That code runs as the JACK process callback, on the real-time
graph thread (`JackNetMaster::Process`). `ThreadExit()` ends that thread.

The client is still registered. Nothing services it. The server still calls
its cycle, and the cycle waits out the full `PACKET_TIMEOUT` every time. A
comment in the source calls this "an UGLY temporary way".

### 2. `KillMaster` is a use-after-free

`JackNetMasterManager::KillMaster` does this:

    fMasterList.erase(master_it);
    delete (*master_it);

`erase()` makes the iterator not valid. The next line reads it. The `delete`
then acts on whatever that read produced. So the multicast `KILL_MASTER`
path — the one clean stop uses — does not reap the master either. It corrupts
the heap.

### 3. `InitMaster` does not check for an existing slave

`JackNetMasterManager::InitMaster` creates a new `JackNetMaster` for every
`SLAVE_AVAILABLE` packet. It does not look for a master that already holds
that slave name. A slave that restarts, or a fast Ethernet Audio toggle on
the pedal, sends a fresh `SLAVE_AVAILABLE` while the old master is still in
the list. A duplicate is the result.

### 4. The slave does not pin its multicast interface

`JackNetAdapter` calls `fSocket.SetMulticastIF()` before `NewSocket()`. The
socket does not exist yet, so the `setsockopt` acts on file descriptor 0.
`NewSocket()` never re-applies the option. The slave's reconnect loop builds
a new socket on every attempt, and none of them is pinned.

A leaked netadapter then sends its `SLAVE_AVAILABLE` announcements out the
default route. On a host with the cable on one interface and Wi-Fi on
another, teardown deletes the cable's route, and the announcements go out
**over Wi-Fi**.

## The correction

### 1. Reap off the RT thread

`FatalRecvError` and `FatalSendError` set an atomic flag `fDead` and return.
They do not call `ThreadExit()`. `Exit()` still runs, so `fRunning` becomes
false and the multicast euthanasia request is still sent.

`JackNetMasterManager::Run` calls `ReapDeadMasters()` at the top of its loop.
That function walks the master list and destroys every master whose
`IsDead()` is true. The loop wakes at least every `MANAGER_INIT_TIMEOUT`
(2 seconds), so a silent network is still handled.

### 2. Capture the pointer before the erase

A new function `RemoveMaster(master_list_it_t)` reads the pointer, then
erases, then deletes:

    JackNetMaster* master = *master_it;
    fMasterList.erase(master_it);
    delete master;

`KillMaster`, `ReapDeadMasters`, and the dedupe path all use it.

### 3. Dedupe by slave name

`InitMaster` walks the master list first. It reaps every master whose
`fParams.fName` equals the new slave's name. Then it creates the new master.

The `sleep 3` in the pi-Stomp `jackbridge-pi-up` helper was a workaround for
this defect. It can be removed once this correction is in the field.

### 4. Re-apply the multicast interface on every socket

`JackNetUnixSocket` stores the interface name in `fMcastIF`. `SetMulticastIF`
records the name and applies it only if the socket already exists.
`NewSocket()` re-applies it, through a new private `ApplyMulticastIF()`, and
logs an error if the interface is not found.

## Note

Defect 1 also returns `SOCKET_ERROR` from the process callback. The server
may deactivate the client for that. This is acceptable: `fRunning` is already
false, so the next cycle returns 0 at once, and the manager reaps the master
within one loop pass.

The slave reconnect loop (`JackNetSlaveInterface::Init`) stays unbounded. A
legitimate slave waits there for its master. Defect 3's correction is what
makes an unbounded wait safe on the master side.

The Windows socket (`JackNetWinSocket`) is not changed. It is not built for
the pi-Stomp targets. Its `SetMulticastIF` keeps the apply-at-call behavior.
