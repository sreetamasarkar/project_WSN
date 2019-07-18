#define MOBILE_MOTE 1 //Uncomment for downloading code to mobile motes

/*----------------------------INCLUDES----------------------------------------*/
// standard C includes:
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Contiki-specific includes:
#include "contiki.h"
#include "net/rime/rime.h"     // Establish connections.
#include "net/netstack.h"      // Wireless-stack definitions
#include "dev/button-sensor.h" // User Button
#include "dev/leds.h"          // Use LEDs.
#include "core/net/linkaddr.h"
#include "dev/adc-zoul.h"      // ADC
#include "dev/zoul-sensors.h"  // Sensor functions
#include "dev/sys-ctrl.h"

#include "helpers.h"

static uint16_t node_id;		    // Stores node id
static uint16_t node_id_new;		        // Stores node id mapped from 1,..,8
static bool ListEmpty;				//Checks id neighbor list is empty
nodes nearest_nbr;
static bool gateway_reached = false;
static int broadcast_ended = 0;

LIST(neighbor_list);
MEMB(mem_alloc_neighbor, neighbor, MAX_NEIGHBORS);

// Creates an instance of a broadcast connection.
static struct broadcast_conn broadcast;

// Creates an instance of a unicast connection.
static struct unicast_conn unicast;

uint16_t node_id_map(uint16_t node_id);

//--------------------- PROCESS CONTROL BLOCK ---------------------
PROCESS(process_broadcast, "Broadcast Packet Process");
PROCESS(process_cost_share, "Routing table sharing");
AUTOSTART_PROCESSES(&process_broadcast, &process_cost_share);


//------------------------------------- FUNCTIONS -----------------------------------

/*
 * Prints the current settings.
 */
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

/*
 * Removes neighbor from which packets are not received for a certain time
 */
void neighbor_remove(void *n)
{
	neighbor *e = n;
	int count1 = 0, count2 = 0;
	route_packet gw;
	int gw_broadcast_counter = 0;
	static struct etimer gw_broadcast_timer;

//	int count1 = 0, count2 = 0;

	e->cost = INFINITE_HOP_COUNT;
	printf("*neighbor_removed: %x%x\r\n",e->address.u8[0],e->address.u8[1]);
	for (e = (neighbor *) list_head(neighbor_list); e != NULL;
				e = (neighbor *) e->next)
	{
		count1 = count1 +1;
		if (e->cost >= 10)
			count2 = count2 +1;
	}
	if ((count1 - count2) <= 1 )
	{
		if (node_id_new == 1)
		{
			if (etimer_expired(&gw_broadcast_timer))
			{
				if (gw_broadcast_counter < 10)
				{
					leds_on(LEDS_RED);
					strcpy(gw.text,
							"Gateway!");
					strcpy(gw.type, "TM");
					gw.hops = 0;

					packetbuf_clear();
					packetbuf_copyfrom(&gw, 80);
					broadcast_send(&broadcast);
					printf("\nBroadcast message %d sent.\n",
							gw_broadcast_counter);
					gw_broadcast_counter++;
					etimer_set(&gw_broadcast_timer,
					CLOCK_SECOND * 2 * random_rand() / RANDOM_RAND_MAX);
					leds_off(LEDS_RED);
				}
			}
		}
		else
			process_post(&process_broadcast, PROCESS_EVENT_MSG, 0);
	}
//	printf("\ncount1 = %d count2 = %d\n",count1,count2);
//	list_remove(neighbor_list,e);
//	memb_free(&mem_alloc_neighbor,e);
//	if(!list_length(neighbor_list))
//	{
//		printf("list length zero making ListEmpty true\r\n");
//		ListEmpty = true;
//	}
}

/*
 * Updates cost of neighbors which are already in list
 * @param address is the neighbor address from which is receives unicast packets
 * @param cost is the cost of nearest neighbor of that neighbor from ehich is receives packets
 * @return returns true if neighbor in list, false otherwise
 *
 */
bool update_nbr_cost(const linkaddr_t *address, unsigned int cost)
{
	//Traversing the neighbor list
	neighbor *e;
	for (e = (neighbor *) list_head(neighbor_list); e != NULL;
			e = (neighbor *) e->next)
	{

		if (linkaddr_cmp(address, &e->address))
		{
			e->cost = cost + 1;
			ctimer_set(&e->node_timer,CLOCK_SECOND*TIME_TO_LIVE,neighbor_remove,e);
			return true;
		}
	}
	return false;
}

/*
 * Prints Neighbor list with cost at each Tracking Mote
 */
void print_nbr_list()
{
	neighbor *e;

	printf("\nPrint Neighbour list: ");
	for (e = (neighbor *) list_head(neighbor_list); e != NULL;
			e = (neighbor *) e->next)
	{
		uint16_t neighbor_addr = node_id_map(((e->address.u8[0] << 8) | e->address.u8[1]));
		printf("\nAddress: %d Cost: %d\r\n", neighbor_addr, e->cost);
	}
	printf("\r\n");
}

/*
 * @brief This functions checks if neighbor sending route packet is already in list. If it is already in list,
 * its cost is updated, otherwise it is added to list.
 * @param address is the neighbor address from which is receives unicast packets
 * @param cost is the cost of nearest neighbor of that neighbor from ehich is receives packets
 */

void add_to_nbr_list(const linkaddr_t *address, unsigned int cost)
{
	bool already_in_list = false;
	already_in_list = update_nbr_cost(address, cost);
	printf("\ncost = %d", cost);
	if (already_in_list == false)
	{
		printf("\nNew Neighbour found");
		neighbor *temp = memb_alloc(&mem_alloc_neighbor);

		if (temp != NULL)
		{
			linkaddr_copy(&temp->address, address);
			temp->cost = cost + 1;
			list_add(neighbor_list, temp);
			ctimer_set(&temp->node_timer,CLOCK_SECOND*TIME_TO_LIVE,neighbor_remove,temp);
			print_nbr_list();
		}
		else
		{
			printf("Out of memory!! \r\n");
		}
	}
}

/*
 * This function is called at the Gateway when it receives broadcast packets from tracking mote
 * to inform its neighbors
 * @param address is the neighbor address from which is receives unicast packets
 */
static void gw_to_neighbor(const linkaddr_t * address)
{
	leds_on(LEDS_BLUE);
	route_packet tx_payload;
	strcpy(tx_payload.text, "Gateway!");
	strcpy(tx_payload.type, "uni");
	tx_payload.hops = 0;
	packetbuf_clear();
	packetbuf_copyfrom(&tx_payload, 80);
	printf("\nSending Unicast packet: type : %s\n",tx_payload.type);
	unicast_send(&unicast, address);
	leds_off(LEDS_BLUE);
}

/*
 * This function goes through the neighbor list and stores the address and cost of
 * the neighbor with minimum cost in the structure nearest_nbr
 */
void calculate_nearest_neighbor()
{
	static neighbor *tmp_nearest;
	static neighbor temp_nearest_node;

	if (list_length(neighbor_list))
	{
		tmp_nearest = (neighbor *) list_head(neighbor_list);

		temp_nearest_node.cost = tmp_nearest->cost;
		linkaddr_copy(&temp_nearest_node.address, &tmp_nearest->address);

		static neighbor *e;
		for (e = (neighbor *) tmp_nearest->next; e != NULL;
				e = (neighbor *) e->next)
		{
			if (e->cost < temp_nearest_node.cost)
			{
				temp_nearest_node.cost = e->cost;
				linkaddr_copy(&temp_nearest_node.address, &e->address);
			}
		}

		linkaddr_copy(&nearest_nbr.addr, &temp_nearest_node.address);
		nearest_nbr.cost = temp_nearest_node.cost;

	}
	else
	{
		printf("Neighbor list empty!!!!\r\n");
	}
}
/*
 * This function checks its neighbor list and shares the cost of its nearest neighbor
 */
void nn_cost_share()
{

	calculate_nearest_neighbor();
	printf("\nNeighbor Nearest to Gateway (by hop count): %x%x",
			nearest_nbr.addr.u8[0], nearest_nbr.addr.u8[1]);

	route_packet payload;
	static neighbor *e = NULL;
	e = (neighbor *) list_head(neighbor_list);
	for (e = (neighbor *) list_head(neighbor_list); e != NULL;
			e = (neighbor *) e->next)
	{
		leds_on(LEDS_BLUE);
		if (node_id_new == 1)	//Gateway node
		{
			nearest_nbr.cost = 0;
			nearest_nbr.addr.u8[0] = 0x00;
			nearest_nbr.addr.u8[1] = 0x01;
		}
		strcpy(payload.text, "Share Route Table");
		strcpy(payload.type, "uni");
		payload.hops = nearest_nbr.cost;
		packetbuf_clear();
		packetbuf_copyfrom(&payload, 80);

		printf("\nSending Unicast message to %x%x: Hops = %d type = %s \n",
				e->address.u8[0], e->address.u8[1], payload.hops,payload.type);
		unicast_send(&unicast, &e->address);
		leds_off(LEDS_BLUE);
	}

}


/**
 * @param from address from whom this packet is received
 * @param is_broadcast this variable represents whether the packet received is
 * 			broadcast packet or unicast packet
 * @return returns true always as of now.
 */
static void animal_packet_forward(const linkaddr_t *from, bool broadcast_packet)
{
	static route_packet packet;
	char sndr_addr[10];
	if (gateway_reached == false)
	{
		printf("Gateway not in network!! \r\n");
		//return false;
	}
	packetbuf_copyto(&packet);
//	printf("\nText = %s; Type = %s; Hops = %d; rssi = %d; path = %s\n",
//			packet.text, packet.type, packet.hops, packet.rssi, packet.path);
	if (broadcast_packet)
	{
		packet.rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
	}
	sprintf(sndr_addr, "%x-", ((from->u8[0] << 8) | from->u8[1]));
	strcat(packet.path, sndr_addr);
//	printf("\nText = %s; Type = %s; Hops = %d; rssi = %d; path = %s\n",
//			packet.text, packet.type, packet.hops, packet.rssi, packet.path);
	printf("Neighbor Address: %x%x",nearest_nbr.addr.u8[0],nearest_nbr.addr.u8[1]);
	if (nearest_nbr.addr.u8[0] != 0 && nearest_nbr.addr.u8[1] != 0)
	{
		leds_on(LEDS_PURPLE);
		packetbuf_clear();
		packetbuf_copyfrom(&packet, 80);
		printf("\nForwarding unicast packet: '%s' RSSI %d type: %s path:%s\n",
						packet.text, packet.rssi, packet.type,
						packet.path);
		//Later unicast it to nearby nearest neighbor
		unicast_send(&unicast, &nearest_nbr.addr);
		leds_off(LEDS_PURPLE);

	}

}

/*
 * This function checks to which tracking mote the animal 1 is closest, that is, rssi is max
 * and prints the shortest path from animal mote 1 to gateway
 */
static void path_to_gateway_animal1(route_packet packet_gw)
{
	static int16_t prev_rssi = -100,current_rssi = 0;

	static char prev_addr[20];
	char path[20];
	char gw_id[20];
	current_rssi = packet_gw.rssi;

	//adding gateway address to the end of the packet
	sprintf(gw_id, "%x", (node_id));
	strcat(packet_gw.path, gw_id);
	strcpy(path,packet_gw.path);

	//Parse path
	char *delim = "-";
	char *current_addr = strtok(path, delim);
	current_addr = strtok(NULL, delim);

	//printf("Battery:30-Temp:45-Heartbeat:65-%s\n",packet_gw.path);
	//printf("path: %s\n",packet_gw.path);
	printf("current address: %s prev address: %s prev_rssi = %d current_rssi= %d\n",current_addr,prev_addr,prev_rssi,current_rssi);

	//Parse packet text
	char *text = packet_gw.text;
	char *delim1 = "Bat: TempHb";
	char *bat_val = strtok(text, delim1);
	char *temp_val = strtok(NULL, delim1);
	char *hb_val = strtok(NULL, delim1);

	//printf("Battery: %s Temperature: %s Hearbeat: %s\n",bat_val,temp_val,hb_val);

	if(strcmp(current_addr,prev_addr)) //strcmp returns 1 if 2 address are not same
	{
		//printf("curr addr != prev addr");
		if(current_rssi > (prev_rssi+10) )
		{
			//printf("**** current_rssi > (prev_rssi + 10) **\r\n");
			strcpy(prev_addr,current_addr);
			prev_rssi = current_rssi;
			printf("Battery:%s-Temp:%s-Heartbeat:%s-%s\n",bat_val,temp_val,hb_val,packet_gw.path);
		}
	}
	else
	{
		prev_rssi = current_rssi;
		printf("Battery:%s-Temp:%s-Heartbeat:%s-%s\n",bat_val,temp_val,hb_val,packet_gw.path);
	}
}

/*
 * This function checks to which tracking mote the animal 2 is closest, that is, rssi is max
 * and prints the shortest path from animal mote 2 to gateway
 */

static void path_to_gateway_animal2(route_packet packet_gw)
{
	static int16_t prev_rssi = -100,current_rssi = 0;

	static char prev_addr[20];
	char path[20];
	char gw_id[20];
	current_rssi = packet_gw.rssi;

	//adding gateway address to the end of the packet
	sprintf(gw_id, "%x", (node_id));
	strcat(packet_gw.path, gw_id);
	strcpy(path,packet_gw.path);

	//Parse path
	char *delim = "-";
	char *current_addr = strtok(path, delim);
	current_addr = strtok(NULL, delim);

	//printf("Battery:30-Temp:45-Heartbeat:65-%s\n",packet_gw.path);
	//printf("path: %s\n",packet_gw.path);
	printf("current address: %s prev address: %s prev_rssi = %d current_rssi= %d\n",current_addr,prev_addr,prev_rssi,current_rssi);

	//Parse packet text
	char *text = packet_gw.text;
	char *delim1 = "Bat: TempHb";
	char *bat_val = strtok(text, delim1);
	char *temp_val = strtok(NULL, delim1);
	char *hb_val = strtok(NULL, delim1);

	//printf("Battery: %s Temperature: %s Hearbeat: %s\n",bat_val,temp_val,hb_val);

	if(strcmp(current_addr,prev_addr)) //strcmp returns 1 if 2 address are not same
	{
		//printf("curr addr != prev addr");
		if(current_rssi > (prev_rssi+10) )
		{
			//printf(" current_rssi > (prev_rssi + 10) \r\n");
			strcpy(prev_addr,current_addr);
			prev_rssi = current_rssi;
			printf("Battery:%s-Temp:%s-Heartbeat:%s-%s\n",bat_val,temp_val,hb_val,packet_gw.path);
		}
	}
	else
	{
		prev_rssi = current_rssi;
		printf("Battery:%s-Temp:%s-Heartbeat:%s-%s\n",bat_val,temp_val,hb_val,packet_gw.path);
	}
}

/*
 * Defines the behavior of a connection upon receiving broadcast data
 */
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
#ifndef MOBILE_MOTE
	printf("\n\n %x%x RSSI Broadcast = %d \n", from->u8[0], from->u8[1], (int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI));
	static route_packet rx_packet;
	bool a;
	char s_addr[20];
	char animal_mote_addr[20];
	packetbuf_copyto(&rx_packet);

	if ((int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI) > (-80))
	{
		leds_on(LEDS_GREEN);
		printf(
				"\nBroadcast message received from 0x%x%x: '%s' [RSSI %d] type: %s\n",
				from->u8[0], from->u8[1], rx_packet.text,
				(int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI), rx_packet.type);

		if (!strcmp(rx_packet.type, "TM")) //strcmp returns 0 if 2 strings are equal
		{
			printf("\nAdd neighbour to list: ");
			//Adding neighbors to the list
			add_to_nbr_list(from, INFINITE_HOP_COUNT);
			if (node_id_new == 1) //Gateway node
			{
				gw_to_neighbor(from);
				//process_post(&process_cost_share,PROCESS_EVENT_MSG, 0);
			}
		}
		if (!strcmp(rx_packet.type, "MM"))
		{
			if (node_id_new != 1) //not Gateway node
			{
				animal_packet_forward(from, true);
			}
			else
			{
				rx_packet.rssi = (int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI);
				sprintf(s_addr, "%x-", ((from->u8[0] << 8) | from->u8[1]));
				sprintf(animal_mote_addr, "%x",((from->u8[0] << 8) | from->u8[1]));
				strcat(rx_packet.path, s_addr);
				printf("animal mote addr: %s\n",animal_mote_addr);
				if (!strcmp(animal_mote_addr, "ee65"))
				{
					path_to_gateway_animal1(rx_packet);
				}
				 if (!strcmp(animal_mote_addr, "ee66"))
				 {
					 path_to_gateway_animal2(rx_packet);
				 }
			}
		}
		packetbuf_clear();
		leds_off(LEDS_GREEN);
	}
#endif //MOBILE_MOTE
}

/*
 * Defines the behavior of a connection upon receiving unicast data
 */
static void unicast_recv(struct unicast_conn *c, const linkaddr_t *from)
{
#ifndef MOBILE_MOTE
	static route_packet rx_packet_uni;
	char s_addr[20];
	char path[20];
	printf("\n %x%x RSSI Unicast = %d \n", from->u8[0], from->u8[1], (int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI));
	if ((int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI) > (-80))
	{
		leds_on(LEDS_GREEN);
		packetbuf_copyto(&rx_packet_uni);
		printf(
				"\nUnicast message received from 0x%x%x: %s Hops: %d, Type: %s path: %s [RSSI %d]\n",
				from->u8[0], from->u8[1], rx_packet_uni.text,
				rx_packet_uni.hops, rx_packet_uni.type,rx_packet_uni.path,
				(int16_t) packetbuf_attr(PACKETBUF_ATTR_RSSI));
		packetbuf_clear();
		if (!gateway_reached)
			gateway_reached = true;
		printf("\nIn function unicast receive: gateway_reached = %d",gateway_reached);

		if (!strcmp(rx_packet_uni.type, "uni")) //strcmp returns 0 if 2 strings are equal
		{
			add_to_nbr_list(from, rx_packet_uni.hops);
			print_nbr_list();
		}
		if (!strcmp(rx_packet_uni.type, "MM"))
		{
			if (node_id_new != 1) //not Gateway node
			{
				animal_packet_forward(from, false);
			}
			else
			{
				//uint16_t rssi, packet_len;
				//packet_len = packetbuf_datalen();
				//printf("rssi at broadcast receive: %x\r\n",rssi);
				sprintf(s_addr, "%x-", ((from->u8[0] << 8) | from->u8[1]));
				strcat(rx_packet_uni.path, s_addr);
				strcpy(path,rx_packet_uni.path);
				char *delim = "-";
				char *animal_mote_addr = strtok(path, delim);
				printf("animal mote addr: %s\n",animal_mote_addr);
				//process_packet_gateway(rx_packet_uni);

				if (!strcmp(animal_mote_addr, "ee65"))
				{
					path_to_gateway_animal1(rx_packet_uni);
				}
				 if (!strcmp(animal_mote_addr, "ee66"))
				 {
					 path_to_gateway_animal2(rx_packet_uni);
				 }
			}
		}
		leds_off(LEDS_GREEN);
	}
#endif //MOBILE_MOTE
}

uint16_t node_id_map(uint16_t node_id)
{
	uint16_t node_id_new;
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

//------------------------ PROCESS' THREAD ------------------------
/*
 * This process is for broadcasting packets. Tracking motes broadcast 10 packets and stop.
 * Animal motes keep on broadcasting packets containing sensor information. RED LEDs blink during
 * sending broadcast packets.
 */
PROCESS_THREAD(process_broadcast, ev, data)
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

		//Set Transmit power
		NETSTACK_CONF_RADIO.set_value(RADIO_PARAM_TXPOWER, 7);

		print_settings();

		// Open broadcast connection.
		broadcast_open(&broadcast, 129, &broadcast_call);
		// Open unicast connection.
		unicast_open(&unicast, 146, &unicast_call);

		node_id = ((linkaddr_node_addr.u8[0] << 8) | linkaddr_node_addr.u8[1]);
		printf("node id = %x\n", node_id);
		node_id_new = node_id_map(node_id);
		printf("new node id = %x\n", node_id_new);

		if (node_id_new == 1)
		{
			broadcast_ended = 1;
		}

		etimer_set(&broadcast_timer,
		CLOCK_SECOND * timer_interval * random_rand() / RANDOM_RAND_MAX);
		/* Configure the ADC ports */
		adc_zoul.configure(SENSORS_HW_INIT,ZOUL_SENSORS_ADC1 | ZOUL_SENSORS_ADC3);
		if (node_id_new != 1) //not gateway mote
		{
		while (1)
		{
			PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&broadcast_timer));

				//------------------------------ Animal Mote -----------------------------------------
				if (node_id_new == 7 || node_id_new == 8) //mobile mote
				{
					leds_on(LEDS_RED);
					//--------------------------- Sensor Values ----------------------------------------
					//Get Battery value
					int battery_val = vdd3_sensor.value(CC2538_SENSORS_VALUE_TYPE_CONVERTED);
					//Get temperature value
					int temp_val = cc2538_temp_sensor.value(CC2538_SENSORS_VALUE_TYPE_CONVERTED);
					//Calculate heartbeat value
					uint16_t adc3_value = adc_zoul.value(ZOUL_SENSORS_ADC3) >> 4;
					uint16_t BPM = (adc3_value >= 920) ? adc3_value/13.857 : 0;
					printf("adc value: %d\n",adc3_value);
					sprintf(_from_animal_mote.text, "Bat:%d Temp:%d Hb:%d",
							battery_val, temp_val, BPM);
					printf("\n%s",_from_animal_mote.text);
					//strcpy(_from_animal_mote.text, "Message from Animal Mote!");
					strcpy(_from_animal_mote.type, "MM");
					_from_animal_mote.hops = INFINITE_HOP_COUNT;
					_from_animal_mote.path[0] = '\0';

					packetbuf_clear();
					packetbuf_copyfrom(&_from_animal_mote, 80);
					broadcast_send(&broadcast);
					printf("\nBroadcast message %d sent.\n", broadcast_counter);
					broadcast_counter++;
					etimer_set(&broadcast_timer,
					CLOCK_SECOND * 5 * random_rand() / RANDOM_RAND_MAX);
					leds_off(LEDS_RED);
				}
				//------------------------------ Tracking Mote --------------------------------------
				else
				{
					if (broadcast_counter < 10)
					{
						leds_on(LEDS_RED);
						strcpy(_from_tracking_mote.text,
								"Message from Tracking Mote!");
						strcpy(_from_tracking_mote.type, "TM");
						_from_tracking_mote.hops = INFINITE_HOP_COUNT;

						packetbuf_clear();
						packetbuf_copyfrom(&_from_tracking_mote, 80);
						broadcast_send(&broadcast);
						printf("\nBroadcast message %d sent.\n",
								broadcast_counter);
						broadcast_counter++;
						etimer_set(&broadcast_timer,
						CLOCK_SECOND * 5 * random_rand() / RANDOM_RAND_MAX);
						leds_off(LEDS_RED);
					}
					else
					{
						/* If broadcast period finished*/
						printf(
								"--------------Stopping the broadcast-------------------- \n\r");
						etimer_stop(&broadcast_timer);
						//			print_routing_table();
						broadcast_counter = 0;
						broadcast_ended = 1;
						//process_post(&process_cost_share,PROCESS_EVENT_MSG, 0);
						//PROCESS_EXIT(); // Exit the process.
//						if(ListEmpty)
//						{
//							process_post(&flooding_process, NULL, 0);
//						}
					}
				}

			}
		}

	PROCESS_END();
}

/*
 * This process shares the cost of its nearest neighbor with all other neighbors at regular interval
 */
PROCESS_THREAD(process_cost_share, ev, data)
{
PROCESS_EXITHANDLER(printf("----- Cost Sharing Process terminated!-----\r\n");)
PROCESS_BEGIN();

	static struct etimer cost_share_timer;

	etimer_set(&cost_share_timer,
	CLOCK_SECOND * COST_SHARE_INTERVAL + (random_rand() % 5) / 5);
	printf("\n%f",CLOCK_SECOND * COST_SHARE_INTERVAL + (random_rand() % 5) / 5);
	while (1)
	{
//		printf("\n744");
		PROCESS_WAIT_EVENT();
//		printf("\n746");
//		if (ev == PROCESS_EVENT_MSG)
//		{
//			printf("\n749");
			if (etimer_expired(&cost_share_timer))
			{
				printf("Found Gateway: %d\n", gateway_reached);
				if (gateway_reached && broadcast_ended)
				{
					printf("\nSharing cost of nearest neighbor..");
					nn_cost_share();
					printf("\n757");
				}
				etimer_set(&cost_share_timer,
				CLOCK_SECOND * COST_SHARE_INTERVAL + (random_rand() % 5) / 5);
				printf("\n761");
			}



			//process_post(&process_cost_share, PROCESS_EVENT_MSG, 0);
//		}
	}
PROCESS_END();
}
