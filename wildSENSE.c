
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
MEMB(neighbor_mem,neighbors, MAX_NEIGHBORS);

static int rssi_arr[MAX_NEIGHBORS][3];

//------------------------ FUNCTIONS ------------------------
// Prints the current settings.
static void print_settings(void){
	radio_value_t channel;

	NETSTACK_CONF_RADIO.get_value(RADIO_PARAM_CHANNEL,&channel);

	printf("\n-------------------------------------\n");
	printf("RIME addr = \t0x%x%x\n",
			linkaddr_node_addr.u8[0],
			linkaddr_node_addr.u8[1]);
	printf("Using radio channel %d\n", channel);
	printf("---------------------------------------\n");
}

bool is_duplicate_entry(const linkaddr_t *address,uint8_t cost)
{
	//This for iteration purpose only
	neighbors *e;
    for(e = (neighbors *)list_head(nbr_list);e != NULL; e = (neighbors *)e->next) {

        if(linkaddr_cmp(address, &e->nbr_addr)) {
        	//e->hop_count = cost+1;
        	//ctimer_set(&e->node_timer,CLOCK_SECOND*TIME_TO_LIVE,remove_nbr,e);
        	return true;
        }
    }
	return false;
}

void print_routing_table()
{
	printf("Print Neighbour list");
	neighbors *e;
//	printf("%s\r\n",__func__);
    for(e = (neighbors *)list_head(nbr_list); e != NULL;e = (neighbors *)e->next) {
    	printf("Address %x.%x: in %d\r\n",e->nbr_addr.u8[0], e->nbr_addr.u8[1], e->hop_count);
    }
    printf("\r\n");
}

void add_routing_entry(const linkaddr_t *address,uint8_t cost)
{
	bool status = false;
	status = is_duplicate_entry(address,cost);
	if(false == status)
	{
		printf("New Neighbour found");
		neighbors *temp = memb_alloc(&neighbor_mem);
/*		neighbors temp;
		linkaddr_copy(&temp.nbr_addr,address);
		temp.hop_count = cost+1;
		list_add(nbr_list,&temp);
		print_routing_table();*/
		if(temp != NULL)
		{
			linkaddr_copy(&temp->nbr_addr,address);
			temp->hop_count = cost+1;
			list_add(nbr_list,temp);
			print_routing_table();
		}
		else
		{
			printf("mem alloc failed \r\n");
		}
	}
}

// Defines the behavior of a connection upon receiving data.
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
	printf("RSSI %d = \n",(int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI));

	if((int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI)>(-60))
	{
		leds_on(LEDS_GREEN);
		printf("Broadcast message received from 0x%x%x: '%s' [RSSI %d]\n",
				 from->u8[0], from->u8[1],
				(char *)packetbuf_dataptr(),
				(int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI));
		//Here i will add only the neighbor entry to the table
		//with infinite hop count
		if(BROADCAST == packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE))
		{
			printf("Add neighbour to list ");
			//Adding neighbors to the list
			add_routing_entry(from,INFINITE_HOP_COUNT);
			/*if(i_am_gateway)
			{
				inform_neighbor(from);
			}*/
		}

	/*leds_on(LEDS_GREEN);
	 printf("Broadcast message received from 0x%x%x: '%s' [RSSI %d]\n",
			 from->u8[0], from->u8[1],
			(char *)packetbuf_dataptr(),
			(int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI));*/
	leds_off(LEDS_GREEN);
}
}

void node_id_map()
{
	switch(node_id)
	{
		case 0xee65: node_id_new = 8; break;
		case 0xee66: node_id_new = 7; break;
		case 0xef31: node_id_new = 6; break;
		case 0xf46c: node_id_new = 5; break;
		case 0xf442: node_id_new = 4; break;
		case 0x2dc1: node_id_new = 3; break;
		case 0xed9c: node_id_new = 2; break;
		case 0xef1c: node_id_new = 1; break; //Gateway node
		default: node_id_new = 0; break;
	}
}


// Creates an instance of a broadcast connection.
static struct broadcast_conn broadcast;

// Defines the functions used as callbacks for a broadcast connection.
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};


//--------------------- PROCESS CONTROL BLOCK ---------------------
PROCESS(flooding_process, "Flooding Process");
AUTOSTART_PROCESSES(&flooding_process);

//------------------------ PROCESS' THREAD ------------------------
PROCESS_THREAD(flooding_process, ev, data) {

	PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
	PROCESS_BEGIN();

	static int broadcast_counter = 0;
	static uint8_t timer_interval = 3;	// In seconds

	static struct etimer broadcast_timer;

	// Configure your team's channel (11 - 26).
	NETSTACK_CONF_RADIO.set_value(RADIO_PARAM_CHANNEL,14);

	print_settings();

	// Open broadcast connection.
	broadcast_open(&broadcast, 129, &broadcast_call);

	// node_id = (linkaddr_node_addr.u8[1] & 0xFF);
	node_id = ((linkaddr_node_addr.u8[0]<<8) | linkaddr_node_addr.u8[1]);
	printf("node id = %x\n",node_id);
	node_id_map(node_id);
	printf("new node id = %x\n",node_id_new);


	while(1) {
		etimer_set(&broadcast_timer, CLOCK_SECOND * timer_interval);

		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&broadcast_timer));
		if (node_id != 1)
		{
			if(broadcast_counter < 10)
					{
						leds_on(LEDS_RED);
						packetbuf_copyfrom("I am your neighbor", 20);
						packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,INFINITE_HOP_COUNT);
						packetbuf_set_attr(PACKETBUF_ATTR_HOPS,BROADCAST);
						broadcast_send(&broadcast);
						printf("Broadcast message %d sent.\n",broadcast_counter);
						broadcast_counter++;
						leds_off(LEDS_RED);
					}
					else
					{
						/* If broadcast period finished*/
						printf("--------------Stopping the broadcast-------------------- \n\r");
						etimer_stop(&broadcast_timer);
			//			print_routing_table();
			//			check_gateway();
			//			gBroadcast_ended=true;
						broadcast_counter=0;
						PROCESS_EXIT(); // Exit the process.
					}
		}
	}

	PROCESS_END();
}



