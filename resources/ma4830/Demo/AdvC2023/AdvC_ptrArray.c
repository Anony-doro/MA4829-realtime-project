#include <stdio.h>
#include <stdlib.h>

void func1(int t[]){
int i;
for(i=0;i<2;i++){
	t[i+2]=0x11;
}
}

int main(){
int a[5]={7,4,9,11,8};
int *p;
int x,d;
int i,j;
int tt[]={1,2,3,4,5,6};
int ss[2][3]={{1,2,3},{4,5,6}};

printf("a[5]={");
for(d=0;d<5;d++){
	printf("%d",a[d]);
	if(d==4)printf("}\n"); else printf(",");				//a[5] the number starts from 0 to 4
}

p=&a[0];															//pointer p points to address a[0]
printf("p=&a[0],*p=%d\n",*p);							//Print out value of a[0]. *p is the value	
p=a;																	//same reps since a is an array unless it needs &a
printf("p=a,*p=%d\n",*p);

x=*(p+3);															//x is the value of (p+3) = 11
printf("x=*(p+3)=%d\n",x);
x=a[3];
printf("x=a[3]=%d\n",x);

p=&a[3];															
x=*(p-1);															//9		
printf("p=&a[3],x=*(p-1)=%d\n",x);
x=a[2];																//9
printf("x=a[2]=%d\n",x);

/*
int i,j;													//gives error
int tt[]={1,2,3,4,5,6};						//declare them on top
int ss[2][3]={{1,2,3},{4,5,6}};
*/

printf("\ntt[]={");
for(j=0;j<6;j++){
	printf("%2x",tt[j]);											//give space 
	if(j==5)printf("}\n"); else printf(",");
}

printf("ss[][]={");
for(i=0;i<2;i++){
	printf("{");
	for(j=0;j<3;j++){
		printf("%2x",ss[i][j]);
		if(j==2)printf("}");else printf(",");
	}
	if(i==2)printf("}");else printf(",");						//when i=0 and i=1, it prints coma
}
printf("}");

func1(&tt[0]);
printf("\nFirst printf->");
for(j=0;j<6;j++) printf("%2x ",tt[j]);

func1(&ss[0][0]);														// change ss[0][2] and s[1][0]
printf("\nSecond printf->");
for(i=0;i<2;i++)									
	for(j=0;j<3;j++) printf("%2x ",ss[i][j]);

printf("\nAlt printf->");												// it can print 2 x 3 array as 1 x 6 array
for(j=0;j<6;j++) printf("%2x ",ss[0][j]);

printf("\n");

return(0);
}