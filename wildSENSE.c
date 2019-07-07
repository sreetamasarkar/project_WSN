//#define MOBILE_MOTE 1
/*----------------------------INCLUDES----------------------------------------*/
// standard C includes:
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Contiki-specific includes:
#include "contiki.h"
#include "net/rime/rime.h"     // Establish connections.
#include "net/netstack.h"      // Wireless-stack definitions
#include "dev/button-sensor.h" // User Button
#include "dev/leds.h"          // Use LEDs.
#include "core/net/linkaddr.h"

static uint16_t node_id;		        // Stores node id
static int node_id_new;		        // Stores node id

const linkaddr_t node_addr;

#include "helpers.h"

LIST(nbr_list);
MEMB(neighbor_mem, neighbors, MAX_NEIGHBORS);

//static int rssi_arr[MAX_NEIGHBORS][3]; //take last 3 values of rssi

// Creates an instance of a broadcast connection.
static struct broadcast_conn broadcast;

// Creates an instance of a unicast connection.
static struct unicast_conn unicast;
nodes nearest_neighbor;
static bool gateway_found = false;

//------------------------ FUNCTIONS ------------------------
// Prints the current settings.
static void print_settings(void)
{
	radio_value_t channel;

	NETSTACK_CONF_RADIO.get_value(RADIO_PARAM_CHANNEL, &channel);

	printf("\n-------------------------------------\n");
	printf("RIME addr = \t0x%x%x\n", linkaddr_node_addr.u8[0],
			linkaddr_node_addr.u8[1]);
	printf("Using radio channel %d\n", channel);
	printf("---------------------------------------\n");
}

bool is_duplicate_entry(const linkaddr_t *address, uint8_t cost)
{
	//This for iteration purpose only
	neighbors *e;
	for (e = (neighbors *) list_head(nbr_list); e != NULL;
			e = (neighbors *) e->next)
	{

		if (linkaddr_cmp(address, &e->nbr_addr))
		{
			e->hop_count = cost + 1;
			//ctimer_set(&e->node_timer,CLOCK_SECOND*TIME_TO_LIVE,remove_nbr,e);
			return true;
		}
	}
	return false;
}

void print_routing_table()
{
	neighbors *e;

	printf("\nPrint Neighbour list: ");
//	printf("%s\r\n",__func__);
	for (e = (neighbors *) list_head(nbr_list); e != NULL;
			e = (neighbors *) e->next)
	{
		int neighbor_addr = node_id_map(
				((e->nbr_addr.u8[0] << 8) | e->nbr_addr.u8[1]));
		printf("\nAddress: %d Cost: %d\r\n", neighbor_addr, e->hop_count);
	}
	printf("\r\n");
}

void add_routing_entry(const linkaddr_t *address, uint8_t cost)
{
	bool status = false;
	status = is_duplicate_entry(address, cost);
	printf("\ncost = %d", cost);
	if (false == status)
	{
		printf("\nNew Neighbour found");
		neighbors *temp = memb_alloc(&neighbor_mem);
		/*		neighbors temp;,_from_animal_mote.text
		 linkaddr_copy(&temp.nbr_addr,address);
		 temp.hop_count = cost+1;
		 list_add(nbr_list,&temp);
		 print_routing_table();*/
		if (temp != NULL)
		{
			linkaddr_copy(&temp->nbr_addr, address);
			temp->hop_count = cost + 1;
			list_add(nbr_list, temp);
			print_routing_table();
		}
		else
		{
			printf("mem alloc failed \r\n");
		}
	}
}

static void inform_neighbor(const linkaddr_t * address)
{
	route_packet tx_payload;
	strcpy(tx_payload.text, "Gateway!");
	strcpy(tx_payload.type, "unicast");
	tx_payload.hops = 0;
	//tx_payload.hops = 5;
	packetbuf_copyfrom(&tx_payload, 80);
	//packetbuf_set_attr(PACKETBUF_ATTR_HOPS,0);
	//packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,UNICAST);
	packetbuf_set_addr(PACKETBUF_ADDR_ESENDER, 1);
	printf("\nSending Unicast packet: type : %s",tx_payload.type);
	unicast_send(&unicast, address);
}

void estimate_short_path()
{
	//This for iteration purpose only
	static neighbors *tmp_nearest;
	static neighbors temp_nearest_node;

	if (list_length(nbr_list))
	{
		tmp_nearest = (neighbors *) list_head(nbr_list);

		temp_nearest_node.hop_count = tmp_nearest->hop_count;
		linkaddr_copy(&temp_nearest_node.nbr_addr, &tmp_nearest->nbr_addr);

		static neighbors *e;
		for (e = (neighbors *) tmp_nearest->next; e != NULL;
				e = (neighbors *) e->next)
		{
			if (e->hop_count < temp_nearest_node.hop_count)
			{
				temp_nearest_node.hop_count = e->hop_count;
				linkaddr_copy(&temp_nearest_node.nbr_addr, &e->nbr_addr);
			}
		}
		linkaddr_copy(&nearest_neighbor.addr, &temp_nearest_node.nbr_addr);

		nearest_neighbor.cost = temp_nearest_node.hop_count;

	}
	else
	{
		printf("list empty\r\n");
	}
}

void share_routing_table()
{
	estimate_short_path();
	printf("\nNeighbor Nearest to Gateway (by hop count): %x%x",
			nearest_neighbor.addr.u8[0], nearest_neighbor.addr.u8[1]);

	route_packet payload;
	static neighbors *e = NULL;
	e = (neighbors *) list_head(nbr_list);
	for (e = (neighbors *) list_head(nbr_list); e != NULL;
			e = (neighbors *) e->next)
	{
		if (node_id_new == 1)	//Gateway node
		{
			nearest_neighbor.cost = 0;
			nearest_neighbor.addr.u8[0] = 0x00;
			nearest_neighbor.addr.u8[1] = 0x01;
		}
		strcpy(payload.text, "Share Route Table");
		strcpy(payload.type, "unicast");
		payload.hops = nearest_neighbor.cost;
		//payload.hops = 7;
		packetbuf_copyfrom(&payload, 80);
		//packetbuf_set_attr(PACKETBUF_ATTR_HOPS,nearest_neighbor.cost);
		//packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,UNICAST);
		packetbuf_set_addr(PACKETBUF_ADDR_ESENDER, &nearest_neighbor.addr);
		printf("\nSending Unicast message to %x%x: Hops = %d type = %s",
				e->nbr_addr.u8[0], e->nbr_addr.u8[1], payload.hops,payload.type);
		unicast_send(&unicast, &e->nbr_addr);
	}

}

#ifndef ASSET_MOTE

/**
 *
 * @param from address from whom this packet is received
 * @param is_broadcast this variable represents whether the packet received is
 * 			broadcast packet or unicast packet
 * @return returns true always as of now.
 */
static void forward_data(const linkaddr_t *from, bool broadcast_packet)
{
	static uint16_t rssi = 0;
	static route_packet packet;
	char sndr_addr[10];
	if (gateway_found == false)
	{
		printf("Asset packet received but gateway not found return \r\n");
		//return false;
	}
	packetbuf_copyto(&packet);
	printf("\nText = %s; Type = %s; Hops = %d; rssi = %d; path = %s\n",
			packet.text, packet.type, packet.hops, packet.rssi, packet.path);
	if (broadcast_packet)
	{
		packet.rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
	}
	sprintf(sndr_addr, "%d", ((from->u8[0] << 8) | from->u8[1]));
	strcat(packet.path, sndr_addr);
	printf("\nText = %s; Type = %s; Hops = %d; rssi = %d; path = %s\n",
			packet.text, packet.type, packet.hops, packet.rssi, packet.path);
	printf("Neighbor Address: %x%x",nearest_neighbor.addr.u8[0],nearest_neighbor.addr.u8[1]);
	if (nearest_neighbor.addr.u8[0] != 0 && nearest_neighbor.addr.u8[1] != 0)
	{
		packetbuf_copyfrom(&packet, 80);
		printf("\nForwarding unicast packet: '%s' RSSI %d type: %s path:%s\n",
						packet.text, packet.rssi, packet.type,
						packet.path);
		//packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,ASSET_TRACKING);
		//Later unicast it to nearby nearest neighbor
		//unicast_send(&unicast, &nearest_neighbor.addr);

	}

	//return true;
}
#endif //ASSET_MOTE

// Defines the behavior of a connection upon receiving data.
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
#ifndef MOBILE_MOTE
	printf("RSSI %d = \n", (int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI));
	static route_packet rx_packet;
	bool a;
	char sndr_addr[20];

	packetbuf_copyto(&rx_packet);

	if ((int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI) > (-60))
	{
		leds_on(LEDS_GREEN);
		printf(
				"\nBroadcast message received from 0x%x%x: '%s' [RSSI %d] type: %s\n",
				from->u8[0], from->u8[1], rx_packet.text,
				(int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI), rx_packet.type);
		//Here i will add only the neighbor entry to the table
		//with infinite hop count
		if (!strcmp(rx_packet.type, "TRACKING_MOTE")) //strcmp returns 0 if 2 strings are equal
		{
			printf("\nAdd neighbour to list: ");
			//Adding neighbors to the list
			add_routing_entry(from, INFINITE_HOP_COUNT);
			if (node_id_new == 1) //Gateway node
			{
				inform_neighbor(from);
			}
		}
		if (!strcmp(rx_packet.type, "MOBILE_MOTE"))
		{
			if (node_id_new != 1) //not Gateway node
			{
				//forward_data(from, true);
				if (gateway_found == false)
				{
					printf("Asset packet received but gateway not found \r\n");
				}
				rx_packet.rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
				sprintf(sndr_addr, "%d", ((from->u8[0] << 8) | from->u8[1]));
				strcat(rx_packet.path, sndr_addr);
				if (nearest_neighbor.addr.u8[0] != 0 && nearest_neighbor.addr.u8[1] != 0)
				{
					packetbuf_copyfrom(&rx_packet, 80);
					printf("\nForwarding unicast packet: '%s' RSSI %d type: %s path:%s\n",
									rx_packet.text, rx_packet.rssi, rx_packet.type,
									rx_packet.path);
					//packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,ASSET_TRACKING);
					//Later unicast it to nearby nearest neighbor
					unicast_send(&unicast, &nearest_neighbor.addr);

				}
				printf("abc");
			}
			else
			{
				//uint16_t rssi, packet_len;
				//packet_len = packetbuf_datalen();
				//printf("rssi at broadcast receive: %x\r\n",rssi);
				rx_packet.rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
				strcat(rx_packet.path,
						(char) ((from->u8[0] << 8) | from->u8[1]));
				linkaddr_t *asset_addr = (linkaddr_t*) (packetbuf_dataptr() + 7);
				/*if (linkaddr_cmp(&asset1_addr, asset_addr))
				 {
				 process_packet_gateway_asset1(packetbuf_dataptr(),
				 packet_len + 4);
				 }
				 if (linkaddr_cmp(&asset2_addr, asset_addr))
				 {
				 process_packet_gateway_asset2(packetbuf_dataptr(),
				 packet_len + 4);
				 }*/
			}
			//unicast_send(&unicast, &nearest_neighbor.addr);
		}
		leds_off(LEDS_GREEN);
	}
#endif //MOBILE_MOTE
}

// Defines the behavior of a connection upon receiving data.
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
#ifndef MOBILE_MOTE
	static route_packet rx_packet_uni;
	bool a;
	//printf("\nCheck fot unicast(1) = %d",packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE));
	if ((int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI) > (-60))
	{
		leds_on(LEDS_GREEN);
		packetbuf_copyto(&rx_packet_uni);
		printf(
				"\nUnicast message received from 0x%x%x: %s Hops = %d, Type = %s [RSSI %d]\n",
				from->u8[0], from->u8[1], rx_packet_uni.text,
				rx_packet_uni.hops, rx_packet_uni.type,
				(int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI));

		if (!gateway_found)
			gateway_found = true;

		if (!strcmp(rx_packet_uni.type, "unicast")) //strcmp returns 0 if 2 strings are equal
		{
			add_routing_entry(from, rx_packet_uni.hops);
			print_routing_table();
		}
		if (!strcmp(rx_packet_uni.type, "MOBILE_MOTE"))
		{
			if (node_id_new != 1) //not Gateway node
			{
				forward_data(from, false);
				printf("\n%b",a);
			}
			else
			{
				//uint16_t rssi, packet_len;
				//packet_len = packetbuf_datalen();
				//printf("rssi at broadcast receive: %x\r\n",rssi);
				rx_packet_uni.rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
				strcat(rx_packet_uni.path,
						(char) ((from->u8[0] << 8) | from->u8[1]));
				linkaddr_t *asset_addr = (linkaddr_t*) (packetbuf_dataptr() + 7);
				/*if (linkaddr_cmp(&asset1_addr, asset_addr))
				 {
				 process_packet_gateway_asset1(packetbuf_dataptr(),
				 packet_len + 4);
				 }
				 if (linkaddr_cmp(&asset2_addr, asset_addr))
				 {
				 process_packet_gateway_asset2(packetbuf_dataptr(),
				 packet_len + 4);
				 }*/
			}
		}
		leds_off(LEDS_GREEN);
	}
#endif //MOBILE_MOTE
}

int node_id_map(int node_id)
{
	int node_id_new;
	switch (node_id)
	{
	case 0xee65:
		node_id_new = 8;
		break;
	case 0xee66:
		node_id_new = 7;
		break;
	case 0xef31:
		node_id_new = 6;
		break;
	case 0xf46c:
		node_id_new = 5;
		break;
	case 0xf442:
		node_id_new = 4;
		break;
	case 0x2dc1:
		node_id_new = 3;
		break;
	case 0xed9c:
		node_id_new = 2;
		break;
	case 0xef1c:
		node_id_new = 1;
		break; //Gateway node
	default:
		node_id_new = 0;
		break;
	}
	return node_id_new;
}

// Defines the functions used as callbacks for a broadcast connection.
static const struct broadcast_callbacks broadcast_call =
{ broadcast_recv };

// Defines the functions used as callbacks for a unicast connection.
static const struct unicast_callbacks unicast_call =
{ unicast_recv };

//--------------------- PROCESS CONTROL BLOCK ---------------------
PROCESS(flooding_process, "Flooding Process");
PROCESS(route_share_process, "Routing table sharing");
AUTOSTART_PROCESSES(&flooding_process, &route_share_process);

//------------------------ PROCESS' THREAD ------------------------
PROCESS_THREAD(flooding_process, ev, data)
{

	PROCESS_EXITHANDLER(broadcast_close(&broadcast)
	;
	)
	PROCESS_BEGIN()
		;

		static int broadcast_counter = 0;
		static uint8_t timer_interval = 3;	// In seconds

		static struct etimer broadcast_timer;

		route_packet _from_animal_mote;
		route_packet _from_tracking_mote;

		// Configure your team's channel (11 - 26).
		NETSTACK_CONF_RADIO.set_value(RADIO_PARAM_CHANNEL, 14);

		print_settings();

		// Open broadcast connection.
		broadcast_open(&broadcast, 129, &broadcast_call);
		unicast_open(&unicast, 146, &unicast_call);

		// node_id = (linkaddr_node_addr.u8[1] & 0xFF);
		node_id = ((linkaddr_node_addr.u8[0] << 8) | linkaddr_node_addr.u8[1]);
		printf("node id = %x\n", node_id);
		node_id_new = node_id_map(node_id);
		printf("new node id = %x\n", node_id_new);

		etimer_set(&broadcast_timer,
		CLOCK_SECOND * timer_interval * random_rand() / RANDOM_RAND_MAX);
		while (1)
		{
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&broadcast_timer));
			if (node_id_new != 1) //not gateway mote
			{
				if (node_id_new == 7 || node_id_new == 8) //mobile mote
				{
					leds_on(LEDS_RED);
					strcpy(_from_animal_mote.text, "Message from Animal Mote!");
					strcpy(_from_animal_mote.type, "MOBILE_MOTE");
					_from_animal_mote.hops = INFINITE_HOP_COUNT;
					packetbuf_copyfrom(&_from_animal_mote, 80);
					broadcast_send(&broadcast);
					printf("\nBroadcast message %d sent.\n", broadcast_counter);
					broadcast_counter++;
					etimer_set(&broadcast_timer,
					CLOCK_SECOND * 5 * random_rand() / RANDOM_RAND_MAX);
					leds_off(LEDS_RED);
				}
				else
				{
					if (broadcast_counter < 10)
					{
						leds_on(LEDS_RED);
						strcpy(_from_tracking_mote.text,
								"Message from Tracking Mote!");
						strcpy(_from_tracking_mote.type, "TRACKING_MOTE");
						_from_tracking_mote.hops = INFINITE_HOP_COUNT;
						packetbuf_copyfrom(&_from_tracking_mote, 80);
						broadcast_send(&broadcast);
						printf("\nBroadcast message %d sent.\n",
								broadcast_counter);
						broadcast_counter++;
						etimer_set(&broadcast_timer,
						CLOCK_SECOND * 2 * random_rand() / RANDOM_RAND_MAX);
						leds_off(LEDS_RED);
					}
					else
					{
						/* If broadcast period finished*/
						printf(
								"--------------Stopping the broadcast-------------------- \n\r");
						etimer_stop(&broadcast_timer);
						//			print_routing_table();
						//			check_gateway();
						//			gBroadcast_ended=true;
						broadcast_counter = 0;
						process_post(&route_share_process,
						PROCESS_EVENT_MSG, 0);
						//PROCESS_EXIT(); // Exit the process.
					}
				}

			}
		}

	PROCESS_END();
}

PROCESS_THREAD(route_share_process, ev, data)
{
PROCESS_EXITHANDLER(printf("****route_share_process terminated!***\r\n")
;
)
PROCESS_BEGIN()
	;

	static struct etimer route_share_etimer;
	etimer_set(&route_share_etimer,
	CLOCK_SECOND * ROUTE_SHARE_INTERVAL + (random_rand() % 5) / 5);

	while (1)
	{
		PROCESS_WAIT_EVENT()
		;
		if (ev == PROCESS_EVENT_MSG)
		{
			if (etimer_expired(&route_share_etimer))
			{
				printf("Found Gateway: %d\n", gateway_found);
				if (gateway_found)
				{
					printf("\nSharing routing table..");
					share_routing_table();
				}
				etimer_set(&route_share_etimer,
				CLOCK_SECOND * ROUTE_SHARE_INTERVAL + (random_rand() % 5) / 5);
			}

			//etimer_set(&route_share_etimer, CLOCK_SECOND*ROUTE_SHARE_INTERVAL+(random_rand()%5)/5);
			process_post(&route_share_process, PROCESS_EVENT_MSG, 0);
		}
	}
PROCESS_END();
}

