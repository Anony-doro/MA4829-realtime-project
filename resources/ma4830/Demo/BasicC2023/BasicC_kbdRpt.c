#include <stdio.h>
#include <stdlib.h>

int main(){
	int i,c;
	int n_spaces=0,n_symbols=0,n_chars=0;

	for(i=0;i<50;i++){
		c=getchar();
		if(c==EOF) break;																		//press ctrl-d twice or enter then ctrl-d from keyboard as EOF
		if(c=='\n') continue;

		switch(c){
			case ' ': n_spaces++;
				         break;
			case ',':
			case '.':
			case ';': n_symbols++;
			default : n_chars++;																//symbols count as chars
		}
			
	}
	printf("Chars: %d spaces: %d symbols %d\n",n_chars,n_spaces,n_symbols);
	exit(0);
}
#/** PhEDIT attribute block
#-11:16777215
#0:555:TextFont9:-3:-3:0
#**  PhEDIT attribute block ends (-0000114)**/
