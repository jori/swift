#ifndef SWIFT_ETHER_H
#define SWIFT_ETHER_H
#include <string.h>
#include "swift.h"

namespace swift {

#define MAX_ETH_FRAME_LEN (ETH_FRAME_LEN + ETH_FCS_LEN)

typedef enum {
    SWIFT_ETH_OPEN = SWIFT_MESSAGE_COUNT + 1,
    SWIFT_ETH_REQUEST = SWIFT_MESSAGE_COUNT + 2,
    SWIFT_ETH_HASH = SWIFT_MESSAGE_COUNT + 3,
    SWIFT_ETH_DATA = SWIFT_MESSAGE_COUNT + 4,
    SWIFT_ETH_HAVE = SWIFT_MESSAGE_COUNT + 5,
    SWIFT_ETH_ACK = SWIFT_MESSAGE_COUNT + 6,
    SWIFT_ETH_PEER = SWIFT_MESSAGE_COUNT + 7,
    SWIFT_ETH_CLOSE = SWIFT_MESSAGE_COUNT + 8,
    SWIFT_ETH_MESSAGE_COUNT = SWIFT_MESSAGE_COUNT + 9
} eth_messageid_t;

class EthernetSwift {
public:
    EthernetSwift(FileTransfer *ft);
    ~EthernetSwift();
    static bool Init(const std::string& dev, bool selftst=false);
    void Open(tint channel, tint rev_channel, const Sha1Hash& hash);
    void Request(tint channel, const bin64_t& range);
    void Hash(tint channel, const Sha1Hash& hash);
    void Data(tint channel, const bin64_t& range, struct evbuffer* buf);
    void Have(tint channel, const bin64_t& range);
    void Ack(tint channel, const bin64_t& range);
    void Peer(tint channel, const Address& addr);
    void Close(tint channel);
    static void OnHandshake(const unsigned char *srcmac,
			    const unsigned char *dstmac, struct evbuffer *evb);
    void OnOpen(const unsigned char *srcmac, const unsigned char *dstmac,
		struct evbuffer *evb);
    void OnRequest(const unsigned char *srcmac, const unsigned char *dstmac,
		   struct evbuffer *evb);
    void OnData(const unsigned char *srcmac, const unsigned char *dstmac,
		struct evbuffer *evb);
    void OnClose(const unsigned char *srcmac, const unsigned char *dstmac,
		 struct evbuffer *evb);
private:
    static bool initialised;
    static bool selftest;
    static SOCKET sock;
    static int myifindex;
    unsigned char pkttype;
    static unsigned char mymac[ETH_ALEN];
    unsigned char peer_mac[ETH_ALEN];
    const static unsigned char brmac[ETH_ALEN];
    tint channel;
    tint peer_channel;
    FileTransfer *transfer;
    std::deque<struct evbuffer *> msgs;
    static std::vector<EthernetSwift *> sessions;
    struct event evsendeth;
    static struct event evrecveth;
    static tint last_send_time;
    static tint send_interval;
    void AddHdr(struct evbuffer *evb, const unsigned char *srcmac,
		const unsigned char *dstmac);
    static int SendTo(SOCKET sock, const Address& addr, struct evbuffer *evb);
    static int RecvFrom(SOCKET sock, struct evbuffer *evb);
    tint NextSendTime();
    void Send();
    void Recv(unsigned char *srcmac, unsigned char *dstmac,
	      struct evbuffer *evb);
    static void SendEthCallback(int fd, short event, void *arg);
    static void RecvEthCallback(int fd, short event, void *arg);
};

}

#endif
