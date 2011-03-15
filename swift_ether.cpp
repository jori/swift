#include <sys/ioctl.h>
#include <string>
#include "swift_ether.h"

using namespace swift;

bool EthernetSwift::initialised = false;
bool EthernetSwift::selftest = false;
SOCKET EthernetSwift::sock = -1;
int EthernetSwift::myifindex = -1;
unsigned char EthernetSwift::mymac[ETH_ALEN];
const unsigned char EthernetSwift::brmac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
std::vector<EthernetSwift *> EthernetSwift::sessions(1);
struct event EthernetSwift::evrecveth;
tint EthernetSwift::last_send_time = NOW;
tint EthernetSwift::send_interval = 0;
uint64_t EthernetSwift::frames_up = 0;
uint64_t EthernetSwift::frames_down = 0;

EthernetSwift::EthernetSwift(FileTransfer *ft) {
    pkttype = (selftest) ? PACKET_HOST : PACKET_OTHERHOST;
    transfer = ft;
    my_channel = sessions.size();
    peer_channel = 0;
    memset(peer_mac, 0, ETH_ALEN);
    sessions.push_back(this);
    transfer->hs_in_.push_back(bin_t(my_channel));
    evtimer_assign(&evsendeth,Channel::evbase,&SendEthCallback,this);
    if (!transfer->file().is_complete())
	Open(0, Channel::EncodeID(my_channel), transfer->file().root_hash());
    Channel::Time();
    evtimer_add(&evsendeth,tint2tv(NextSendTime()-NOW));
}

EthernetSwift::~EthernetSwift() {
    sessions[my_channel] = NULL;
}

bool EthernetSwift::Init(const std::string& dev, bool selftst) {
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
    selftest = selftst;
    initialised = true;
    return true;
}

void EthernetSwift::Open(tint channel, tint rev_channel, const Sha1Hash& hash) {
    struct evbuffer *evb = evbuffer_new();
    if (peer_channel) {
	AddHdr(evb, mymac, peer_mac);
        dprintf("%s #%u +open %s\n", tintstr(),my_channel,
		transfer->file().root_hash().hex().c_str());
    } else {
	// Send broadcast handshake
	AddHdr(evb, mymac, brmac);
        dprintf("%s #%u +open (hs) %s\n", tintstr(),my_channel,
		transfer->file().root_hash().hex().c_str());
    }
    evbuffer_add_32be(evb, channel);
    evbuffer_add_8(evb, SWIFT_ETH_OPEN);
    evbuffer_add_32be(evb, rev_channel);
    evbuffer_add_32be(evb, bin_toUInt32(bin_t::ALL));
    evbuffer_add_hash(evb, transfer->file().root_hash());
    if (peer_channel)
	// Add peak hashes:
	for (int i = 0; i < transfer->file().peak_count(); i++) {
	    if (evbuffer_get_length(evb) + 5 + Sha1Hash::SIZE > ETH_FRAME_LEN) {
		msgs.push_back(evb);
		evb = evbuffer_new();
	    }
	    bin_t peak = transfer->file().peak(i);
	    evbuffer_add_8(evb, SWIFT_ETH_HASH);
	    evbuffer_add_32be(evb, bin_toUInt32(peak));
	    evbuffer_add_hash(evb, transfer->file().peak_hash(i));
	    char bin_name_buf[32];
	    dprintf("%s #%u +phash %s\n",tintstr(),my_channel,
		    peak.str(bin_name_buf));
	}
    evbuffer_add_8(evb, SWIFT_ETH_END);
    msgs.push_back(evb);
}

void EthernetSwift::Request(tint channel, const bin_t& range) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, channel);
    evbuffer_add_8(evb, SWIFT_ETH_REQUEST);
    evbuffer_add_32be(evb, bin_toUInt32(range));
    evbuffer_add_8(evb, SWIFT_ETH_END);
    char bin_name_buf[32];
    dprintf("%s #%u +request %s\n", tintstr(), my_channel,
	    range.str(bin_name_buf));
    msgs.push_back(evb);
}

void EthernetSwift::Hash(tint channel, const Sha1Hash& hash) {
}

void EthernetSwift::Data(tint channel, const bin_t& range,
			 struct evbuffer* buf) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, channel);
    // Add uncle hashes
    bin_t pos = range;
    bin_t peak = transfer->file().peak_for(pos);
    char bin_name_buf[32];
    while (pos != peak) {
	if (evbuffer_get_length(evb) + 5 + Sha1Hash::SIZE > ETH_FRAME_LEN) {
	    evbuffer_add_8(evb, SWIFT_ETH_END);
	    msgs.push_back(evb);
	    evb = evbuffer_new();
	}
        bin_t uncle = pos.sibling();
        evbuffer_add_8(evb, SWIFT_ETH_HASH);
        evbuffer_add_32be(evb, bin_toUInt32(uncle));
        evbuffer_add_hash(evb,  transfer->file().hash(uncle) );
        dprintf("%s #%u +hash %s\n",tintstr(), my_channel,
		uncle.str(bin_name_buf));
	pos = pos.parent();
    }
    // Add data
    if (evbuffer_get_length(evb) + 5 + evbuffer_get_length(buf)
	> ETH_FRAME_LEN) {
	evbuffer_add_8(evb, SWIFT_ETH_END);
	msgs.push_back(evb);
	evb = evbuffer_new();
    }
    evbuffer_add_8(evb, SWIFT_ETH_DATA);
    evbuffer_add_32be(evb, bin_toUInt32(range));
    evbuffer_add_16be(evb, evbuffer_get_length(buf));
    evbuffer_add_buffer(evb, buf);
    evbuffer_add_8(evb, SWIFT_ETH_END);
    dprintf("%s #%u +data %s\n",tintstr(),my_channel,range.str(bin_name_buf));
    msgs.push_back(evb);
}

void EthernetSwift::Have(tint channel, const bin_t& range) {
}

void EthernetSwift::Ack(tint channel, const bin_t& range) {
}

void EthernetSwift::Peer(tint channel, const Address& addr) {
}

void EthernetSwift::Close(tint channel) {
    struct evbuffer *evb = evbuffer_new();
    AddHdr(evb, mymac, peer_mac);
    evbuffer_add_32be(evb, channel);
    evbuffer_add_8(evb, SWIFT_ETH_CLOSE);
    evbuffer_add_8(evb, SWIFT_ETH_END);
    dprintf("%s #%u +close\n", tintstr(), my_channel);
    msgs.push_back(evb);
    // TODO: close channel
}

void EthernetSwift::OnHandshake(const unsigned char *srcmac,
				const unsigned char *dstmac,
				struct evbuffer *evb) {
    // Minimum handshake length: channel, msg type, rev channel, range, hash
    if (evbuffer_get_length(evb) < 13 + Sha1Hash::SIZE) {
	eprintf("%s #0 incorrect size in eth handshake\n", tintstr());
	return;
    }
    uint8_t channel = evbuffer_remove_32be(evb);
    if (channel != 0)  {
	eprintf("%s #0 corrupt handshake message\n", tintstr());
	return;
    }
    uint8_t type = evbuffer_remove_8(evb);
    if (type != SWIFT_ETH_OPEN) {
	eprintf("%s #0 incorrect msg type in eth handshake: %i\n", tintstr(),
		(int)type);
	return;
    }
    uint32_t rev_channel = evbuffer_remove_32be(evb);
    bin_t range = bin_fromUInt32(evbuffer_remove_32be(evb));
    Sha1Hash hash = evbuffer_remove_hash(evb);
    FileTransfer* file = NULL;
    for(int i=0; i<FileTransfer::files.size(); i++)
        if (FileTransfer::files[i]
	    && FileTransfer::files[i]->root_hash()==hash
	    && FileTransfer::files[i]->file().is_complete()) {
	    file = FileTransfer::files[i];
	    break;
	}
    if (!file)
	return;		    // No such (complete) file on this server.
    EthernetSwift *session = new EthernetSwift(file);
    if (!session) {
	eprintf("%s #0 creating new session failed.\n", tintstr());
	return;			// No session for such file on this server.
    }
    dprintf("%s #%u -open (hs) %s\n", tintstr(), session->my_channel,
	    hash.hex().c_str());
    memcpy(session->peer_mac, srcmac, ETH_ALEN);
    session->peer_channel = rev_channel;
    // Send handshake ack
    session->Open(session->peer_channel, Channel::EncodeID(session->my_channel),
		  hash);
}

void EthernetSwift::OnOpen(const unsigned char *srcmac,
			   const unsigned char *dstmac, struct evbuffer *evb) {
    // Minimum Open length: rev channel, range hash
    if (evbuffer_get_length(evb) < 8 + Sha1Hash::SIZE) {
	eprintf("%s #%u incorrect size in eth open\n", tintstr(), my_channel);
	return;
    }
    memcpy(peer_mac, srcmac, ETH_ALEN);
    peer_channel = evbuffer_remove_32be(evb);
    bin_t range = bin_fromUInt32(evbuffer_remove_32be(evb));
    Sha1Hash hash = evbuffer_remove_hash(evb);
    dprintf("%s #%u -open %s\n", tintstr(), my_channel, hash.hex().c_str());
    if (transfer->file().root_hash()!=hash) {
	eprintf("%s #%u incorrect hash in eth open\n", tintstr(), my_channel);
	return;
    }
    bin_t reqrange = transfer->picker().Pick(rec_ranges, 1, 0);
    Request(peer_channel, reqrange);
}

void EthernetSwift::OnRequest(const unsigned char *srcmac,
			      const unsigned char *dstmac,
			      struct evbuffer *evb) {

    if (evbuffer_get_length(evb) < 4) {
	eprintf("%s #%u incorrect size in eth request\n", tintstr(),
		my_channel);
	return;
    }
    bin_t range = bin_fromUInt32(evbuffer_remove_32be(evb));
    char bin_name_buf[32];
    dprintf("%s #%u -request %s\n",tintstr(), my_channel,
	    range.str(bin_name_buf));
    while (!range.is_base())
	range = range.left();
    struct evbuffer_iovec vec;
    struct evbuffer* evbuf = evbuffer_new();
    if (evbuffer_reserve_space(evbuf, SWIFT_ETH_MAX_DATA, &vec, 1) < 0) {
	print_error("EthernetSwift::OnRequest - error on evbuffer_reserve_space");
	return;
    }
    size_t r = pread(transfer->file().file_descriptor(),(char *)vec.iov_base,
		     SWIFT_ETH_MAX_DATA, range.base_offset()<<10);
    vec.iov_len = r;
    if (evbuffer_commit_space(evbuf, &vec, 1) < 0)  {
        print_error("EthernetSwift::OnRequest - error on evbuffer_commit_space");
	return;
    }
    Data(peer_channel, range, evbuf);
    evbuffer_free(evbuf);
}


void EthernetSwift::OnHash(const unsigned char *srcmac,
			   const unsigned char *dstmac, struct evbuffer *evb) {
    if (evbuffer_get_length(evb) < 4 + Sha1Hash::SIZE) {
	eprintf("%s #%u incorrect size in eth has\n", tintstr(), my_channel);
	return;
    }
    bin_t range = bin_fromUInt32(evbuffer_remove_32be(evb));
    rec_ranges.set(range);
    Sha1Hash hash = evbuffer_remove_hash(evb);
    transfer->file().OfferHash(range, hash);
    char bin_name_buf[32];
    dprintf("%s #%u -hash %s\n",tintstr(),my_channel,range.str(bin_name_buf));
}

void EthernetSwift::OnData(const unsigned char *srcmac,
			   const unsigned char *dstmac, struct evbuffer *evb) {

    if (evbuffer_get_length(evb) < 4) {
	eprintf("%s #%u incorrect size in eth data\n", tintstr(), my_channel);
	return;
    }
    bin_t range = bin_fromUInt32(evbuffer_remove_32be(evb));
    int length = evbuffer_remove_16be(evb);
    if (length < 0 || length > SWIFT_ETH_MAX_DATA) {
	eprintf("%s #%u incorrect data length: %d\n", tintstr(), my_channel,
	    length);
	return;
    }
    uint8_t *data = evbuffer_pullup(evb, length);
    char bin_name_buf[32];
    if (transfer->file().OfferData(range, (char*)data, length)) {
	dprintf("%s #%u -data %s\n",tintstr(), my_channel,
		range.str(bin_name_buf));
    } else {
	dprintf("%s #%u !data %s\n",tintstr(), my_channel,
		range.str(bin_name_buf));
    }
    if (!transfer->file().is_complete()) {
	bin_t reqrange = transfer->picker().Pick(rec_ranges, 1, 0);
	if (!reqrange.is_none()) {
	    Request(peer_channel, reqrange);
	} else
	    Close(peer_channel);
    } else
	Close(peer_channel);
    evbuffer_drain(evb, length);
}

void EthernetSwift::OnClose(const unsigned char *srcmac,
			    const unsigned char *dstmac,
			    struct evbuffer *evb) {
    dprintf("%s #%u -close\n", tintstr(), my_channel);
    // TODO: close channel
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
    frames_up++;
    Channel::bytes_up += length;
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
	if (!peer_channel) {
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
    while (evbuffer_get_length(evb) > 0) {
	uint8_t type = evbuffer_remove_8(evb);
	switch (type) {
	case SWIFT_ETH_OPEN:
	    OnOpen(srcmac, dstmac, evb);
	    break;
	case SWIFT_ETH_REQUEST:
	    OnRequest(srcmac, dstmac, evb);
	    break;
	case SWIFT_ETH_DATA:
	    OnData(srcmac, dstmac, evb);
	    break;
	case SWIFT_ETH_CLOSE:
	    OnClose(srcmac, dstmac, evb);
	    break;
	case SWIFT_ETH_HASH:
	    OnHash(srcmac, dstmac, evb);
	    break;
	case SWIFT_ETH_END:
	    return;
	case SWIFT_ETH_HAVE:
	case SWIFT_ETH_ACK:
	case SWIFT_ETH_PEER:
	default:
	    eprintf("%s #%u eth msg id unimplemented or unknown: %i\n",
		    tintstr(),my_channel,(int)type);
	    return;
	}
    }
}

void EthernetSwift::SendEthCallback(int fd, short event, void *arg) {
    EthernetSwift *sender = (EthernetSwift *)arg;
    sender->Send();
}

void EthernetSwift::RecvEthCallback(int fd, short event, void *arg) {
    struct evbuffer *evb = evbuffer_new();
    if (RecvFrom(fd, evb) >= 0) {
	unsigned char srcmac[ETH_ALEN], dstmac[ETH_ALEN];
	// Minimum msg length: dstmac, srcmac, prot, channel, msg type:
	if (evbuffer_get_length(evb) > 6 + 2*ETH_ALEN) {
	    evbuffer_remove(evb, dstmac, ETH_ALEN);
	    evbuffer_remove(evb, srcmac, ETH_ALEN);
	    if (evbuffer_remove_16be(evb) == 0x0000) {// TODO: swift
						      // protocol num?
		frames_down++;
		Channel::bytes_down += evbuffer_get_length(evb);
		if (memcmp(dstmac, brmac, ETH_ALEN)) {
		    uint32_t channel =
			Channel::DecodeID(evbuffer_remove_32be(evb));
		    if (channel < sessions.size()) {
			EthernetSwift *session = sessions[channel];
			if (session)
			    session->Recv(srcmac, dstmac, evb);
			else
			    eprintf("%s eth #%u is already closed\n",tintstr(),
				    channel);
		    }
		} else 		// Broadcast message, handshake.
		    OnHandshake(srcmac, dstmac, evb);
	    }
	}
    }
    evbuffer_free(evb);
    event_add(&evrecveth, NULL);
}
