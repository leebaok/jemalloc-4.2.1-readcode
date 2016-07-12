#include<stdio.h>
#include<malloc.h>

int main()
{
	void *p1,*p2,*p3,*p4;
	// tcache & small size
	p1 = malloc(10);
	// tcache & large size
	p2 = malloc(20000);
	// not tcache & large size 
	p3 = malloc(1000000);
	// not tcache & huge size
	p4 = malloc(6000000);

	free(p1);
	free(p2);
	free(p3);
	free(p4);

	return 0;
}
