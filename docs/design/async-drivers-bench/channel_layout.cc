// Where an idle three-voter follower's SnapshotChannel bytes go: the layout of the channel and
// its per-peer state (snapshot_channel.h:183-189), and the block the allocator returns for the
// two peers' channels (the reserve at snapshot_channel.h:44).
#define private public  // the per-peer Channel is private; this program only reads sizeof
#include "consensus/snapshot_channel.h"
#undef private
#include "probe.hh"

#include <cstdio>
#include <cstdlib>

namespace c = kronuz::consensus;
using Channel = c::SnapshotChannel::Channel;

int main() {
	std::printf("layout sizeof_snapshot_channel=%zu sizeof_channel=%zu input_array=%zu channel_without_input=%zu\n",
		sizeof(c::SnapshotChannel), sizeof(Channel), c::maximum_transfer_bytes, sizeof(Channel) - c::maximum_transfer_bytes);
	for (std::size_t peers : {1, 2, 4}) {
		void* block = std::malloc(peers * sizeof(Channel));
		std::printf("peers=%zu requested=%zu usable=%zu\n", peers, peers * sizeof(Channel), usable(block));
		std::free(block);
	}
	return 0;
}
