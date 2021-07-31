//Yhondri Josué Acosta Novas - Hao Hao He
#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define MAX_SIZE 32
#define BUF_SIZE 256

DEFINE_SPINLOCK(sp);

static struct proc_dir_entry *proc_modtimer;
static struct proc_dir_entry *proc_modconfig;
struct work_struct work;
struct timer_list my_timer_list; //Contiene el timer del kernel.
struct kfifo cbuffer; //Buffer circular.
struct list_head my_list;
struct semaphore mtx; //Garantizar exclusión mutua.
struct semaphore consumidores_semaphore; //Cola de consumidores.

struct list_item_t {
	unsigned int data;
	struct list_head links;
} list_item_t;

unsigned long flags;
unsigned long timer_period_ms = 500; 
int emergency_threshold = 80;
unsigned int max_random = 300;
int productores_counter = 0; // Número de procesos de la escårictura en la entrada /proc.
int consumidores_counter = 0; // Número de procesos de la lectura en la entrada /proc.
int consumidores_waiting = 0;

void copy_items_into_list(struct work_struct *work) {
	int length;
	int index = 0;
	int result;
	int aux;
	unsigned int kbuffer[MAX_SIZE];
	struct list_item_t *node;

	spin_lock_irqsave(&sp, flags);
	length = kfifo_len(&cbuffer);
	result = kfifo_out(&cbuffer, kbuffer, length);
	spin_unlock_irqrestore(&sp, flags);
	aux = length/4;
	down(&mtx);

	spin_lock_irqsave(&sp, flags);
	spin_unlock_irqrestore(&sp, flags);

	while (aux > 0)
	{
		node = (struct list_item_t *)vmalloc(sizeof(struct list_item_t));
		node->data = kbuffer[index];
		spin_lock_irqsave(&sp, flags);
		list_add_tail(&node->links, &my_list);
		spin_unlock_irqrestore(&sp, flags);
		index++;
		aux--;
	}

	if (consumidores_waiting > 0)
	{
		up(&consumidores_semaphore);
		consumidores_waiting--;
	}

	up(&mtx);
}

static void fire_timer(unsigned long data) {
	int length, percent;
	unsigned int random_number;
	random_number = (get_random_int() % max_random);

	printk(KERN_INFO "Número random generado en fire_timer %u\n", random_number);

	spin_lock_irqsave(&sp, flags);
	kfifo_in(&cbuffer, &random_number, sizeof(unsigned int));
	length = kfifo_len(&cbuffer);
	spin_unlock_irqrestore(&sp, flags);

	percent = (100*length)/MAX_SIZE;

	if (percent >= emergency_threshold)
	{
		int current_cpu;
		int work_cpu = 0;

		current_cpu = smp_processor_id();
		if (current_cpu == 0)
		{
			work_cpu = 1;
		}
		
		INIT_WORK(&work, copy_items_into_list);

		schedule_work_on(work_cpu, &work); // Work queue
	}
	
	mod_timer(&(my_timer_list), jiffies + msecs_to_jiffies(timer_period_ms));
}

static ssize_t modtimer_read(struct file *inputFile, char __user *buf, size_t length, loff_t *offset) {
	struct list_item_t *item = NULL;
	struct list_head *cur_node = NULL;
	struct list_head *aux;
	char kbuf[BUF_SIZE];
	int bytes_counter = 0;

	down(&mtx);

	while (list_empty(&my_list))
	{
		consumidores_waiting++;

		up(&mtx);

		if (down_interruptible(&consumidores_semaphore))
		{
			consumidores_waiting--;
			return -EINTR;
		}
		
		down(&mtx);
	}

	list_for_each_safe(cur_node, aux, &my_list) {
		item = list_entry(cur_node, struct list_item_t, links);
		sprintf(&kbuf[bytes_counter], "%u\n", item->data);
		bytes_counter = strlen(kbuf);
		list_del(cur_node);
		vfree(item);
	}

	up(&mtx);

	if(length < bytes_counter) {
		return -ENOSPC;
	}

	if (copy_to_user(buf, kbuf, bytes_counter))
	{
		return -EINVAL;
	}

	return bytes_counter;	
}

static int modtimer_open(struct inode *inode, struct file *file) {
	//Inicializamos datos
	my_timer_list.data = 0;
	my_timer_list.function = fire_timer;
	my_timer_list.expires = jiffies + timer_period_ms + HZ;

	add_timer (&my_timer_list);

	return 0;
}

static int mod_timer_release(struct inode *inode, struct file *file) {
	struct list_item_t *item = NULL;
	struct list_head *cur_node = NULL;
	struct list_head *aux; 

	del_timer_sync(&my_timer_list);
	flush_scheduled_work();

	spin_lock_irqsave(&sp, flags);
	kfifo_reset(&cbuffer);
	spin_unlock_irqrestore(&sp, flags);

	list_for_each_safe(cur_node, aux, &my_list) {
		item = list_entry(cur_node, struct list_item_t, links);
		list_del(cur_node);
		vfree(item);
	}

	return 0;
}

static ssize_t mod_config_read(struct file *filp, char __user *buf, size_t length, loff_t *offset) {
	char kbuf[BUF_SIZE];
	int bytes_counter = 0;
	static bool has_finished = false;

	if (has_finished)
	{
		has_finished = false;
		return 0;
	}

	bytes_counter = sprintf(kbuf, "timer_period_ms=%lu\nemergency_threshold=%i\nmax_random%i\n", timer_period_ms, emergency_threshold, max_random);

	has_finished = true;

	if (copy_to_user(buf, kbuf, bytes_counter))
	{
		return -EFAULT;
	}
	
	return bytes_counter;	
}

static ssize_t mod_config_write(struct file *file, const char __user *buf, size_t length, loff_t *offset) {
	unsigned long num;
	int num_aux;
	char kbuf[BUF_SIZE];

	if (copy_to_user(kbuf, buf, length))
	{
		return -EFAULT;
	}

	if (sscanf(kbuf, "timer_period_ms %lu", &num) == 1)
	{
		timer_period_ms = num;

		printk(KERN_INFO "Periodo Timer %lu\n", timer_period_ms);
	}

	if (sscanf(kbuf, "emergency_threshold %i", &num_aux) == 1)
	{
		if (num_aux > 0 && num_aux <= 100)
		{
			emergency_threshold = num_aux;
		
			printk(KERN_INFO "emergency_threshold %i\n", emergency_threshold);
		} else {
			printk(KERN_ALERT "Error obteniendo emergency_threshold, valor obtenido: %i\n", num_aux);
		}
	}

	if (sscanf(kbuf, "max_random %i", &num_aux) == 1)
	{
		max_random = num_aux;
	}
	
	return length;
}

static const struct file_operations proc_entry_modtimer = {
	.read = modtimer_read,
	.open = modtimer_open,
	.release = mod_timer_release,
};

static const struct file_operations proc_entry_modconfig = {
	.read = mod_config_read,
	.write = mod_config_write,
};

int mod_timer_init(void) {
	int ret = 0;
	ret = kfifo_alloc(&cbuffer, MAX_SIZE, GFP_KERNEL);

	if (ret)
	{
		printk(KERN_ERR "Error reservando memoria para kfifo");
		return -ENOMEM;
	}

	//Init timer
	init_timer(&my_timer_list);

	//Init List
	INIT_LIST_HEAD(&my_list);

	sema_init(&consumidores_semaphore, 0);
	sema_init(&mtx, 1);
	
	proc_modconfig = proc_create_data("modconfig", 0666, NULL, &proc_entry_modconfig, NULL);
	proc_modtimer = proc_create_data("modtimer", 0666, NULL, &proc_entry_modtimer, NULL);

	if (proc_modconfig == NULL || proc_modtimer == NULL)
	{
		kfifo_free(&cbuffer);
		return -ENOMEM;
	}
	
	return ret;	
}

void mod_timer_clean(void) {
	kfifo_free(&cbuffer);
	del_timer_sync(&my_timer_list);
	remove_proc_entry("modtimer", NULL);
	remove_proc_entry("modconfig", NULL);
}

module_init(mod_timer_init);
module_exit(mod_timer_clean);
