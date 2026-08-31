/*
Copyright (C) 2008-2011 Romain Moret at Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __JACKNETMANAGER_H__
#define __JACKNETMANAGER_H__

#include "JackNetInterface.h"
#include "jack.h"
#include <list>
#include <map>

namespace Jack
{
    class JackNetMasterManager;

    /**
    \Brief This class describes a Net Master
    */

    typedef std::list<std::pair<std::string, std::string> > connections_list_t;

    class JackNetMaster : public JackNetMasterInterface
    {
            friend class JackNetMasterManager;

        private:

            static int SetProcess(jack_nframes_t nframes, void* arg);
            static int SetBufferSize(jack_nframes_t nframes, void* arg);
            static int SetSampleRate(jack_nframes_t nframes, void* arg);
            static void SetTimebaseCallback(jack_transport_state_t state, jack_nframes_t nframes, jack_position_t* pos, int new_pos, void* arg);
            static void SetConnectCallback(jack_port_id_t a, jack_port_id_t b, int connect, void* arg);
            static void LatencyCallback(jack_latency_callback_mode_t mode, void* arg);

            //jack client
            jack_client_t* fClient;
            const char* fName;

            //jack ports
            jack_port_t** fAudioCapturePorts;
            jack_port_t** fAudioPlaybackPorts;
            jack_port_t** fMidiCapturePorts;
            jack_port_t** fMidiPlaybackPorts;

            //sync and transport
            int fLastTransportState;

            // Five-second, interval-based diagnostics. These fields are written
            // only by the JACK process callback; reporting is deliberately
            // rate-limited so the normal cycle does not emit per-packet logs.
            uint64_t fDiagCycles = 0;
            uint64_t fDiagDataPacketErrors = 0;
            uint64_t fDiagSyncPacketErrors = 0;
            uint64_t fDiagSocketErrors = 0;
            uint64_t fDiagSlowCycles = 0;
            uint64_t fDiagMaxProcessUsecs = 0;
            uint64_t fDiagMaxSyncSendUsecs = 0;
            uint64_t fDiagMaxDataSendUsecs = 0;
            uint64_t fDiagMaxSyncRecvUsecs = 0;
            uint64_t fDiagMaxDataRecvUsecs = 0;
            jack_time_t fDiagLastReportUsecs = 0;

            void RecordDiagnosticStage(uint64_t& max_usecs,
                                        jack_time_t start,
                                        jack_time_t end);
            void FinishDiagnosticCycle(jack_time_t start, jack_time_t end);
            void ReportDiagnosticsIfDue(jack_time_t now);

            //monitoring
#ifdef JACK_MONITOR
            jack_time_t fPeriodUsecs;
            JackGnuPlotMonitor<float>* fNetTimeMon;
#endif

            bool Init(bool auto_connect);
            int AllocPorts();
            void FreePorts();

            //transport
            void EncodeTransportData();
            void DecodeTransportData();

            int Process();
            void TimebaseCallback(jack_position_t* pos);
            void ConnectPorts();
            void ConnectCallback(jack_port_id_t a, jack_port_id_t b, int connect);

            void SaveConnections(connections_list_t& connections);
            void LoadConnections(const connections_list_t& connections);

        public:

            JackNetMaster(JackNetSocket& socket, session_params_t& params, const char* multicast_ip);
            ~JackNetMaster();

            bool IsSlaveReadyToRoll();
    };

    typedef std::list<JackNetMaster*> master_list_t;
    typedef master_list_t::iterator master_list_it_t;
    typedef std::map <std::string, connections_list_t> master_connections_list_t;

    /**
    \Brief This class describer the Network Manager
    */

    class JackNetMasterManager
    {
            friend class JackNetMaster;

        private:

            static void SetShutDown(void* arg);
            static int SetSyncCallback(jack_transport_state_t state, jack_position_t* pos, void* arg);
            static void* NetManagerThread(void* arg);

            jack_client_t* fClient;
            const char* fName;
            char fMulticastIP[32];
            // Interface name to pin IP_ADD_MEMBERSHIP / IP_BOUND_IF to.
            // Set from JACK_NETJACK_MULTICAST_IF. Empty = legacy INADDR_ANY
            // behavior. See posix/JackNetUnixSocket.cpp::JoinMCastGroup.
            char fMulticastIF[16];
            // Interface index that master sockets pin unicast egress to.
            // InitMaster() latches the first SLAVE_AVAILABLE arrival interface
            // and keeps it. Do not re-latch on later packets: a stray announce
            // on another interface must not move live masters.
            int fBoundIF;
            // True when JACK_NETJACK_MULTICAST_IF sets the pin. Then fBoundIF
            // comes from fMulticastIF on each InitMaster() and no latch runs.
            bool fPinFromEnv;
            JackNetSocket fSocket;
            jack_native_thread_t fThread;
            master_list_t fMasterList;
            master_connections_list_t fMasterConnectionList;
            uint32_t fGlobalID;
            bool fRunning;
            bool fAutoConnect;
            bool fAutoSave;

            void Run();
            JackNetMaster* InitMaster(session_params_t& params);
            master_list_it_t FindMaster(uint32_t client_id);
            void RemoveMaster(master_list_it_t master_it);
            void ReapDeadMasters();
            int KillMaster(session_params_t* params);
            int SyncCallback(jack_transport_state_t state, jack_position_t* pos);
            int CountIO(const char* type, int flags);
            void ShutDown();

        public:

            JackNetMasterManager(jack_client_t* jack_client, const JSList* params);
            ~JackNetMasterManager();
    };
}

#endif
