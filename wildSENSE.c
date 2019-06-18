
/*----------------------------INCLUDES----------------------------------------*/
// standard C includes:
#include <stdio.h>
#include <stdint.h>

// Contiki-specific includes:
#include "contiki.h"
#include "net/rime/rime.h"     // Establish connections.
#include "net/netstack.h"      // Wireless-stack definitions
#include "dev/button-sensor.h" // User Button
#include "dev/leds.h"          // Use LEDs.
#include "core/net/linkaddr.h"

static uint8_t node_id;		        // Stores node id

#include "helpers.h"

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

// Defines the behavior of a connection upon receiving data.
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from) {
	leds_on(LEDS_GREEN);
	 printf("Broadcast message received from 0x%x%x: '%s' [RSSI %d]\n",
			 from->u8[0], from->u8[1],
			(char *)packetbuf_dataptr(),
			(int16_t)packetbuf_attr(PACKETBUF_ATTR_RSSI));
	leds_off(LEDS_GREEN);
	printf("Add neighbour to list");
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

	node_id = (linkaddr_node_addr.u8[1] & 0xFF);
	printf("%x",node_id);

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
						printf("Broadcast message sent.\n");
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


