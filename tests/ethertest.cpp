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

    evtimer_assign(&evcompl, Channel::evbase, IsCompleteCallback, NULL);
    evtimer_add(&evcompl, tint2tv(TINT_SEC));
    event_base_dispatch(Channel::evbase);

    ASSERT_EQ(size,swift::SeqComplete(copy));

    swift::Close(file);
    swift::Close(copy);
}

int main (int argc, char** argv) {
    swift::LibraryInit();
    Channel::debug_file = stdout;
    Channel::evbase = event_base_new();
    if (!EthernetSwift::Init("lo", true)) // selftest = true
	return -1;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
