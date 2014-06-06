/**
 * prodcons.c
 * Project 2 - CS1550
 * rom66
 * 
 * compiled with:
 * 		gcc -m32 -o prodcons -I /u/OSLab/rom66/linux-2.6.23.1/include/ prodcons.c
 * on thot.cs.pitt.edu using custom kernel
 **/

#include <sys/types.h>
#include <linux/unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>

struct cs1550_sem 
{
	int value;
	struct Node *start;
	struct Node *end;
};

void up(struct cs1550_sem *sem) {
       syscall(__NR_cs1550_up, sem);
}

void down(struct cs1550_sem *sem) {
       syscall(__NR_cs1550_down, sem);
}

int main(int argc, char *argv[]) 
{        
        if(argc != 4) 
        {
			printf("Usage: %s [numProducers] [numConsumers] [bufferSize]\n",argv[0]);
			exit(-1);
        }
        
        //get command line arguments
        int numProducers = atoi(argv[1]);
        int numConsumers = atoi(argv[2]);
        int bufferSize = atoi(argv[3]);
        
        //allocate memory before any fork() so that all of the threads can share
        void * sem_ptr = mmap(NULL, sizeof(struct cs1550_sem)*3, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
        
        struct cs1550_sem * empty  = (struct cs1550_sem*)sem_ptr;
        struct cs1550_sem * full = (struct cs1550_sem*)sem_ptr + 1; //offset by sizeof one semaphore
        struct cs1550_sem * mutex = (struct cs1550_sem*)sem_ptr + 2; //"            " two sempahores

        // Allocate the memory for the shared buffer. Also includes the size of the buffer at the beginning.
        void * intMem = mmap(NULL, sizeof(int)*(bufferSize + 3), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
                
        int * buffSizePointer = (int*)intMem; //save bufferSize in public mem
        int * producerPointer = (int*)intMem + 1; //next position to produce into
        int * consumerPointer = (int*)intMem + 2; //next position to consume from
        int * bufferPointer = (int*)intMem + 3; //pointer to the buffer
        
        *buffSizePointer = bufferSize;
        *producerPointer = 0; //set to beginning of buffer
        *consumerPointer = 0; //set to beginning of buffer
        
        //initialize semaphores
        full->value = 0;
        full->start = NULL;
        full->end = NULL;
        mutex->value = 1;
        mutex->start = NULL;
        mutex->end = NULL;
        empty->value = bufferSize;
        empty->start = NULL;
        empty->end = NULL;
        
        int i;        
        for(i = 0; i < bufferSize; i++) //make sure buffer is 0'ed out
			bufferPointer[i] = 0;
        
        for(i = 0; i < numProducers; i++) //create producer threads
        {
			if(fork() == 0) //if child thread
			{
				int item;
				while(1) 
				{
					down(empty);
					down(mutex); // lock the mutex
					
					item = *producerPointer;
					bufferPointer[*producerPointer] = item;
					printf("Producer %c Produced: %d\n", i+65, item); //add 65 for ascii offset
					*producerPointer = (*producerPointer+1) % *buffSizePointer;
					
					up(mutex); // Unlock the mutex
					up(full);
				}
			}
        }
        
        for(i = 0; i < numConsumers; i++) //create consumer threads
        {
			if(fork() == 0) //child process
			{
				int item;
				
				while(1) 
				{
					down(full);
					down(mutex); // lock the mutex
					
					item = bufferPointer[*consumerPointer];
					printf("Consumer %c Consumed: %d\n", i+65, item); //add 65 for ascii offset
					*consumerPointer = (*consumerPointer+1) % *buffSizePointer;
					
					up(mutex); // Unlock the mutex
					up(empty);
				}
			}
        }
        
        int status;
        wait(&status); // Wait until all processes complete
        return 0; // Finished successfully
}
