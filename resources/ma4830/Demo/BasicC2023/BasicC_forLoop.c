#include <stdio.h>
#include <stdlib.h>
int sum,i;

int main(){
	
	for(sum=0,i=1;i<5;i++){		
	
	sum +=i;											//i=1->sum=1, i=2->sum=3, i=3->sum=6, i=4->sum=10
	
	}

	printf("Summation is %d\n",sum);
	exit(0);
}