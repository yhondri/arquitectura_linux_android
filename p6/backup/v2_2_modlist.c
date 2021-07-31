#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/init.h>

MODULE_LICENSE("GPL"); /*  Licencia del modulo */

#define BUFFER_LENGTH 200
#define COMMAND_BUFFER_LENGHT 100
#define MAX_NAME_LENGHT 64

//SMP-safe
struct semaphore my_locker;

static struct proc_dir_entry *mod_dir;
static struct proc_dir_entry *control_entry;

static char *mi_modulo; // Space for the "mi_modulo"

//Número máximo de entradas que pueden existir a la vez.
static int max_entries = 4;
module_param(max_entries, int, 0660); //Para obtener parámetros a través del terminal (más info al final).
//Tamaño máximo de listas.
static int max_size = 64;
module_param(max_size, int, 0660);

int number_lists;

struct list_head mylist; /* Lista enlazada */
/* Nodos de la lista */
struct list_item
{
    int data;
    struct list_head links;
    char *name;
    int productores;
    int consumidores;
    struct semaphore mtx;
    struct semaphore productores_sem;
    struct semaphore consumidores_sem;
    int productores_waiting;
    int consumidores_waiting;
};

void insert_item(int new_number, struct list_head *m_list_head);
void remove_item(int item);
void clean_up(void);
int create_new_list(char *list_name);
int delete_list(char *list_name);
void delete_all_lists(void);
static ssize_t control_write(struct file *file, const char __user *buf, size_t len, loff_t *off);

static ssize_t mi_modulo_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{

    struct list_item *m_list = (struct list_item *)PDE_DATA(filp->f_inode);
    int available_space = BUFFER_LENGTH - 1;
    char commandBuffer[COMMAND_BUFFER_LENGHT]; //definimos el buffer para obtener el comando.
    int newNumber = 0;

    printk(KERN_INFO "mi_modulo_write 1 y 2\n");

    if ((*off) > 0)
    { /* The application can write in this entry just once !! */
        return 0;
    }

    if (len > available_space)
    {
        printk(KERN_INFO "list: not enough space!!\n");
        return -ENOSPC;
    }

    /* Transfer data from user to kernel space */
    if (copy_from_user(&commandBuffer[0], buf, len))
    {
        return -EFAULT;
    }

    commandBuffer[len] = '\0'; /* Add the `\0' */

    *off += len; /* Update the file pointer */

    if (sscanf(commandBuffer, "add %d", &newNumber) == 1)
    {
        // printk(KERN_INFO "Comando add introducido. %d\n", newNumber);
        insert_item(newNumber, &m_list->links);
        return len;
    }
    else if (sscanf(commandBuffer, "remove %d", &newNumber) == 1)
    {
        // printk(KERN_INFO "Comando remove introducido.\n");

        remove_item(newNumber);

        return len;
    }
    else if (sscanf(commandBuffer, "cleanup") == 1)
    {
        // printk(KERN_INFO "Comando cleanup introducido.\n");
        clean_up();

        return len;
    }
    else
    {
        printk(KERN_INFO "Comando inválido [Comando admitidos, add, remove, cleanup].\n");
        return EINVAL;
    }

    return len;
}

static ssize_t mi_modulo_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{

    int nr_bytes;
    int nr_bytes_temporal;
    char number_char_list[COMMAND_BUFFER_LENGHT];
    char buffer_auxiliar[10];

    struct list_head *current_head = NULL; //Posición actual.
    struct list_item *current_item = NULL; //Item/posición actual de la lista.
    struct list_item *m_list = (struct list_item *)PDE_DATA(filp->f_inode);

    nr_bytes = 0;
    nr_bytes_temporal = 0;

    if ((*off) > 0)
    { /* Tell the application that there is nothing left to read */
        return 0;
    }

    printk(KERN_INFO "mi_modulo_read 1 y 2 \n");

      if (down_interruptible(&m_list->mtx))
    {
        return -EINTR; //Ha llegado una señal.
    }

    printk(KERN_INFO "mi_modulo_read 3 \n");

    while (m_list->productores > 0)
    {
        printk(KERN_INFO "mi_modulo_read: Waiting productores... \n");
        m_list->consumidores_waiting++;
        up(&m_list->mtx);
        if (down_interruptible(&m_list->consumidores_sem))
        {
            down(&m_list->mtx);
            m_list->consumidores_waiting--;
            up(&m_list->mtx);
            return -EFAULT;
        }

        if (down_interruptible(&m_list->mtx))
        {
            return -EINTR; //Ha llegado una señal.
        }
    }

        printk(KERN_INFO "mi_modulo_read 3.1 \n");

    if (m_list->productores == 0)
    {
        up(&m_list->mtx);
        return -EPIPE; /* Broken pipe */
    }

            printk(KERN_INFO "mi_modulo_read 3.2 \n");

    list_for_each(current_head, &m_list->links)
    {
        printk(KERN_INFO "mi_modulo_read 3.3 \n");
        current_item = list_entry(current_head, struct list_item, links);
        printk(KERN_INFO "mi_modulo_read 4 \n");

        nr_bytes_temporal = sprintf(buffer_auxiliar, "%d\n", current_item->data);
        printk(KERN_INFO "mi_modulo_read 5 \n");

        if ((nr_bytes_temporal + nr_bytes) >= COMMAND_BUFFER_LENGHT - 1)
        {
            // up(&m_list->mtx);
            return -ENOSPC; //No hay más espacio.
        }

        printk(KERN_INFO "mi_modulo_read 6 \n");

        nr_bytes += sprintf(&number_char_list[nr_bytes], "%d\n", current_item->data);

        printk(KERN_INFO "mi_modulo_read 7 \n");
    }

    printk(KERN_INFO "En mi_modulo_read 8 \n");

    if (m_list->productores_waiting > 0)
    {
        up(&m_list->productores_sem);
        m_list->productores_waiting--;
    }

    up(&m_list->mtx);

    if (len < nr_bytes)
        return -ENOSPC;

    // Transfer data from the kernel to userspace
    if (copy_to_user(buf, number_char_list, nr_bytes))
    {
        return -EINVAL;
    }

    // printk("Cadena recibida: %d", 1);

    (*off) += len;

    printk(KERN_INFO "En mi_modulo_read 9 \n");

    return nr_bytes;
}

static int mi_modulo_open(struct inode *inode, struct file *file)
{

    printk(KERN_INFO "En mi_modulo_open 1 y 2\n");

    struct list_item *m_list = (struct list_item *)PDE_DATA(file->f_inode);
    int i;

    if (down_interruptible(&m_list->mtx))
    {
        return -EINTR; //Ha llegado una señal.
    }


    if (file->f_mode & FMODE_WRITE)
    {
        m_list->productores++;
        if (m_list->productores == 0) // No hay porductores.
        {
            up(&m_list->mtx);
            if (down_interruptible(&m_list->productores_sem))
            {
                down(&m_list->mtx);
                m_list->productores--;
                up(&m_list->mtx);
                return -EPIPE; //Ha llegado una señal.
            }

            if (down_interruptible(&m_list->mtx))
            {
                return -EPIPE; //Ha llegado una señal.
            }
        }

        if (m_list->consumidores > 0 && m_list->productores > 0)
        {
            for (i = 0; i < m_list->consumidores; i++)
            {
                up(&m_list->consumidores_sem);
            }
        }

        printk(KERN_INFO "En mi_modulo_open 2.1 \n");
    }
    else if (file->f_mode & FMODE_READ)
    {
        m_list->consumidores++;
        if (m_list->consumidores == 0) // No hay porductores.
        {
            up(&m_list->mtx);
            if (down_interruptible(&m_list->consumidores_sem))
            {
                down(&m_list->mtx);
                m_list->consumidores--;
                up(&m_list->mtx);
                return -EPIPE; //Ha llegado una señal.
            }

            if (down_interruptible(&m_list->mtx))
            {
                return -EPIPE; //Ha llegado una señal.
            }
        }

        if (m_list->consumidores > 0 && m_list->productores > 0)
        {
            for (i = 0; i < m_list->productores; i++)
            {
                up(&m_list->productores_sem);
            }
        }

        printk(KERN_INFO "En mi_modulo_open 2.2 \n");
    }

    up(&m_list->mtx);

    printk(KERN_INFO "Open: Productores %d -- Consumidores %d\n", m_list->productores, m_list->consumidores);

    printk(KERN_INFO "En mi_modulo_open 2 \n");
    return 0;
}

static int mi_modulo_release(struct inode *inode, struct file *file)
{

    struct list_item *m_list;
    m_list = (struct list_item *)PDE_DATA(file->f_inode);

    if (down_interruptible(&m_list->mtx))
    {
        return -EINTR; //Ha llegado una señal.
    }

    if (file->f_mode & FMODE_WRITE)
    {
        m_list->productores--;
        if (m_list->productores == 0 && m_list->consumidores_waiting > 0) // No hay porductores.
        {
            int counter = 0;
            int consumidores_waiting = m_list->consumidores_waiting;
            while (counter < consumidores_waiting)
            {
                m_list->consumidores_waiting--;
                up(&m_list->consumidores_sem);
                counter++;
            }
        }

        printk(KERN_INFO "En mi_modulo_release 1.1 \n");
    }
    else if (file->f_mode & FMODE_READ)
    {
        m_list->consumidores--;
        if (m_list->consumidores == 0 && m_list->productores_waiting > 0) // No hay consumidores.
        {
            int counter = 0;
            int productores_waiting = m_list->productores_waiting;
            while (counter < productores_waiting)
            {
                m_list->productores_waiting--;
                up(&m_list->productores_sem);
                counter++;
            }
        }

        printk(KERN_INFO "En mi_modulo_release 1.2 \n");
    }

    if ((m_list->consumidores == 0) && (m_list->productores == 0))
    {
        /* TODO: Eliminar elementos de lista*/
        number_lists--;
    }
   
    up(&m_list->mtx);

    printk(KERN_INFO "Released: Productores %d -- Consumidores %d\n", m_list->productores, m_list->consumidores);

    printk(KERN_INFO "En mi_modulo_release 2 \n");

    return 0;
}

static const struct file_operations proc_entry_fops = {
    .read = mi_modulo_read,
    .write = mi_modulo_write,
    .release = mi_modulo_release,
    .open = mi_modulo_open,
};

static const struct file_operations proc_entry_control_fops = {
    .write = control_write,
};

int create_new_list(char *list_name)
{
    struct list_head *item_head;
    struct list_head *item_head_auxiliar;
    struct list_item *new_list_item;
    struct list_item *item_auxiliar;
    struct proc_dir_entry *new_entry;
    int list_exist;

    //region Inicializar variables
    list_exist = 0;
    new_list_item = (struct list_item *)vmalloc(sizeof(struct list_item));
    //endregion Inicializar variables

    printk(KERN_INFO "En create_new_list 1 \n");

    //Vamos a consultar un valor que puede haber sido cambiado por otro proceso, por lo que necesitamos asegurarnos la exclusión mutua.
    if (down_interruptible(&my_locker))
    {
        return -ENAVAIL;
    }

    //Controlar si se ha superado el número máximo de listas.
    if (number_lists >= max_entries)
    {
        vfree(new_list_item);
        up(&my_locker);
        printk(KERN_INFO "There was a problem creating new_list\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "En create_new_list 2 \n");

    //Controlar si ya existe una lista con ese nombre.
    list_for_each_safe(item_head, item_head_auxiliar, &mylist)
    {
        item_auxiliar = list_entry(item_head, struct list_item, links);
        if (strcmp(item_auxiliar->name, list_name) == 0)
        {
            list_exist = 1;
        }
    }

    printk(KERN_INFO "En create_new_list 3 \n");

    up(&my_locker);

    if (list_exist)
    {
        printk(KERN_INFO "Ya existe una lista con este nombre\n");
        vfree(new_list_item);
        return -EINVAL;
    }

    printk(KERN_INFO "En create_new_list 4 \n");

    //Crear lista
    sema_init(&new_list_item->mtx, 1);
    sema_init(&new_list_item->productores_sem, 0);
    sema_init(&new_list_item->consumidores_sem, 0);
    new_list_item->productores_waiting = 0;
    new_list_item->consumidores_waiting = 0;
    new_list_item->productores = 0;
    new_list_item->consumidores = 0;
    new_list_item->name = (char *)vmalloc(sizeof(MAX_NAME_LENGHT));
    strcpy(new_list_item->name, list_name);

    printk(KERN_INFO "En create_new_list 5 \n");

    INIT_LIST_HEAD(&new_list_item->links);

    if (down_interruptible(&my_locker))
    {
        return -ENAVAIL;
    }

    number_lists++;

    up(&my_locker);

    printk(KERN_INFO "En create_new_list 6 \n");

    new_entry = proc_create_data(list_name, 0666, mod_dir, &proc_entry_fops, (void *)new_list_item);
    if (new_entry == NULL)
    {
                printk(KERN_INFO "No se ha podido crear la entrada\n");
        delete_list(list_name);
        printk(KERN_INFO "No se ha podido crear la entrada\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "En create_new_list 7 \n");

    printk(KERN_INFO "Lista %s creada\n", list_name);

    return 0;
}

int delete_list(char *list_name)
{
    struct list_head *item_head = NULL;
    struct list_head *item_head_auxiliar = NULL;
    struct list_item *item = NULL;
    int list_found = -1;

    if (down_interruptible(&my_locker))
    {
        return -ENAVAIL;
    }

    //Controlar si ya existe una lista con ese nombre.
    list_for_each_safe(item_head, item_head_auxiliar, &mylist)
    {
        item = list_entry(item_head, struct list_item, links);
        if (strcmp(item->name, list_name) == 0)
        {
            list_found = 0;
            number_lists--;
            list_del(item_head);
            strcpy(list_name, item->name);
            vfree(item->name);
            vfree(item);
        }
    }

    up(&my_locker);

    if (list_found == 0)
    {
        remove_proc_entry(list_name, mod_dir);
        printk(KERN_INFO "Lista eliminada %s\n", list_name);
    }
    else
    {
        printk(KERN_INFO "No se ha eliminado la lista %s porque no se ha encontrado.\n", list_name);
        return -ENOMEM;
    }

    return 0;
}

void delete_all_lists(void)
{
    struct list_head *item_head;
    struct list_head *m_head_auxiliar;
    struct list_item *item;

    printk(KERN_INFO "delete_all_lists 1.\n");

    if (down_interruptible(&my_locker))
    {
        return;
    }

    list_for_each_safe(item_head, m_head_auxiliar, &mylist)
    {
        number_lists--;
        item = list_entry(item_head, struct list_item, links);
        list_del(item_head);
        remove_proc_entry(item->name, mod_dir);
        vfree(item->name);
        vfree(item);
    }

    up(&my_locker);

    printk(KERN_INFO "delete_all_lists 2.\n");

    printk(KERN_INFO "All lists have been deleted.\n");
}

static ssize_t control_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{

    char kbuffer[COMMAND_BUFFER_LENGHT];
    char command_name[50];

    if (len > max_size)
    {
        return -EFAULT; /* Bad address */
    }

    if (copy_from_user(kbuffer, buf, len))
    {
        return -EFAULT; /* Bad address */
    }

    kbuffer[len] = '\0';

    if (sscanf(kbuffer, "create %s", command_name))
    {
        if (strcmp(command_name, "control") != 0)
        {
            if (create_new_list(command_name) != 0)
            {
                return -ENOMEM;
            }
        }
        else
        {
            return -EINVAL;
        }
    }

    return len;
}

/*Función para insertar un nuevo número en la lista*/
void insert_item(int number, struct list_head *m_list_head)
{

    //Hay que poner el tipo struct si no, no compila :S.
    struct list_item *new_item;
    new_item = vmalloc(sizeof(struct list_item)); //Reservamos memoria para el nuevo item.
    new_item->data = number;                      //Añadiemos el nuevo número al nodo.
                                                  // write_lock(&rwl);

    printk(KERN_INFO "insert_item 1\n");
    list_add_tail(&new_item->links, m_list_head); //Insertamos el item a la lista.
                                                  // write_unlock(&rwl);

    printk(KERN_INFO "insert_item 2\n");
}

/*Función que elimina el número pasado por parámetro
 - Documentación de funciones utilizadas:
 ! list_for_each_safe — iterate over a list safe against removal of list entry
 ! list_entry — get the struct for this entry
 ! list_del — deletes entry from list.
 ! vfree — release memory allocated by vmalloc
 */
void remove_item(int item)
{
    struct list_head *current_head = NULL; //Posición actual.
    struct list_head *auxiliar = NULL;
    struct list_item *current_item = NULL; //Item/posición actual de la lista.

    list_for_each_safe(current_head, auxiliar, &mylist)
    {
        current_item = list_entry(current_head, struct list_item, links);
        if (current_item->data == item)
        {
            // write_lock(&rwl);
            list_del(&current_item->links);
            // write_unlock(&rwl);
            vfree(current_item);
        }
    }
}

/*Función que elimina todos los elementos de la lista */
void clean_up()
{
    struct list_head *current_head = NULL; //Posición actual.
    struct list_head *auxiliar = NULL;
    struct list_item *current_item = NULL; //Item/posición actual de la lista.

    list_for_each_safe(current_head, auxiliar, &mylist)
    {
        current_item = list_entry(current_head, struct list_item, links);
        // write_lock(&rwl);
        list_del(&current_item->links);
        // write_unlock(&rwl);
        vfree(current_item);
    }
}

//endregion Funciones para modificar la lista.

/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_lin_init(void)
{
    printk(KERN_INFO "En modulo_lin_init 1 \n");

    // mi_modulo = (char *)vmalloc(BUFFER_LENGTH);

    // if (!mi_modulo)
    // {
    //     return -ENOMEM;
    // }

    // memset(mi_modulo, 0, BUFFER_LENGTH);
    /* Create proc directory */
    mod_dir = proc_mkdir("list", NULL);

    if (mod_dir == NULL)
    {
        printk(KERN_INFO "List: Can't create /proc/list entry\n");
        return -ENOMEM;
    }

    // default_entry = proc_create("default", 0666, mod_dir, &proc_entry_fops);
    control_entry = proc_create("control", 0666, mod_dir, &proc_entry_control_fops);

    if (control_entry == NULL)
    {
        remove_proc_entry("list", NULL);
        printk(KERN_INFO "list: Can't create /proc entry\n");
        return -ENOMEM;
    }

    sema_init(&my_locker, 1); //Inicializamos semáforo.
    INIT_LIST_HEAD(&mylist);
    number_lists = 0;

    printk(KERN_INFO "En modulo_lin_init 2 \n");

    return create_new_list("default");
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_lin_clean(void)
{
    // delete_all_lists();
    remove_proc_entry("default", mod_dir);
    remove_proc_entry("control", mod_dir);
    remove_proc_entry("list", NULL);
    // vfree(mi_modulo);
    printk(KERN_INFO "Modulo LIN descargado. Adiós kernel.\n");
}

/* Declaración de funciones init y exit */
module_init(modulo_lin_init);
module_exit(modulo_lin_clean);

/* 
 * https://www.tldp.org/LDP/lkmpg/2.6/html/x323.html
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is it's data type
 * The final argument is the permissions bits, 
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */