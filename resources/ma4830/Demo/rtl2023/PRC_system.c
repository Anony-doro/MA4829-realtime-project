#include <stdlib.h>
#include <stdio.h>
#include <process.h>

int main() {

printf("demo of system\n");
system("ls -t -r");
getchar();

printf("Demo of execl\n");
//execl("/bin/ls","'bin/ls", "-t","-r",NULL); // inherits parent pid

printf("Demo of spawnl\n");
spawnl(P_WAIT,"/bin/ls","'bin/ls", "-t","-r",NULL);

printf("Continue with parent process\n");
return(0);
}