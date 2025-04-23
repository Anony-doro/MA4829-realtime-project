#include <stdio.h>
#include <stdlib.h>

int main(){
	int i=1,sum=0;

	do{
	sum +=i;											//i=1->sum=1, i=2->sum=3, i=3->sum=6, i=4->sum=10
	i++;
	}while(i<5);

	printf("Summation is %d\n",sum);
	exit(0);
}