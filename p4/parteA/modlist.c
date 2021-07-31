#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/list.h>

MODULE_LICENSE("GPL"); /*  Licencia del modulo */

//Lo nuevo 
DEFINE_RWLOCK(rwl);

#define BUFFER_LENGTH PAGE_SIZE
#define COMMAND_BUFFER_LENGHT 100

static struct proc_dir_entry *proc_entry;
static char *mi_modulo; // Space for the "mi_modulo"

struct list_head mylist; /* Lista enlazada */
/* Nodos de la lista */
struct list_item
{
    int data;
    struct list_head links;
};

void insert_item(int new_number);
void remove_item(int item);
void clean_up(void);

static ssize_t mi_modulo_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    int available_space = BUFFER_LENGTH - 1;
    char commandBuffer[COMMAND_BUFFER_LENGHT]; //definimos el buffer para obtener el comando.
    
    if ((*off) > 0)
    { /* The application can write in this entry just once !! */
        return 0;
    }
    
    if (len > available_space)
    {
        printk(KERN_INFO "modlist: not enough space!!\n");
        return -ENOSPC;
    }
    
    /* Transfer data from user to kernel space */
    if (copy_from_user(&commandBuffer[0], buf, len))
    {
        return -EFAULT;
    }
    
    commandBuffer[len] = '\0'; /* Add the `\0' */
    
    int newNumber;
    
    *off += len; /* Update the file pointer */
    
    if (sscanf(commandBuffer, "add %d", &newNumber) == 1)
    {
        //printk(KERN_INFO "Comando add introducido. %d\n", newNumber);
        
        insert_item(newNumber);
        
        return len;
    } else if (sscanf(commandBuffer, "remove %d", &newNumber) == 1) {
        // printk(KERN_INFO "Comando remove introducido.\n");
        
        remove_item(newNumber);
        
        return len;
    } else if (sscanf(commandBuffer, "cleanup") == 1)
    {
        // printk(KERN_INFO "Comando cleanup introducido.\n");
        clean_up();
        
        return len;
    } else {
        printk(KERN_INFO "Comando inválido [Comando admitidos, add, remove, cleanup].\n");
        return EINVAL;
    }
    
    //FALTA AÑADIR MENSAJE ERROR, COMANDO NO VÁLIDO.
    //EINVAL
    
    return len;
}

static ssize_t mi_modulo_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Estoy en modlist\n");
    
    if ((*off) > 0)
    { /* Tell the application that there is nothing left to read */
        return 0;
    }
    
    int nr_bytes = 0;
    int nr_bytes_temporal = 0;
    char number_char_list[COMMAND_BUFFER_LENGHT];
    char buffer_auxiliar[10];
    
    struct list_head *current_head = NULL; //Posición actual.
    struct list_item *current_item = NULL; //Item/posición actual de la lista.
    
    printk(KERN_INFO "Antes de recorrer la lista: \n");
    
    list_for_each(current_head, &mylist) {
        // printk(KERN_INFO "Paso 1: \n");
        current_item = list_entry(current_head, struct list_item, links);
        
        nr_bytes_temporal = sprintf(buffer_auxiliar, "%d\n", current_item->data);

        if ((nr_bytes_temporal+nr_bytes) >= COMMAND_BUFFER_LENGHT-1) {
            return -ENOSPC; //No hay más espacio.
        }
        
        nr_bytes += sprintf(&number_char_list[nr_bytes], "%d\n", current_item->data);
    }
    
    if (len < nr_bytes)
        return -ENOSPC;
    
    // Transfer data from the kernel to userspace
    if (copy_to_user(buf, number_char_list, nr_bytes))
    {
        return -EINVAL;
    }
    
    // printk("Cadena recibida: %d", 1);
    
    (*off) += len;
    
    return nr_bytes;
}

//region Funciones para modificar la lista. 

/*Función para insertar un nuevo número en la lista*/
void insert_item(int number) {
    //Hay que poner el tipo struct si no, no compila :S.
    struct list_item *new_item; //Creamos un puntero al nuevo número.
    new_item = vmalloc(sizeof(struct list_item)); //Reservamos memoria para el nuevo item.
    new_item->data = number; //Añadiemos el nuevo número al nodo.
    write_lock(&rwl);
    list_add_tail(&new_item->links, &mylist);//Insertamos el item a la lista.
    write_unlock(&rwl);
}

/*Función que elimina el número pasado por parámetro
 - Documentación de funciones utilizadas:
 ! list_for_each_safe — iterate over a list safe against removal of list entry
 ! list_entry — get the struct for this entry
 ! list_del — deletes entry from list.
 ! vfree — release memory allocated by vmalloc
 */
void remove_item(int item) {
    struct list_head *current_head = NULL; //Posición actual.
    struct list_head *auxiliar = NULL;
    struct list_item *current_item = NULL; //Item/posición actual de la lista.
    
    list_for_each_safe(current_head, auxiliar, &mylist) {
        current_item = list_entry(current_head, struct list_item, links);
        if (current_item->data == item)
        {
            write_lock(&rwl);
            list_del(&current_item->links);
            write_unlock(&rwl);
            vfree(current_item);
        }
    }
}

/*Función que elimina todos los elementos de la lista */
void clean_up() {
    struct list_head *current_head = NULL; //Posición actual.
    struct list_head *auxiliar = NULL;
    struct list_item *current_item = NULL; //Item/posición actual de la lista.
    
    list_for_each_safe(current_head, auxiliar, &mylist) {
        current_item = list_entry(current_head, struct list_item, links);
        write_lock(&rwl);
        list_del(&current_item->links);
        write_unlock(&rwl);
        vfree(current_item);
    }
}

//endregion Funciones para modificar la lista. 

static const struct file_operations proc_entry_fops = {
    .read = mi_modulo_read,
    .write = mi_modulo_write,
};

/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_lin_init(void)
{
    int ret = 0;
    mi_modulo = (char *)vmalloc(BUFFER_LENGTH);
    
    if (!mi_modulo)
    {
        ret = -ENOMEM;
    }
    else
    {
        
        memset(mi_modulo, 0, BUFFER_LENGTH);
        proc_entry = proc_create("modlist", 0666, NULL, &proc_entry_fops);
        if (proc_entry == NULL)
        {
            ret = -ENOMEM;
            vfree(mi_modulo);
            printk(KERN_INFO "modlist: Can't create /proc entry\n");
        }
        else
        {
            INIT_LIST_HEAD(&mylist);
            
            printk(KERN_INFO "modlist: Module loaded - modlis\n");
        }
    }
    
    return ret;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_lin_clean(void)
{
    remove_proc_entry("modlist", NULL);
    vfree(mi_modulo);
    printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}

/* Declaración de funciones init y exit */
module_init(modulo_lin_init);
module_exit(modulo_lin_clean);
