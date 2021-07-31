#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <math.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

void show_percent_in_leds(int result)
{
    int blinkstick_pointer = open("/dev/usb/blinkstick0", O_WRONLY);
    if (blinkstick_pointer == -1)
    {
        fprintf(stderr, "No se ha podido abrir el fichero de blinkstick");
        exit(1);
    }

    char mensaje[100];

    if (result == 0)
    {
        strcpy(mensaje, "1:0x0,2:0x0,3:0x0,4:0x0,5:0x0,6:0x0,7:0x0,8:0x0");
    }
    else if (result < 12.5)
    {
        strcpy(mensaje, "0:0x000011,1:0x000011");
    }
    else if (result > 12.5 && result <= 25)
    {
        strcpy(mensaje, "0:0x000011,1:0x000011,2:0x000011");
    }
    else if (result > 25 && result <= 37.5)
    {
        strcpy(mensaje, "0:0x000011,1:0x000011,2:0x000011,3:0x000011");
    }
    else if (result > 37.5 && result <= 50)
    {
        strcpy(mensaje, "0:0x111000,1:0x111000,2:0x111000,3:0x111000,4:0x111000");
    }
    else if (result > 50 && result <= 62.5)
    {
        strcpy(mensaje, "0:0x111000,1:0x111000,2:0x111000,3:0x111000,4:0x111000");
    }
    else if (result > 75 && result <= 86.5)
    {
        strcpy(mensaje, "0:0x111000,1:0x111000,2:0x111000,3:0x111000,4:0x111000,5:0x111000");
    }
    else
    {
        strcpy(mensaje, "0:0x111000,1:0x111000,2:0x111000,3:0x111000,4:0x111000,5:0x111000,6:0x111000,7:0x111000");
    }

    write(blinkstick_pointer, mensaje, sizeof(char) * strlen(mensaje));

    close(blinkstick_pointer);
}

/**
 * Obtener porcentaje de utilización de la CPU. 
 * https://www.idnt.net/en-GB/kb/941772
 **/
void show_cpu_usage(void)
{
    long double a[4], b[4], loadavg;
    FILE *fp;

    int i;
    // for (i = 1; i < 11; ++i)
    for(;;)
    {
        fp = fopen("/proc/stat", "r");
        fscanf(fp, "%*s %Lf %Lf %Lf %Lf", &a[0], &a[1], &a[2], &a[3]);
        fclose(fp);
        sleep(1);

        fp = fopen("/proc/stat", "r");
        fscanf(fp, "%*s %Lf %Lf %Lf %Lf", &b[0], &b[1], &b[2], &b[3]);
        fclose(fp);

        loadavg = ((b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2])) / ((b[0] + b[1] + b[2] + b[3]) - (a[0] + a[1] + a[2] + a[3]));

        int result = (int)ceil(loadavg * 100);
        printf("The current CPU utilization is: %Lf - result: %d\n", loadavg, result);

        show_percent_in_leds(result);

        sleep(1);
    }
}

void simularCPU(int mode)
{
    if (mode == 1)
    {
        show_percent_in_leds(13);
        sleep(1);
        show_percent_in_leds(25);
        sleep(1);
        show_percent_in_leds(37);
    }
    else if (mode == 2)
    {
        show_percent_in_leds(13);
        sleep(1);
        show_percent_in_leds(25);
        sleep(1);
        show_percent_in_leds(37);
        sleep(1);
        show_percent_in_leds(40);
    }
    else
    {
        show_percent_in_leds(13);
        sleep(1);
        show_percent_in_leds(25);
        sleep(1);
        show_percent_in_leds(37);
        sleep(1);
        show_percent_in_leds(60);
        sleep(1);
        show_percent_in_leds(76);
        sleep(1);
        show_percent_in_leds(90);
        sleep(1);
        show_percent_in_leds(0);
    }
}

int main(void)
{
    printf("Uso real de la CPU1.");
    show_cpu_usage();

    // printf("A continuación porcentajes fakes. \n");
    // simularCPU(1);
    // sleep(2);
    // simularCPU(2);
    // sleep(2);
    // simularCPU(3);
    return (0);
}