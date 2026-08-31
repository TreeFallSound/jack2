/*
 Copyright (C) 2008-2011 Romain Moret at Grame

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#ifndef __JackNetUnixSocket__
#define __JackNetUnixSocket__

#include "JackNetSocket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace Jack
{
#define NET_ERROR_CODE errno
#define SOCKET_ERROR -1
#define StrError strerror

    typedef struct sockaddr socket_address_t;
    typedef struct in_addr address_t;

    //JackNetUnixSocket********************************************
    class SERVER_EXPORT JackNetUnixSocket
    {
        private:

            int fSockfd;
            int fPort;
            int fTimeOut;

            struct sockaddr_in fSendAddr;
            struct sockaddr_in fRecvAddr;

            // Interface name to pin outgoing multicast to. Stored here rather
            // than applied immediately: callers (JackNetAdapter) set it before
            // the socket exists, so the old code did the setsockopt on fd 0 and
            // never re-applied it. Empty = legacy behavior. Re-applied by every
            // NewSocket().
            char fMcastIF[16];
            int ApplyMulticastIF();

            // Interface index for unicast egress. 0 disables the pin.
            // NewSocket() applies it. The copy constructor must copy it.
            int fBoundIF;
            int ApplyBoundIF();

            // If true, CatchHost() uses recvmsg() and records the arrival
            // interface index in fLastRecvIF. If false, CatchHost() uses
            // recvfrom().
            bool fRecvIF;
            int fLastRecvIF;
            int ApplyRecvIF();
        #if defined(__sun__) || defined(sun)
            int WaitRead();
            int WaitWrite();
        #endif

        public:

            JackNetUnixSocket();
            JackNetUnixSocket(const char* ip, int port);
            JackNetUnixSocket(const JackNetUnixSocket&);
            ~JackNetUnixSocket();

            JackNetUnixSocket& operator=(const JackNetUnixSocket& socket);

            //socket management
            int NewSocket();
            int Bind();
            int BindWith(const char* ip);
            int BindWith(int port);
            int Connect();
            int ConnectTo(const char* ip);
            void Close();
            void Reset();
            bool IsSocket();

            //IP/PORT management
            void SetPort(int port);
            int GetPort();

            //address management
            int SetAddress(const char* ip, int port);
            char* GetSendIP();
            char* GetRecvIP();

            //utility
            int GetName(char* name);
            int JoinMCastGroup(const char* mcast_ip);
            // Bind the multicast join to a specific interface. Required when
            // the host has more than one interface with a route to the
            // multicast group (e.g. a direct-cable link-local on en7 plus a
            // wifi link on en0) and the kernel would otherwise pick the
            // wrong one via the unicast default route. Pass NULL or "" to
            // keep the legacy INADDR_ANY behavior.
            int JoinMCastGroup(const char* mcast_ip, const char* ifname);
            // Pin the outgoing interface for multicast packets. Slave side:
            // sendto() to the multicast group will go out this interface
            // regardless of what the kernel's unicast/multicast route table
            // says. Required on hosts whose default route is on the wrong
            // interface (e.g. wifi) but whose netJACK2 link is on a different
            // one (e.g. direct-cable eth0/en7). ifname == NULL or "" is a
            // no-op so the legacy behavior (kernel chooses interface) is
            // preserved when the env var is unset.
            int SetMulticastIF(const char* ifname);

            // Pin unicast egress (connect(), send()) to an interface index.
            // Pass 0 to remove the pin. NewSocket() applies the pin.
            int SetBoundIF(int ifindex);
            // Make CatchHost() record the arrival interface index.
            int SetRecvIF();
            // Arrival interface index of the last CatchHost() datagram.
            // 0 if not known.
            int GetLastRecvIF();
            // Interface index for the given name. 0 if the name is unknown.
            int IFNameToIndex(const char* ifname);
            // True if the interface index refers to a current interface.
            bool IFIndexValid(int ifindex);

            //options management
            int SetOption(int level, int optname, const void* optval, socklen_t optlen);
            int GetOption(int level, int optname, void* optval, socklen_t* optlen);

            //timeout
            int SetTimeOut(int us);

            //disable local loop
            int SetLocalLoop();

            bool IsLocal(char* ip);

            //network operations
            int SendTo(const void* buffer, size_t nbytes, int flags);
            int SendTo(const void* buffer, size_t nbytes, int flags, const char* ip);
            int Send(const void* buffer, size_t nbytes, int flags);
            int RecvFrom(void* buffer, size_t nbytes, int flags);
            int Recv(void* buffer, size_t nbytes, int flags);
            int CatchHost(void* buffer, size_t nbytes, int flags);

            //error management
            net_error_t GetError();
            void PrintError();
    };
}

#endif
