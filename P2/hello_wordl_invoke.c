#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>

#define __NR_gettid 186
#define __NR_hello 332

long mygettid(void)
{
    return (long)syscall(__NR_gettid);
}

void say_hello(void)
{
   syscall(__NR_hello);
}


int main(void)
{
    printf("El código de retorno de la llamada gettid es %ld\n",
    mygettid());
	
	say_hello();

    return 0;
}
