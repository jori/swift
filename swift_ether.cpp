#include <sys/ioctl.h>
#include <string>
#include "swift_ether.h"

using namespace swift;

bool EthernetSwift::initialised = false;
SOCKET EthernetSwift::sock = -1;
int EthernetSwift::myifindex = -1;
unsigned char EthernetSwift::mymac[ETH_ALEN];
const unsigned char EthernetSwift::brmac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
std::vector<EthernetSwift *> EthernetSwift::sessions(1);
struct event EthernetSwift::evrecveth;
tint EthernetSwift::last_send_time = NOW;
tint EthernetSwift::send_interval = 50*TINT_MSEC; // TODO:?

EthernetSwift::EthernetSwift(FileTransfer *ft, bool selftest) {
    pkttype = (selftest) ? PACKET_HOST : PACKET_OTHERHOST;
    transfer = ft;
    channel = sessions.size();
    sessions.push_back(this);
    evtimer_assign(&evsendeth,Channel::evbase,&SendEthCallback,this);
    if (!swift::IsComplete(ft->file().file_descriptor()))
	Open(channel, 0, ft->file().root_hash());
    Channel::Time();
    evtimer_add(&evsendeth,tint2tv(NextSendTime()-NOW));
}

EthernetSwift::~EthernetSwift() {
    sessions[channel] = NULL;
}

bool EthernetSwift::Init(const std::string& dev) {
    if (initialised) {
	perror("EthernetSwift already initialised.");
	return true;
    }
    sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock == -1) {
	perror("EthernetSwift - socket failed.");
	return false;
    }
    struct ifreq ifr;
    strncpy(ifr.ifr_name, dev.c_str(), IFNAMSIZ);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
	perror("EthernetSwift - SIOCGIFINDEX failed.");
	return false;
    }
    myifindex = ifr.ifr_ifindex;
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
	perror("EthernetSwift - SIOCGIFINDEX failed.");
	return false;
    }
    memcpy(mymac, (const unsigned char *)ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    // swift ethernet receive
    event_assign(&evrecveth, Channel::evbase, sock, EV_READ,
		 RecvEthCallback, NULL);
    event_add(&evrecveth, NULL);
    initialised = true;
    return true;
}

void EthernetSwift::Open(tint channel, tint rev_channel, const Sha1Hash& hash) {
    struct evbuffer *evb = evbuffer_new();
    if (rev_channel != 0)
	AddHdr(evb, mymac, peer_mac);
    else
	// Send broadcast handshake
	AddHdr(evb, mymac, brmac);
    evbuffer_add_32be(evb, Channel::EncodeID(channel));
    evbuffer_add_8(evb, SWIFT_ETH_OPEN);
    evbuffer_add_32be(evb, Channel::EncodeID(rev_channel));
    evbuffer_add_32be(evb, bin64_t::ALL32);
    evbuffer_add_hash(evb, transfer->file().root_hash());
    msgs.push_back(evb);
}

void EthernetSwift::Request(tint channel, const bin64_t& range) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, Channel::EncodeID(channel));
    evbuffer_add_8(evb, SWIFT_ETH_REQUEST);
    evbuffer_add_32be(evb, range.to32());
    msgs.push_back(evb);
}

void EthernetSwift::Hash(tint channel, const Sha1Hash& hash) {
}

void EthernetSwift::Data(tint channel, const bin64_t& range,
			 struct evbuffer* buf) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, Channel::EncodeID(channel));
    evbuffer_add_8(evb, SWIFT_ETH_DATA);
    evbuffer_add_32be(evb, range.to32());
    evbuffer_add(evb, buf, evbuffer_get_length(evb));
    msgs.push_back(evb);
}

void EthernetSwift::Have(tint channel, const bin64_t& range) {
}

void EthernetSwift::Ack(tint channel, const bin64_t& range) {
}

void EthernetSwift::Peer(tint channel, const Address& addr) {
}

void EthernetSwift::Close(tint channel) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, Channel::EncodeID(channel));
    evbuffer_add_8(evb, SWIFT_ETH_CLOSE);
    msgs.push_back(evb);
}

void EthernetSwift::AddHdr(struct evbuffer *evb, const unsigned char *srcmac,
			   const unsigned char *dstmac) {
    evbuffer_add(evb, dstmac, ETH_ALEN);
    evbuffer_add(evb, srcmac, ETH_ALEN);
    evbuffer_add_16be(evb, 0x00); // TODO: swift protocol number?
}

int EthernetSwift::SendTo(SOCKET sock, const Address& addr,
			struct evbuffer *evb) {
    int length = evbuffer_get_length(evb);
    int r = sendto(sock,(const char *)evbuffer_pullup(evb, length),length,0,
                   (struct sockaddr*)&(addr.addrll),
		   sizeof(struct sockaddr_ll));
    if (r<0)
        perror("EthernetSwift::Send - can't send");
    Channel::Time();
    return r;
}

int EthernetSwift::RecvFrom(SOCKET sock, struct evbuffer *evb) {
    struct evbuffer_iovec vec;
    if (evbuffer_reserve_space(evb, MAX_ETH_FRAME_LEN, &vec, 1) < 0) {
	print_error("EthernetSwift::Recv - error on evbuffer_reserve_space");
	return 0;
    }
    int length = recvfrom(sock, (char *)vec.iov_base, MAX_ETH_FRAME_LEN, 0,
			  NULL, NULL);
    if (length<0) {
        length = 0;
        print_error("EthernetSwift::Recv - error on recv");
    }
    vec.iov_len = length;
    if (evbuffer_commit_space(evb, &vec, 1) < 0)  {
        length = 0;
        print_error("EthernetSwift::Recv - error on evbuffer_commit_space");
    }
    Channel::Time();
    return length;
}

tint EthernetSwift::NextSendTime() {
    tint diff = NOW - last_send_time;
    last_send_time = (diff > send_interval) ? NOW :
	NOW + send_interval - diff;
    return last_send_time;
}

void EthernetSwift::Send() {
    Channel::Time();
    if (!msgs.empty()) {
	struct evbuffer *evb = msgs.front();
	msgs.pop_front();
	Address addr(peer_mac, pkttype, myifindex);
	if (addr.is_mac(Address())) {
	    // Broadcast handshake
	    addr.set_mac(brmac);
	    addr.set_pkttype(PACKET_BROADCAST);
	}
	SendTo(sock, addr, evb);
	evbuffer_free(evb);
    }
    last_send_time = NOW;
    evtimer_del(&evsendeth);
    evtimer_add(&evsendeth,tint2tv(NextSendTime()-NOW));
}

void EthernetSwift::Recv(unsigned char *srcmac, unsigned char *dstmac,
				   struct evbuffer *evb) {
    Channel::Time();
    if (!memcmp(dstmac, brmac, ETH_ALEN)) {
	// Broadcast handshake
	// TODO:
    }
    // TODO: handle
}

void EthernetSwift::SendEthCallback(int fd, short event, void *arg) {
    EthernetSwift *sender = (EthernetSwift *)arg;
    sender->Send();
}

void EthernetSwift::RecvEthCallback(int fd, short event, void *arg) {
    struct evbuffer *evb = evbuffer_new();
    if (RecvFrom(fd, evb) >= 0) {
	unsigned char srcmac[ETH_ALEN], dstmac[ETH_ALEN];
	if (evbuffer_get_length(evb) > 2 + 2*ETH_ALEN) {
	    evbuffer_remove(evb, dstmac, ETH_ALEN);
	    evbuffer_remove(evb, srcmac, ETH_ALEN);
	    if (evbuffer_remove_16be(evb) == 0x0000)// TODO: swift protocol num?
		Recv(srcmac, dstmac, evb);
	}
    }
    evbuffer_free(evb);
    event_add(&evrecveth, NULL);
}
