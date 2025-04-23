#include <stdio.h>
#include <stdlib.h>

int main(){
	int i,b,a,x;
	
	i=10; printf("i = %d\n",i++); 															//add 1 after
	i=10; printf("i = %d\n",++i); 															//add 1 first before printing
	i=(b=1+2)+4; printf("i = %d\n",i); 												// 1+2 = 3, 3+4 = 7
	i=(3<=8)+4; printf("i = %d\n",i); 													// Boolean logic 3< than 8 = 1, 1+4 = 5
	i=(1<<1); printf("i = %d\n",i); 														//1 shift left by 1, bitwise shift left from 01 to 10 = 2
	x=2; a=(x<2)? 5 : 12; printf("a = %d\n",a); // assume x = 2 		//if x < 2, yes = 5, no =12
	
}