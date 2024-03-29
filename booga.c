/*
 * example.c -- the bare example char module
 * This example shows how to use a semaphore to avoid race conditions
 * in updating a global data structure inside a driver.
 */
 
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/version.h> /* printk() */
#include <linux/init.h>  /*  for module_init and module_cleanup */
#include <linux/slab.h>  /*  for kmalloc/kfree */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/proc_fs.h>	/* for the proc filesystem */
#include <linux/seq_file.h>
#include "booga.h"        /* local definitions */

#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/uaccess.h>
#include <asm-generic/current.h>

static int booga_major =   BOOGA_MAJOR;
static int booga_nr_devs = BOOGA_NR_DEVS;    /* number of bare example devices */
module_param(booga_major, int, 0);
module_param(booga_nr_devs, int, 0);
MODULE_AUTHOR("Pablo Lomeli");
MODULE_LICENSE("GPL");

uint current_device;
uint phrases_count[4];
char * phrases[] = {"booga! booga!", "googoo! gaagaa!", "wooga! wooga!", "neka! maka!"};
static booga_stats *booga_device_stats;
static struct proc_dir_entry* booga_proc_file;

static ssize_t booga_read (struct file *, char *, size_t , loff_t *);
static ssize_t booga_write (struct file *, const char *, size_t , loff_t *);
static int booga_open (struct inode *, struct file *);
static int booga_release (struct inode *, struct file *);
static int booga_proc_open(struct inode *inode, struct file *file);

static char* get_phrase(void);
void sighandler(int);

/*  The different file operations */
/* The syntax you see below is an extension to gcc. The prefered */
/* way to init structures is to use C99 Taged syntax */
/* static struct file_operations example_fops = { */
/* 		    .read    =       example_read, */
/* 			.write   =       example_write, */
/* 			.open    =       example_open, */
/* 			.release =       example_release */
/* }; */
/*  This is where we define the standard read,write,open and release function */
/*  pointers that provide the drivers main functionality. */
static struct file_operations booga_fops = {
		    read:       booga_read,
			write:      booga_write,
			open:       booga_open,
			release:    booga_release,
};

/* The file operations struct holds pointers to functions that are defined by */
/* driver impl. to perform operations on the device. What operations are needed */
/* and what they should do is dependent on the purpose of the driver. */
static const struct file_operations booga_proc_fops = {
		.owner	= THIS_MODULE,
		.open	= booga_proc_open,
		.read	= seq_read,
		.llseek	= seq_lseek,
		.release = single_release,
};


/*
 * Open and close
 */
static int booga_open (struct inode *inode, struct file *filp)
{
	int num = NUM(inode->i_rdev);
	if (num >= booga_nr_devs) return -ENODEV;
	
	printk("<1>booga_open invoked.%d\n", num);
	
	filp->f_op = &booga_fops;
	/* need to protect this with a semaphore if multiple processes
	   will invoke this driver to prevent a race condition */
	   
	if (down_interruptible (&booga_device_stats->sem))
			return (-ERESTARTSYS);
	
	current_device = num;
	booga_device_stats->devs[num].opens++;
	//booga_device_stats->num_open++; No longer track here
	up(&booga_device_stats->sem);

	try_module_get(THIS_MODULE);
	return 0;          /* success */
}

static int booga_release (struct inode *inode, struct file *filp)
{
	/* need to protect this with a semaphore if multiple processes
	   will invoke this driver to prevent a race condition */
	
	if (down_interruptible (&booga_device_stats->sem))
			return (-ERESTARTSYS);
	//booga_device_stats->num_close++; No longer track here
	up(&booga_device_stats->sem);

	module_put(THIS_MODULE);
	return 0;
}

/*
 * random used here to get phrase
 */
static char* get_phrase(void){
	char randval;
	uint choice;
	get_random_bytes(&randval, 1);
	choice = (randval & 0x7F) % 4;
	phrases_count[choice]++;
	return phrases[choice];
}

/*
 * Data management: read and write ( Professor note: most time here (r/w) )
 */

static ssize_t booga_read (struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	int result;
	int status;
    int i;
	char *randomstr;
	char *str;
    char *finalstr;
	
	printk("<1>booga_read invoked.\n");
	
	/* need to protect this with a semaphore if multiple processes
	   will invoke this driver to prevent a race condition */
	
	if (down_interruptible (&booga_device_stats->sem))
			return (-ERESTARTSYS); 
		
	
    finalstr = (char *) kmalloc(sizeof(char)*count, GFP_KERNEL);
	if (!finalstr) {
		result = -ENOMEM;
		return result;
	}
	
	randomstr = get_phrase();
    str = randomstr;
	
	// concat finalstr and add space between ending of phrases
	for(i=0; i<count; i++) {
		if(*str=='\0') {
            str = randomstr;
            finalstr[i] = ' ';
			continue;
		}
        else {
            finalstr[i] = *str;
            str++;
        }
	}
	
	// prints finalstr to console
	status = __copy_to_user(buf, finalstr, count);

	if (status > 0)
		printk("booga: Could not copy %d bytes\n", status);
    
    if(finalstr)
        kfree(finalstr);
    
	booga_device_stats->bytes_read += count;
	up(&booga_device_stats->sem);
	
	return count;
}

static ssize_t booga_write(struct file *filp, const char *buf, size_t count , loff_t *f_pos)
{
	printk("<1>booga_write invoked.\n");
	
	/* need to protect this with a semaphore if multiple processes
	   will invoke this driver to prevent a race condition */
	
	if(down_interruptible (&booga_device_stats->sem))
			return (-ERESTARTSYS);
	
	// kills booga 3 device
	if(current_device==3){
	    printk("Send SIGTERM on device 3\n");
		count = 0;
		send_sig(SIGTERM, current, 0);
	}
	
	booga_device_stats->bytes_written += count;
	up(&booga_device_stats->sem);
	
	return count; // pretend that count bytes were written
}

static void init_booga_device_stats(void)
{
	int i;
	booga_device_stats->bytes_read = 0;
	booga_device_stats->bytes_written = 0;
	
	
	for(i=0; i<booga_nr_devs; i++){
		booga_device_stats->devs[i].opens=0;
		phrases_count[i] = 0;
	}
	
	sema_init(&booga_device_stats->sem, 1);
	
}

static int booga_proc_show(struct seq_file *m, void *v)
{
	int i;
	seq_printf(m, "bytes read = %ld \n", booga_device_stats->bytes_read);
	seq_printf(m, "bytes written = %ld \n", booga_device_stats->bytes_written);
	
	seq_printf(m, "number of opens:\n");
	
	for(i=0; i<booga_nr_devs; i++)
		seq_printf(m, "\t/dev/booga%d\t= %d times\n", i, booga_device_stats->devs[i].opens);
	
	seq_printf(m, "strings output:\n");
	
	i=0;
	for(i=0; i<booga_nr_devs; i++)
		seq_printf(m, "\t%s\t= %d times\n", phrases[i], phrases_count[i]);
	
	return 0;
		
}

static int booga_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, booga_proc_show, NULL);
}

static __init int booga_init(void)
{
	int result;

	/*
	 * Register your major, and accept a dynamic number
	 */
	result = register_chrdev(booga_major, "booga", &booga_fops);
	if (result < 0) {
			printk(KERN_WARNING "booga: can't get major %d\n",booga_major);
			return result;
	}
	if (booga_major == 0) booga_major = result; /* dynamic */
	printk("<1> booga device driver version 4: loaded at major number %d\n", booga_major);

	booga_device_stats = (booga_stats *) kmalloc(sizeof(booga_stats),GFP_KERNEL);
	if (!booga_device_stats) {
			result = -ENOMEM;
			goto fail_malloc;
	}
	init_booga_device_stats();

	/* We assume that the /proc/driver exists. Otherwise we need to use proc_mkdir to
	 * create it as follows: proc_mkdir("driver", NULL);
	 */
	booga_proc_file = proc_create("driver/booga", 0, NULL, &booga_proc_fops);
	if (!booga_proc_file)  {
			result = -ENOMEM;
			goto fail_malloc;
	}

	return 0;

	fail_malloc:
		unregister_chrdev(booga_major, "booga");
		return  result;
}



static __exit void booga_exit(void)
{
	remove_proc_entry("driver/booga", NULL /* parent dir */);
	kfree(booga_device_stats);
	unregister_chrdev(booga_major, "booga");
	printk("<1> booga device driver version 4: unloaded\n");
}


module_init(booga_init);
module_exit(booga_exit);

/* vim: set ts=4: */
