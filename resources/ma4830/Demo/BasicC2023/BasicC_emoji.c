#include <stdio.h>
#include <stdlib.h>

int main(){

char data[]={0x18,0x18,0x18,0xff,0xff,0x18,0x18,0x18};					// 0x -> binary representation
char msk;																								// 0x18 -> 00011000
int i,j;																										// 0x ff -> 11111111	

for(i=0;i<8;i++){
	for(j=0;j<8;j++){																				//eg. i=0 -> data[0] = 00011000
	msk=1<<(7-j);																					//j=0 -> msk = 00000001 then shift left 7 times 10000000
	if((data[i]&msk)==0) putchar('0');													//the msk is moving as the j value increasing
	else putchar('1');																				//if the & result is 0, then print 0 else print 1
}
putchar('\n');
}

}