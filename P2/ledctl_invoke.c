#include <sys/syscall.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <stdio.h>
#include <unistd.h>

#define __NR_LEDCTL 333

long lin_ledctl(unsigned int leds)
{
    return (long)syscall(__NR_LEDCTL, leds);
}

int main(int argc, char *argv[])
{

    if (argv[1] == NULL)
    {
        printf("Usage: ./ledctl_invoke <ledmask>");
        //printf("El comando introducido es incorrecto.");
    }
    else
    {
        unsigned int value = 0;
        sscanf(argv[1], "%x", &value);

        if (lin_ledctl(value) != 0)
        {
            perror("Ha ocurrido  un error al ejecutar el programa");
        }
    }

    return 0;
}
