/*
 *  swift.cpp
 *  swift the multiparty transport protocol
 *
 *  Created by Victor Grishchenko on 2/15/10.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "compat.h"
#include "swift.h"
#include "swift_ether.h"

using namespace swift;

#define quit(...) {fprintf(stderr,__VA_ARGS__); exit(1); }
SOCKET InstallHTTPGateway (Address addr);

struct event evreport, evend;
int file = -1;

int main (int argc, char** argv) {
    
    static struct option long_options[] =
    {
        {"hash",    required_argument, 0, 'h'},
        {"file",    required_argument, 0, 'f'},
        {"daemon",  no_argument, 0, 'd'},
        {"ethdev",  required_argument, 0, 'e'},
        {"interval",  required_argument, 0, 'i'},
        {"listen",  required_argument, 0, 'l'},
        {"tracker", required_argument, 0, 't'},
        {"debug",   no_argument, 0, 'D'},
        {"progress",no_argument, 0, 'p'},
        {"http",    optional_argument, 0, 'g'},
        {"wait",    optional_argument, 0, 'w'},
        {0, 0, 0, 0}
    };

    Sha1Hash root_hash;
    char* filename = 0;
    char *devname = 0;
    bool daemonize = false, report_progress = false;
    Address bindaddr;
    Address tracker;
    Address http_gw;
    tint wait_time = 0;
    tint send_interval = 0;
    
    LibraryInit();
    Channel::evbase = event_base_new();

    int c;
    while ( -1 != (c = getopt_long (argc, argv, ":h:f:e:i:dl:t:Dpg::w::", long_options, 0)) ) {
        
        switch (c) {
            case 'h':
                if (strlen(optarg)!=40)
                    quit("SHA1 hash must be 40 hex symbols\n");
                root_hash = Sha1Hash(true,optarg); // FIXME ambiguity
                if (root_hash==Sha1Hash::ZERO)
                    quit("SHA1 hash must be 40 hex symbols\n");
                break;
            case 'f':
                filename = strdup(optarg);
                break;
            case 'e':
                devname = strdup(optarg);
                break;
	    case 'i':
		send_interval = atoi(optarg) * TINT_MSEC;
		if (send_interval <= 0)
		    quit("invalid send interval value\n");
		break;
            case 'd':
                daemonize = true;
                break;
            case 'l':
                bindaddr = Address(optarg);
                if (bindaddr==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                wait_time = TINT_NEVER;
                break;
            case 't':
                tracker = Address(optarg);
                if (tracker==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                SetTracker(tracker);
                break;
            case 'D':
                Channel::debug_file = optarg ? fopen(optarg,"a") : stdout;
                break;
            case 'p':
                report_progress = true;
                break;
            case 'g':
                http_gw = optarg ? Address(optarg) : Address(Address::LOCALHOST,8080);
                if (wait_time==-1)
                    wait_time = TINT_NEVER; // seed
                break;
            case 'w':
                if (optarg) {
                    char unit = 'u';
                    if (sscanf(optarg,"%lli%c",&wait_time,&unit)!=2)
                        quit("time format: 1234[umsMHD], e.g. 1M = one minute\n");
                    switch (unit) {
                        case 'D': wait_time *= 24;
                        case 'H': wait_time *= 60;
                        case 'M': wait_time *= 60;
                        case 's': wait_time *= 1000;
                        case 'm': wait_time *= 1000;
                        case 'u': break;
                        default:  quit("time format: 1234[umsMHD], e.g. 1D = one day\n");
                    }
                } else
                    wait_time = TINT_NEVER;
                break;
        }

    }   // arguments parsed
    

    if (bindaddr!=Address()) { // seeding
        if (Listen(bindaddr)<=0)
            quit("cant listen to %s\n",bindaddr.str())
    } else if (tracker!=Address() || http_gw!=Address()) { // leeching
        for (int i=0; i<=10; i++) {
            bindaddr = Address((uint32_t)INADDR_ANY,0);
            if (Listen(bindaddr)>0)
                break;
            if (i==10)
                quit("cant listen on %s\n",bindaddr.str());
        }
    }
    
    if (tracker!=Address())
        SetTracker(tracker);

    if (devname) {
	if (send_interval)
	    EthernetSwift::send_interval = send_interval;
	EthernetSwift::Init(devname, false);
    }

    // if (http_gw!=Address())
    //     InstallHTTPGateway(http_gw);

    if (root_hash!=Sha1Hash::ZERO && !filename)
        filename = strdup(root_hash.hex().c_str());

    if (filename) {
        file = Open(filename,root_hash);
        if (file<=0)
            quit("cannot open file %s",filename);
        printf("Root hash: %s\n", RootMerkleHash(file).hex().c_str());
    }

    if (bindaddr==Address() && file==-1 && http_gw==Address()) {
        fprintf(stderr,"Usage:\n");
        fprintf(stderr,"  -h, --hash\troot Merkle hash for the transmission\n");
        fprintf(stderr,"  -f, --file\tname of file to use (root hash by default)\n");
        fprintf(stderr,"  -l, --listen\t[ip:|host:]port to listen to (default: random)\n");
        fprintf(stderr,"  -t, --tracker\t[ip:|host:]port of the tracker (default: none)\n");
        fprintf(stderr,"  -e, --ethdev\tnetwork device name used in swift over ethernet (default: none)\n");
	fprintf(stderr,"  -i, --interval\tsend interval (ms) of ethernet frames in swift over ethernet\n");
        fprintf(stderr,"  -D, --debug\tfile name for debugging logs (default: stdout)\n");
        fprintf(stderr,"  -d, --daemon\trun as daemon\n");
        fprintf(stderr,"  -p, --progress\treport transfer progress\n");
        fprintf(stderr,"  -g, --http\t[ip:|host:]port to bind HTTP gateway to (default localhost:8080)\n");
        fprintf(stderr,"  -w, --wait\tlimit running time, e.g. 1[DHMs] (default: infinite with -l, -g)\n");
        return 1;
    }

    // End after wait_time
    if (wait_time != TINT_NEVER && (long)wait_time > 0) {
	evtimer_assign(&evend, Channel::evbase, EndCallback, NULL);
	evtimer_add(&evend, tint2tv(wait_time));
    }

    if (report_progress) {
	evtimer_assign(&evreport, Channel::evbase, ReportCallback, NULL);
	evtimer_add(&evreport, tint2tv(TINT_SEC));
    }

    event_base_dispatch(Channel::evbase);

    if (file!=-1)
        Close(file);
    
    if (Channel::debug_file)
        fclose(Channel::debug_file);
    
    swift::Shutdown();
    
    return 0;

}

void swift::ReportCallback(int fd, short event, void *arg) {
    fprintf(stderr,
	    "%s %lli of %lli (seq %lli) %lli dgram %lli ethfram %lli bytes up, "	\
	    "%lli dgram %lli ethfram %lli bytes down\n",
	    IsComplete(file) ? "DONE" : "done",
	    Complete(file), Size(file), SeqComplete(file),
	    Channel::dgrams_up, EthernetSwift::frames_up, Channel::bytes_up,
	    Channel::dgrams_down, EthernetSwift::frames_down,
	    Channel::bytes_down );
    evtimer_add(&evreport, tint2tv(TINT_SEC));
}

void swift::EndCallback(int fd, short event, void *arg) {
    event_base_loopexit(Channel::evbase, NULL);
}
