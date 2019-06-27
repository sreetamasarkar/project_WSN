#define INFINITE_HOP_COUNT 0xFFFF
#define MAX_NEIGHBORS 10

#define ROUTE_SHARE_INTERVAL (5)

typedef struct
{
	struct neighbors *next;
	linkaddr_t nbr_addr;
	uint16_t hop_count;
	struct ctimer node_timer;
}neighbors;

typedef struct
{
	linkaddr_t addr;
	uint16_t cost;
} nodes;

enum PACKET_TYPE {BROADCAST,UNICAST,ASSET_TRACKING,NETWORK_STAT};
