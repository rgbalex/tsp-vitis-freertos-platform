#include <stdio.h>
#include <string.h>

#include "xparameters.h"
#include "netif/xadapter.h"
#include "xuartps_hw.h"
#include "xil_printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "lwip/sockets.h"
#include "lwipopts.h"
#include <lwip/ip_addr.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>

#include "xsolve_the_tsp.h"

#define THREAD_STACKSIZE 1024
//#define PORT 51000 // Cat facts mode
#define PORT 51100 // Exam mode

unsigned char mac_ethernet_address[] = { 0x00, 0x11, 0x22, 0x33, 0x00, 0x05 };
//Function Prototypes
void network_init(unsigned char* mac_address, lwip_thread_fn app);
void application_task(void *);
void udp_get_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
extern void Xil_DCacheFlush();

//Global Variables
ip_addr_t ip;

int _distance = 0xFFFF;
int _scenario_id = 0;
int _num_cities = 0;

int *distance;
int *scenario_id;
int *num_cities;

int *request_data_counter;
int _request_data_counter = 0;

int *send_solution_flag;
int _send_solution_flag = 0;

int *run_hardware_counter;
int _run_hardware_counter = 0;

int *get_input_counter;
int _get_input_counter = 1;

u32 ram[100];
char adjacency_matrix[400];

int *matrix_size;
int _matrix_size = 0;

double _temperature = 10.0;
double _cooling_rate = 0.99;
double _absolute_temperature = 1;

XSolve_the_tsp hls;
XSolve_the_tsp_Config *hls_config;

//------

void init_tsp(){

		int status;
		// Create HLS Sum IP pointer
		hls_config = XSolve_the_tsp_LookupConfig(XPAR_XSOLVE_THE_TSP_0_DEVICE_ID);

		if (!hls_config) {
			xil_printf("Error loading configuration for XSolve_the_tsp_Config \n\r");
		}

		status = XSolve_the_tsp_CfgInitialize(&hls, hls_config);
		if (status != XST_SUCCESS) {
			xil_printf("Error initialising for XAnnealing \n\r");
		}

		XSolve_the_tsp_Initialize(&hls, XPAR_XSOLVE_THE_TSP_0_DEVICE_ID);

		XSolve_the_tsp_Set_ram(&hls, (u32) ram);
	    Xil_DCacheFlush();
		xil_printf("Initialised hardware successfully. \n\r");

}

void start_tsp(void *p) {

	vTaskDelete(NULL); //Set up complete so delete ourselves
}

void send_solution_data(void *p) {
	// Send an initial message
	// Create a protocol control block (PCB)
	struct udp_pcb *send_pcb = udp_new();
	struct pbuf *pbuf;

	for (;;) {
		while (*send_solution_flag) {
			printf("Sending UDP Solution Data with distance %d...\r\n", *distance);
		    // Save the IP address to send UDP packets to

		    char* request_msg = calloc(10, sizeof(char));

			request_msg[0] = 0x03;
			request_msg[1] = (char) *num_cities;
			request_msg[2] = (char) ((*scenario_id >> 24) & 0xFF);
			request_msg[3] = (char) ((*scenario_id >> 16) & 0xFF);
			request_msg[4] = (char) ((*scenario_id >> 8) & 0xFF);
			request_msg[5] = (char) (*scenario_id & 0xFF);

			char temp;
			memcpy(&request_msg[0]+6, distance, 4);
			// endian_fix
			temp = request_msg[9];
			request_msg[9] = request_msg[6];
			request_msg[6] = temp;
			temp = request_msg[8];
			request_msg[8] = request_msg[7];
			request_msg[7] = temp;

			pbuf = pbuf_alloc(PBUF_TRANSPORT, 10, PBUF_RAM);
			memcpy(pbuf->payload, request_msg, 10);
			udp_sendto(send_pcb, pbuf, &ip, PORT);

			pbuf_free(pbuf);
			
			*send_solution_flag -= 1;
		}
	}
	vTaskDelete(NULL); //Set up complete so delete ourselves
}

void run_hardware_task(void *p) {
	for (;;) {
		while (*run_hardware_counter) {
			printf("Number of cities: %d\n", *num_cities);
			printf("Scenario ID: %d\n\n", *scenario_id);

			for (int x = 0; x < *num_cities; x++) {
				for (int y = 0; y < *num_cities; y++) {
					printf("%3d ", adjacency_matrix[(*num_cities * y) + x]);
				}
				printf("\n");
			}
			printf("\n");

			memcpy(ram, adjacency_matrix, sizeof(adjacency_matrix));

			printf(" ===  Hardware run started  === \r\n\n");

			u32 shortest_distance = 9999;
			u32 hw_num_cities = *num_cities;

			XSolve_the_tsp_Set_p_number_of_cities(&hls, hw_num_cities);

			printf("Starting the hardware...\n");

		    Xil_DCacheFlush();
			XSolve_the_tsp_Initialize(&hls, XPAR_XSOLVE_THE_TSP_0_DEVICE_ID);
			//Start the hardware IP core
			XSolve_the_tsp_Start(&hls);
			//Wait until it is done
			while (!XSolve_the_tsp_IsDone(&hls));

		    Xil_DCacheFlush();

			// store the solution
			shortest_distance = XSolve_the_tsp_Get_p_shortest_calculated_distance(&hls);

			printf("               Distance in parameter value: %d\n", (int)shortest_distance);

			// print out the optimal route from the first 20 ints of mainmemory
			printf("               Optimal route: ");
			for (int i = 0; i < hw_num_cities+1; i++) {
				printf("%d ", (unsigned int) ram[i]);
			}
			printf("\n");

			*distance = shortest_distance;

			printf("Sending the solution with distance %d...\n\n", *distance);
		    Xil_DCacheFlush();

			// send information back task here
			printf(" === Hardware run completed === \r\n\n");
			*run_hardware_counter -= 1;
			*send_solution_flag += 1;
		}
	}
	vTaskDelete(NULL);
}

void get_scenario_and_city_size(void *p) {
	for (;;) {
		if (*get_input_counter) {
			xil_printf("Enter value for Scenario ID (0-255): ");
			scanf("%d", scenario_id);
			xil_printf("%d\n\r", *scenario_id);

			xil_printf("Enter value for Number of Cities: ");
			scanf("%d", num_cities);
			xil_printf("%d\n\r", *num_cities);

			xil_printf("Performing TSP Solve...\r\n\n");

			*request_data_counter += 1;
			*get_input_counter = 0;
		}
	}
	vTaskDelete(NULL);
}


int main() {
	IP4_ADDR(&ip, 192, 168, 10, 1);
	_distance = 0;
	_scenario_id = 0;
	_num_cities = 0;

	distance = &_distance;
	scenario_id = &_scenario_id;
	num_cities = & _num_cities;

	request_data_counter = &_request_data_counter;
	send_solution_flag = &_send_solution_flag;

	run_hardware_counter = &_run_hardware_counter;

	get_input_counter = &_get_input_counter;

	init_tsp();

	//Initialise the network with our MAC address, and the function that should be started as a FreeRTOS task
	network_init(mac_ethernet_address, application_task);

	vTaskStartScheduler(); //Start the scheduler

	//we will only get to here if someone calls vTaskEndScheduler()
	return 0;
}


void send_request_data(void *p) {
	// Send an initial message
	// Create a protocol control block (PCB)
	struct udp_pcb *send_pcb = udp_new();
	struct pbuf *pbuf;

	for (;;) {
		while (*request_data_counter) {
			printf("Sending UDP Request Data...\r\n");
		    // Save the IP address to send UDP packets to

		    char* request_msg = calloc(6, sizeof(char));

			request_msg[0] = 0x01;
			request_msg[1] = (char) *num_cities;
			request_msg[2] = (char) ((*scenario_id >> 24) & 0xFF);
			request_msg[3] = (char) ((*scenario_id >> 16) & 0xFF);
			request_msg[4] = (char) ((*scenario_id >> 8) & 0xFF);
			request_msg[5] = (char) (*scenario_id & 0xFF);


			pbuf = pbuf_alloc(PBUF_TRANSPORT, 6, PBUF_RAM);
			memcpy(pbuf->payload, request_msg, 6);
			udp_sendto(send_pcb, pbuf, &ip, PORT);

			pbuf_free(pbuf);

			*request_data_counter = 0;
		}
	}
	vTaskDelete(NULL); //Set up complete so delete ourselves
}

void application_task(void *p) {
    //This task will set things up and then remove itself once that is done
	xil_printf("application_task started\n\r");

	//Bind a network receiver as we did before
	struct udp_pcb *recv_pcb = udp_new();
	udp_bind(recv_pcb, IP_ADDR_ANY, PORT);
	udp_recv(recv_pcb, udp_get_handler, NULL);

    //Create any other tasks you might need here
	xTaskCreate(send_request_data, "send_request_data", THREAD_STACKSIZE, NULL, DEFAULT_THREAD_PRIO, NULL);
	xTaskCreate(send_solution_data, "send_solution_data", THREAD_STACKSIZE, NULL, DEFAULT_THREAD_PRIO, NULL);
	xTaskCreate(get_scenario_and_city_size, "get_scenario_and_city_size", THREAD_STACKSIZE, NULL, DEFAULT_THREAD_PRIO, NULL);
	xTaskCreate(run_hardware_task, "run_hardware_task", THREAD_STACKSIZE, NULL, DEFAULT_THREAD_PRIO, NULL);
	vTaskDelete(NULL); //Set up complete so delete ourselves
}

void udp_get_handler(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
	if (p) {

		char msg[p->len];
		char type_of_message = ((unsigned char*) p->payload)[0];

		if (type_of_message == 0x02) {
			memcpy(msg, p->payload, 6);
			printf("Request Response Message Type: %d\n", msg[0]);
			memcpy(adjacency_matrix, p->payload+6, p->len-6);

			*matrix_size = (int) msg[1];
			*run_hardware_counter += 1;

			printf("Passing to hardware task...\r\n\n");

		} else if (type_of_message == 0x04) {
			memcpy(msg, p->payload, 7);
			printf("Request Response Message Type: %d\n", msg[0]);

			printf("\nYour solution is of type: %d\n", msg[6]);
			printf("(1 = Correct, 2 = Incorrect, 3 = Unknown)\r\n\n");

			// if correct, print a smiley face ascii art
			// else print something else
			if (msg[6] == 1) {
				printf("_____________________________\n\n");
				printf("| Your solution is correct! |\n");
				printf("_____________________________\n\n");

				printf("                        .                      .             +        .     \n");
				printf("     .    __ _o|                        .                                   \n");
				printf("         |  /__|===--        .                                       <=>    \n");
				printf("         [__|______~~--._                      .                .      .    \n");
				printf("   .    /   `---.__:====]-----...,,_____                *      .     -      \n");
				printf("        |[>-----|_______<----------_____;::===--                            \n");
				printf("        |/_____.....-----'''~~~~~~~                        .              . \n");
				printf("                                            .                               \n");
				printf("   +               .        Congratulations on a correct solution :)        \n");
				printf("        .                           .     Have a star destroyer             \n\n\n");


			} else if (msg[6] == 2) {
				printf("_____________________________\n\n");
				printf("| Your solution is wrong! :( |\n");
				printf("_____________________________\n\n");
			} else if (msg[6] == 3) {
				printf("_____________________________\n\n");
				printf("| Your solution is unknown! |\n");
				printf("_____________________________\n\n");
			} else {
				printf("_____________________________\n\n");
				printf("| Something is foobar-ed :( |\n");
				printf("_____________________________\n\n");
			}
			*get_input_counter += 1;

		} else {
			printf("_____________________________\n\n");
			printf("| Something is foobar-ed :( |\n");
			printf("_____________________________\n\n");
		}
	}

	pbuf_free(p);
}
