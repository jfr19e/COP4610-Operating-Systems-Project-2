#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple module featuring proc read");

#define ENTRY_NAME "timed"
#define ENTRY_SIZE 65
#define PERMS 0644
#define PARENT NULL
static struct file_operations fops;

static char * message;
static int read_p;
static struct timespec currentTime;


/* time_proc_open() modifies the message variable so that it
 * contains the current amount of time since the epoch, and
 * is additionally updated with the elapsed time since the last call
 */
int time_proc_open(struct inode *sp_inode, struct file *sp_file)
{
	struct timespec t;
	long int secs;
	long int nsecs;

	char * buf = kmalloc (sizeof(char) * 100, __GFP_RECLAIM);	
	if (buf == NULL)
	{
		printk(KERN_WARNING "print_time");
		return -ENOMEM;
	}


	t = current_kernel_time();

	printk(KERN_INFO "proc called open\n");
	
	read_p = 1;
	message = kmalloc(sizeof(char) * ENTRY_SIZE,
			 __GFP_RECLAIM | __GFP_IO | __GFP_FS);

	if (message == NULL)
	{
		printk(KERN_WARNING "time_proc_open");
		return -ENOMEM;
	}

	strcpy(message, "");

	sprintf(buf, "current time: %ld.%09ld\n", t.tv_sec, t.tv_nsec);
	strcat(message, buf);

	if (currentTime.tv_sec)
	{
		secs = t.tv_sec - currentTime.tv_sec;
		if (t.tv_nsec - currentTime.tv_nsec < 0)	
		{
			nsecs = t.tv_nsec - 
				currentTime.tv_nsec + 1000000000;
			secs--;
		}
		else
			nsecs = t.tv_nsec - currentTime.tv_nsec;


		sprintf(buf, "elapsed time: %ld.%09ld\n", secs, nsecs);
		strcat(message, buf);
	}

	currentTime = current_kernel_time();

	return 0;
}


/* time_proc_read() copies the contents of message to
 * the /proc/timed entry
 */ 
ssize_t time_proc_read(struct file *sp_file, char __user *buf,
		       		   size_t size, loff_t *offset)
{
	int len = strlen(message);
	
	read_p = !read_p;
	if (read_p)
		return 0;

	printk(KERN_INFO "proc called read\n");

	copy_to_user(buf, message, len);
	return len;
}


/* time_proc_release() frees the message variable and
 * releases the /proc/timed entry
 */
int time_proc_release(struct inode *sp_inode, struct file *sp_file)
{
	printk(KERN_NOTICE "proc called release\n");
	kfree(message);
	return 0;
}


/* time_init() sets fops.open, fops.read, and fops.release, as
 * well as creating the file /proc/timed
 */
static int time_init(void)
{
	printk(KERN_NOTICE "/proc/%s create\n",ENTRY_NAME);
	fops.open = time_proc_open;
	fops.read = time_proc_read;
	fops.release = time_proc_release;
	
	if (!proc_create(ENTRY_NAME, PERMS, NULL, &fops))
	{
		printk(KERN_WARNING "proc create\n");
		remove_proc_entry(ENTRY_NAME, NULL);
		return -ENOMEM;
	}
	
	return 0;
}
module_init(time_init);


/* time_exit() removes the my_xtime module and the /proc/timed entry */ 
static void time_exit(void)
{
	remove_proc_entry(ENTRY_NAME, NULL);
	printk(KERN_NOTICE "Removing /proc/%s\n", ENTRY_NAME);
}
module_exit(time_exit);
