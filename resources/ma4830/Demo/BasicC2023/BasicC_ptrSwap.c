#include <stdio.h>
#include <stdlib.h>

void swap(int *x, int *y){
	int temp;
	
	temp=*x;
	*x=*y;
	*y=temp;
}

void swapneg(int x, int y){
	int temp;
	
	temp=x;
	x=y;
	y=temp;
}

struct point{
	int x;
	int y;
};

int main(){
	int a=2, b=5;

	printf("Before swap:a = %d, b = %d\n",a,b);
	swap(&a,&b);
	printf("After swap:a = %d, b = %d\n",a,b); 

	swapneg(a,b);
	printf("After swapneg:a = %d, b = %d\n",a,b); 

	exit(0);
	
}