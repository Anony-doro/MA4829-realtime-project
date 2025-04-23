#include <stdio.h>
#include <stdlib.h>

float rate(float hours){
	float f;

	if(hours<10.0) f=1.5*hours;								//rate 1
	else if(hours<15.0)	f=2.0*hours;						//rate 2
	     else f=2.5*hours;										//rate 3
	return f;
}

/*
double rate(double hours){
	double f;

	if(hours<10.0) f=1.5*hours;								//rate 1
	else if(hours<15.0)	f=2.0*hours;						//rate 2
	     else f=2.5*hours;										//rate 3
	return f;
}
*/

int main(){
	float hours,cost;
	//double hours,cost;
	
	hours = 10.1;
	cost = rate(hours);
	printf("Your bill for %f hours usage is $%lf\n",hours,cost);			//why is there 0.000001? it is because binary cannot accurately 
	exit(0);																							//reps decimal according to IEEE 754 standard
}																										// Mantissa (fraction) 23 bits for float, 52 bits for double 