//**************************************************************************
//	Program : pt_create.c
//	Demonstrates simple thread process.
// 	Threads drop out of the procedure when done. Threads have access to 
// 	current process's code and data.
//
//	Revised for QNX6.3 : 4 November 2010
//
//**************************************************************************

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <process.h>

void*  thds( void* arg ) {	        // Function for threads
  printf("This is thread %d started\n", pthread_self() ); // gets own tid
  sleep(2);
  printf("Thread %d ending\n", pthread_self());
  return(0);
}

int main(void) {
int i;  pid_t pid;  char buf[80];

pid = getpid();    sprintf( buf, "pidin -p %ld", (long) pid );

  for( i = 0; i < 3; i++ )  {	// args: thread id, attributes, code, args
    pthread_create( NULL, NULL, &thds, NULL );
    sleep( 1 );
    }
  system( buf );  	// pidin -p "pid of current process"
  return (EXIT_SUCCESS);	// try printing message after system call
}


