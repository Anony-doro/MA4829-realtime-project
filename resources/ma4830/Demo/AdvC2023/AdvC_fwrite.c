#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main() {

float data[10];
FILE *fp;

struct person{
	char name[10];
	int age;
};

struct person x;

if ((fp=fopen("numbers","w")) == NULL) {
  perror("cannot opebn");
  exit(1);
  }
  
strcpy(x.name,"Name is");  
if(fwrite(&data[3],sizeof(float),4,fp)!=4) perror("cannot write");

strcpy(x.name,"Name is");  
x.age=25;
if(fwrite(&x,sizeof(struct person),1,fp)!=1) perror("cannot write");

printf("Age is %d Sizeof %d\n", x.age,sizeof(x));

}