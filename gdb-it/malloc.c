#include<stdio.h>
#include<malloc.h>

int main()
{
	void * p;
	// tcache & small size
	p = malloc(10);
	// tcache & large size
	p = malloc(20000);
	// not tcache & large size 
	p = malloc(1000000);
	// not tcache & huge size
	p = malloc(6000000);

	return 0;
}
