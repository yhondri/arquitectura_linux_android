#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

#define MAX_CBUFFER_LEN 64 //Tiene que ser potencia de 2.

//Estructura de proc.
static struct proc_dir_entry *proc_entry;
//Buffer circular.
struct kfifo cbuffer;
//Semáfaro productores (funcionan como cola).
struct semaphore semaphore_productores;
//Semáfaro consumidores (funcionan como cola).
struct semaphore semaphore_consumidores;
//Helper para la exclusión mutua.
struct semaphore sem_mutex;
// Número de procesos productores (que han abierto /proc con permiso de escritura).
int contador_productores = 0;
// Número de procesos consumidores (que han abierto /proc con permiso de lectura).
int contador_consumidores = 0;
int contador_productores_esperando = 0;
int contador_consumidores_esperando = 0;


int init_fifoproc_module(void);
void cleanup_fifoproc_module(void);
static int fifoproc_open(struct inode *inode, struct file *file);
static int fifoproc_release(struct inode *inode, struct file *file);
static ssize_t fifoproc_write(struct file *file, const char *buff, size_t len, loff_t *off);
static ssize_t fifoproc_read(struct file *file, char *buf, size_t len, loff_t *off);
static const struct file_operations proc_entry_fops;

int init_fifoproc_module(void) {
    int ret;
    ret = kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL);
    
    if (ret) {
        printk(KERN_ERR "Ha ocurrido un error al intentar reservar memoria\n");
        return -ENOMEM; /* Out of memory */
    }
    
    sema_init(&sem_mutex, 1);
    sema_init(&semaphore_productores, 0);
    sema_init(&semaphore_consumidores, 0);

    proc_entry = proc_create_data("modfifo", 0666, NULL, &proc_entry_fops, NULL);
    
    if (proc_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_ERR "Ha ocurrido un error al crear la entrada modfifo memoria\n");
        return -ENOMEM; /* Out of memory */
    }
    
    printk(KERN_INFO "Módulo 'modfifo' cargado.\n");

    return 0;
}

void cleanup_fifoproc_module(void) {
    remove_proc_entry("modfifo", NULL);
    kfifo_free(&cbuffer);
    
    printk(KERN_INFO "Módulo 'modfifo' descargado.\n");
}

/**
 Si el proceso es abierto por:
 Un consumidor:
 1º Incrementamos la variable de contador consumidores.
 2º Esperamos a que haya al menos 1 productor.
 Un productor:
 1º Incrementamos la variable de contador productores.
 2º Esperamos a que haya al menos 1 consumidor.
 */

static int fifoproc_open(struct inode *inode, struct file *file)
{
    
    //Si el proceso es depertado antes de decrementar la variable, es decir, se ha interrumpido el proceso
    if (down_interruptible(&sem_mutex))
    {
        return -EINTR; /* Interrupted system call */
    }
    
    if (file->f_mode & FMODE_READ) {
        contador_consumidores++;
        
        while (contador_productores == 0) {
            up(&sem_mutex); //Operación signal del semáforo.
            
            if (down_interruptible(&semaphore_consumidores)) {
                return -EINTR; /* Interrupted system call */
            }
        }
        
        up(&semaphore_productores);
    } else {
        contador_productores++;
        
        while (contador_consumidores == 0) {
            up(&sem_mutex);
            
            if (down_interruptible(&semaphore_productores)) {
                return -EINTR; /* Interrupted system call */
            }
        }
        
        up(&semaphore_consumidores);
    }
    
    up(&sem_mutex);
    
    return 0;
}

static ssize_t fifoproc_write(struct file *file, const char *buff, size_t len, loff_t *off) {
    printk(KERN_INFO "Estoy en fifoproc_write\n");

    char kbuffer[MAX_CBUFFER_LEN];
    
    if (len > MAX_CBUFFER_LEN) {
        return -ENOSPC; /* No space left on device */
    }
    
    if (copy_from_user(kbuffer, buff, len)) {
        return -EFAULT; /* Bad address */
    }
    
    if (down_interruptible(&sem_mutex)) {
        return -EINTR; /* Interrupted system call */
    }
    
    /* Esperar hasta que haya hueco para insertar (debe haber consumidores)
     1º- Comprobamos si hay suficiente hueco para la cadena de tamaño 'len' (kfifo_avail).
     2º- También comprobamos si hay consumidores en caso nos bloqueamos.
     Si se cumplen las 2 condiciones nos bloqueamos.
     */
    while (kfifo_avail(&cbuffer) < len && contador_consumidores > 0)
    {
        contador_productores_esperando++;
        
        up(&sem_mutex);
        
        //Bloqueo
        if (down_interruptible(&semaphore_productores)) {
            down(&sem_mutex);
            contador_productores_esperando--;
            up(&sem_mutex);
            return -EINTR;
        }
        
        //Obtenemos el mutex antes de entrar a la sección crítica.
        if (down_interruptible(&sem_mutex)) {
            return -EINTR;
        }
    }
    
    //Si no hay consumidores, liberamos mutex y devolvemos error.
    if (contador_consumidores == 0)
    {
        up(&sem_mutex);
        return -EPIPE; /* Broken pipe */
    }
    
    //Introducimos datos
    kfifo_in(&cbuffer, kbuffer, len);
    
    /* Despertar a posible consumidor bloqueado */
    up(&semaphore_consumidores);
    
    up(&sem_mutex);
    
    return len;
}

static ssize_t fifoproc_read(struct file *file, char *buff, size_t len, loff_t *off) {
    printk(KERN_INFO "Estoy en fifoproc_read\n");

    char kbuffer[MAX_CBUFFER_LEN];
    
    if (len > MAX_CBUFFER_LEN) {
        return -ENOSPC; /* No space left on device */
    }
    
    //Adquirimos el mutex.
    if (down_interruptible(&sem_mutex)) {
        return -EINTR; /* Interrupted system call */
    }
    
    /*
     Nos bloqueamos si:
     1º Si no están todos los datos que queremos leer, es decir,
     en 'cbuffer' es menor que el número de elementos a leer que es 'len'.
     */
    while (kfifo_len(&cbuffer) < len && contador_productores > 0) {
        contador_consumidores_esperando++;
        
        up(&sem_mutex);
        
        if (down_interruptible(&semaphore_consumidores)) {
            down(&sem_mutex);
            contador_consumidores_esperando--;
            up(&sem_mutex);
            return -EINTR;
        }
        
        if (down_interruptible(&sem_mutex)) {
            return -EINTR;
        }
    }
    
    //Si no hay productores, liberamos mutex y devolvemos error.
    if (contador_productores == 0 && kfifo_is_empty(&cbuffer)) {
        up(&sem_mutex);
        return 0; /* Broken pipe */
    }
    
    int num_bytes_eliminados = kfifo_out(&cbuffer, kbuffer, len); //Consumimos
    
    //Si los datos eliminados no son igual a los que se esperaban a consumir, devolvemos error
    if (num_bytes_eliminados != len) {
        return -EINVAL;
    }
    
    // Transfer data from the kernel to userspace
    if (copy_to_user(buff, kbuffer, len)) {
        return -EINVAL;
    }
    
    /* Despertar a todos los productores bloqueados */
    up(&semaphore_productores);
    
    up(&sem_mutex);
    
    return len;
}

static int fifoproc_release(struct inode *inode, struct file *file) {
    
    //Adquirimos el mutex.
    if (down_interruptible(&sem_mutex)) {
        return -EINTR; /* Interrupted system call */
    }
    
    /**
     Si es un consumidor:
     Decrementamos la variable de consumidores.
     Avisamos a los productores.
     Si es un productor:
     Decrementamos la variable de productores.
     Avisamos a los consumidores por si estuviesen.
     */
    if (file->f_mode & FMODE_READ) {
        contador_consumidores--;
        
        if (contador_productores_esperando != 0) {
            up(&semaphore_productores);
        }
        
        up(&semaphore_consumidores);
    } else {
        contador_productores--;
        
        if (contador_consumidores_esperando != 0) {
            up(&semaphore_consumidores);
        }
        
        up(&semaphore_productores);
    }
    
    //Si uno de los 2 ha llegado a 0, se rompe la "tubería".
    if (contador_consumidores_esperando == 0 && contador_productores_esperando == 0)  {
        kfifo_reset(&cbuffer);
    }
    
    up(&sem_mutex);
    
    return 0;
}

static const struct file_operations proc_entry_fops = {
    .read = fifoproc_read,
    .write = fifoproc_write,
    .open = fifoproc_open,
    .release = fifoproc_release,
};

module_init(init_fifoproc_module);
module_exit(cleanup_fifoproc_module);
