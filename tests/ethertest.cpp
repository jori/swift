#include <gtest/gtest.h>
#include "swift_ether.h"

using namespace swift;

struct event evcompl;
int size, copy;

void IsCompleteCallback(int fd, short event, void *arg) {
    if (swift::SeqComplete(copy)!=size)
	evtimer_add(&evcompl, tint2tv(TINT_SEC));
    else
	event_base_loopexit(Channel::evbase, NULL);
}

TEST(Ethernet, Transfertest) {
    srand ( time(NULL) );

    unlink("doc/sofi-copy.jpg");
    struct stat st;
    ASSERT_EQ(0,stat("doc/sofi.jpg", &st));
    size = st.st_size;//, sizek = (st.st_size>>10) + (st.st_size%1024?1:0) ;

    int file = swift::Open("doc/sofi.jpg");
    FileTransfer* fileobj = FileTransfer::file(file);

    copy = swift::Open("doc/sofi-copy.jpg",fileobj->root_hash());

    // Two EthernetSwift instances with selftest = true,
    EthernetSwift *ethsend = new EthernetSwift(fileobj, true);
    EthernetSwift *ethrecv = new EthernetSwift(FileTransfer::file(copy), true);

    evtimer_assign(&evcompl, Channel::evbase, IsCompleteCallback, NULL);
    event_base_dispatch(Channel::evbase);

    ASSERT_EQ(size,swift::SeqComplete(copy));

    delete ethsend;
    delete ethrecv;

    swift::Close(file);
    swift::Close(copy);
}

int main (int argc, char** argv) {
    std::string dev = "lo";
    swift::LibraryInit();
    Channel::evbase = event_base_new();
    if (!EthernetSwift::Init(dev))
	return -1;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
