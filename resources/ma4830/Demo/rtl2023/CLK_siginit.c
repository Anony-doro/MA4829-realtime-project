//**********************************************************************
// siginit - Demostrated the seting up of a trap fof SIGINIT
//				
// Functions: Control exit from perpetual loop
//
// October 2023, G.Seet
//**********************************************************************

#include <stdio.h>
#include <signal.h>

int signal_count, signal_number,loop;

void signal_handler( int signum ) {
    printf( "\nSignal raised\n" );
    loop=0;
    signal_number = signum;
   }

int  main(){
signal_count = 0; 
signal_number = 0;
loop=1;

signal( SIGINT, signal_handler );
printf("Signal raised on CTRL-C\n" ); 

printf( "Iteration:         " );
while(loop) {
   printf( "\b\b\b\b%4d", signal_count );
   flushall( );
   
   signal_count++;
   delay(500);
   }
   
   
   printf("Exit routine\n");
   delay(2000);
   printf("\f");
   printf("\nSignal raised %d\n",signal_number);
  
}

