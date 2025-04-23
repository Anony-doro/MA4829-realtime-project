//**************************************************************
//	Program : dm_clock.c 
//	Demonstrates clock functions : Posix 1003.4
// 	clock_getres,clock_gettime
//

// 	28 Jan. 2007 : G.Seet

// 	QNX 6.xx version

//****************************************************************

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <time.h>


#define BILLION		1000000000L


#define MILLION		1000000L


#define THOUSAND	1000L


int main()
  {
  struct timespec res,start,stop;
  double accum;

  if(clock_gettime(CLOCK_REALTIME,&start)==-1)
	{
	perror("clock gettime");
	exit(EXIT_FAILURE);
    }
  					// process start
  if(clock_getres(CLOCK_REALTIME,&res)==-1)
	{
	perror("clock get resolution");
	exit(EXIT_FAILURE);
    }

	printf("Resolution is %ld micro seconds.\n",res.tv_nsec/THOUSAND);
	  
	delay(100);  
      	printf("Resolution is %ld milli seconds.\n",res.tv_nsec/MILLION);
	
					
					// process end
  if(clock_gettime(CLOCK_REALTIME,&stop)==-1)
	{
	perror("clock gettime");
	exit(EXIT_FAILURE);
    }

  accum=(double)(stop.tv_sec-start.tv_sec)+(double)(stop.tv_nsec- start.tv_nsec)/BILLION;
  
  printf("Timings for clock_start  was : %10ld\n",start.tv_nsec);
  printf("Timings for clock_stop   was : %10ld\n",stop.tv_nsec);
  printf("Timings for clock_getres was : %9.8lf\n",accum);
  
  exit(EXIT_SUCCESS);
  }