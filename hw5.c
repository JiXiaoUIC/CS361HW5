#include "hw5.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h> // memset
#include <stdlib.h> // malloc
#include <time.h> //rand

//First thing I want to do is create structs to hold information
//for both the passengers and the elevator

//HINT: The template solution can a single set of global variables.
//You’ll probably want to have one set per elevator. 
//Define an elevator struct that holds all your necessary state per elevator, and make an array of such structs.

//contains information for passenger states
struct Passenger{
	int elevator; //what elevator they are on
	int from_floor; //where its coming from
	int to_floor; //where its going
	int finished; //check if trip is finished
	pthread_mutex_t notify_lock;
	pthread_cond_t notify;
} passengers[PASSENGERS];

//contains information for elevator states
struct Elevator{
	int floor; //where its at
	int direction; //which way its headed
	int next_floor; //what is the next floor
	int occupancy; //moved inside the elevator struct
	pthread_mutex_t lock;
	pthread_barrier_t barrier; //barrier for waiting
	enum {ELEVATOR_ARRIVED, ELEVATOR_OPEN, ELEVATOR_CLOSED} state; //moved inside to elevator struct
}elevators[ELEVATORS];	

/*
	To considerably speed up the runtime of the program, I implemented a linked list
	to keep track of elevator and passenger queues and groups
*/
struct Queue* queue(struct Queue*, const int);
struct Queue *elevator_queue;

/*
	Linked list to keep track of passengers
*/
struct Group{
	int passenger;
	struct Group* next;
};

/*
	Linked list to manage the queue of passengers
*/
struct Queue{
	int size;
	struct Group* head;
	struct Group* tail;
};

/*
	This function will initialize and return a struct queue
*/
struct Queue* queue(struct Queue* queue, const int passenger){
	
	struct Group* pos = (struct Group*)malloc(sizeof(struct Group)); //malloc memory
	pos -> passenger = passenger;
	
	if(queue -> head == 0 && queue -> tail == 0){
		queue -> head = pos;
		queue -> tail = pos;
	}
	else if(queue -> head == queue -> tail){
		queue -> tail = pos;
		queue -> head -> next = queue -> tail;
	}
	else{
		queue -> tail -> next = pos;
		queue -> tail = pos;
	}
	return queue;
}

//global lock for every passenger
pthread_mutex_t passenger_lock = PTHREAD_MUTEX_INITIALIZER;

int passenger_counter = 0; //passenger counter


/*
	Initialize the passenger and elevator arrays
	Initialize the memory for elevator queue
*/
void scheduler_init() {	
	
	memset(passengers, 0, sizeof(passengers));

	//intialize passengers
	for(int i=0; i<PASSENGERS; i++){
		pthread_cond_init(&passengers[i].notify, 0);
		pthread_mutex_init(&passengers[i].notify_lock, 0);
	}

	//initialize elevators
	for(int i=0; i<ELEVATORS; i++){
		elevators[i].floor = 0;
		elevators[i].direction = 1; //initially going up
		elevators[i].next_floor = -1;
		elevators[i].occupancy = 0;
		elevators[i].state = ELEVATOR_ARRIVED;
		pthread_mutex_init(&elevators[i].lock, 0);
		pthread_barrier_init(&elevators[i].barrier, 0, 2); //holding 2 threads
	}
	
	//create space for elevator queue
	elevator_queue = (struct Queue*) malloc(sizeof(struct Queue));
	elevator_queue -> head = 0;
	elevator_queue -> tail = 0;
}

//called when passengers hits a button
void passenger_request(int passenger, int from_floor, int to_floor, void (*enter)(int, int), void(*exit)(int, int)){

	//lock passenger_lock while assigning information
	pthread_mutex_lock(&passenger_lock);

		//increment counter by 1 if new passenger
		if(passenger >= passenger_counter)
			passenger_counter = passenger + 1;

		//HINT: Handling multiple elevators should be the last item on your TODO list.
		//An easy way to do it is to randomly decide, for each passenger, which elevator they should use,
		//independent of everything else. Then you can treat each elevator+passengers group separately.
		passengers[passenger].elevator = random() % ELEVATORS;
		passengers[passenger].from_floor = from_floor;
		passengers[passenger].to_floor = to_floor;
		passengers[passenger].finished = 0;

		queue(elevator_queue, passenger);

	pthread_mutex_unlock(&passenger_lock);

	//wait for elevator
	pthread_mutex_lock(&passengers[passenger].notify_lock);
	pthread_cond_wait(&passengers[passenger].notify, &passengers[passenger].notify_lock);
	pthread_mutex_unlock(&passengers[passenger].notify_lock);

	//passenger is assigned to an elevator
	int elevator = passengers[passenger].elevator;

	//passenger waits for their floor
	pthread_barrier_wait(&elevators[elevator].barrier);

	//lock elevator while assigning information
	pthread_mutex_lock(&elevators[elevator].lock);

		enter(passenger, elevator);
		elevators[elevator].occupancy++;
		elevators[elevator].next_floor = to_floor;

	pthread_mutex_unlock(&elevators[elevator].lock);

	pthread_barrier_wait(&elevators[elevator].barrier);
    	pthread_barrier_wait(&elevators[elevator].barrier);

	//lock elevator while assigning information
	pthread_mutex_lock(&elevators[elevator].lock);

		if(elevators[elevator].floor == to_floor && elevators[elevator].state == ELEVATOR_OPEN) {
			exit(passenger, elevator);
			elevators[elevator].next_floor = -1; //reset to assign floor to another passenger
			elevators[elevator].occupancy--;
		}

	pthread_mutex_unlock(&elevators[elevator].lock);

	pthread_barrier_wait(&elevators[elevator].barrier);
}

void elevator_ready(int elevator, int at_floor, void(*move_direction)(int, int), void(*door_open)(int), void(*door_close)(int)) {

	//lock elevator while checking conditions
	pthread_mutex_lock(&elevators[elevator].lock);
	
		if(elevators[elevator].state == ELEVATOR_ARRIVED && elevators[elevator].next_floor == at_floor) {
			door_open(elevator);
			elevators[elevator].state=ELEVATOR_OPEN;

	//unlock early and make barrier wait
	pthread_mutex_unlock(&elevators[elevator].lock);

			//HINT: Start by making sure your passengers have time to get on and off the elevator.
			//You can do this with condition variables (pthread_cond_t), or barriers (pthread_barrier_t).
			//A barrier-based solution seems easier. Use one barrier to make the passenger wait for the door to open,
			//and another to make the elevator wait for the passenger to enter.

			pthread_barrier_wait(&elevators[elevator].barrier); //allows passenger time to board
			return;
		}
		else if(elevators[elevator].state == ELEVATOR_OPEN) {
			pthread_barrier_wait(&elevators[elevator].barrier); //wait for passenger
			door_close(elevator);
			elevators[elevator].state = ELEVATOR_CLOSED;
		}
		// if elevator is closed
		else {
			//look for available passenger
			if(elevators[elevator].next_floor<0) {

				pthread_mutex_lock(&passenger_lock);

					int next_passenger;

					if (elevator_queue -> head == 0) {
						next_passenger = -1; // find a new passenger
					}
					else {
						next_passenger = elevator_queue -> head -> passenger;
					}
	
					//queue is empty
					if(elevator_queue -> head == elevator_queue -> tail) {
						free(elevator_queue -> head);
						// reset head and tail to 0
						elevator_queue -> head = 0;
						elevator_queue -> tail = 0;

					}
					else if(elevator_queue -> head != 0) {
						free(elevator_queue -> head);
						// traverse to next item
						elevator_queue -> head = elevator_queue -> head -> next;
					}

					if(next_passenger != -1) {
						passengers[next_passenger].finished = 1;
						passengers[next_passenger].elevator = elevator;
						elevators[elevator].next_floor = passengers[next_passenger].from_floor;
						pthread_cond_signal(&passengers[next_passenger].notify);
					}

				pthread_mutex_unlock(&passenger_lock);
			}

			if(elevators[elevator].next_floor >= 0){
				if(at_floor < elevators[elevator].next_floor){
					move_direction(elevator, 1);
					elevators[elevator].floor++;
				}
				else if(at_floor > elevators[elevator].next_floor){
					move_direction(elevator, -1);
					elevators[elevator].floor--;
				}
			}
			elevators[elevator].state = ELEVATOR_ARRIVED;
		}
	pthread_mutex_unlock(&elevators[elevator].lock);
}