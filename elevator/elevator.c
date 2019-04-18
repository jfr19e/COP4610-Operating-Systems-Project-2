#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/linkage.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simulates elevator");

#define ENTRY_NAME "elevator"
#define ENTRY_SIZE 700
#define PERMS 0644
#define PARENT NULL
static struct file_operations fops;

static char * message;  
static int read_p;

enum States { OFFLINE, IDLE, LOADING, UP, DOWN };

#define MAX_PASSENGER_UNITS 10
#define MAX_WEIGHT_INT 15
#define MAX_WEIGHT_DEC 0


struct thread_parameter
{
	enum States Current_State;
	int Current_Floor;
	int Next_Floor;
	int Waiting_Passengers[10];
	int Total_Passengers[10];
	
	struct
	{
		int pass_units;
		int weight_int;
		int weight_dec;
	} Current_Load;

	struct list_head list;
	int id;
	struct task_struct * kthread;
	struct mutex mutex;
};

typedef struct
{
	int src;
	int dst;
	int pass_units;
	int weight_int;
	int weight_dec;
	struct list_head list;
} Passenger;

struct thread_parameter elevator;
struct list_head list;
struct list_head elev;
bool stop;


/*************************************************************************/


/* my_start_elevator() sets the elevator's state to IDLE, as it
 * is no longer OFFLINE, and puts the elevator at floor 1, with
 * zero passengers on it or waiting on any floor
 */ 
extern int (*STUB_start_elevator)(void);
int my_start_elevator(void)
{
	// start_elevator implementation
	
	int i;

	if (elevator.Current_State != OFFLINE)
		return 1;
	else
	{
//	 	try to initialize elevator:

		if (mutex_lock_interruptible(&elevator.mutex) == 0)
		{
			elevator.Current_Floor = 1;
			elevator.Next_Floor = 1;
			elevator.Current_Load.pass_units = 0;
			elevator.Current_Load.weight_int = 0;
			elevator.Current_Load.weight_dec = 0;
			elevator.Current_State = IDLE;
								
			for (i = 0; i < 10; i++)
			{
				elevator.Waiting_Passengers[i] = 0;
				elevator.Total_Passengers[i] = 0;
			}
		}
		mutex_unlock(&elevator.mutex);

	   	if (elevator.Current_State != IDLE)
			return -1;
	}
	
	return 0;
}


/*************************************************************************/


/* load_elev() loads all qualifying passengers onto elevator
 * (must be on the same floor as the elevator and be able to fit)
 */
int load_elev(Passenger * p)
{
	struct list_head * temp;
	struct list_head * dummy;
	bool can_get_on = true;
	bool remove = false;

	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		list_for_each_safe(temp, dummy, &list)
		{
			p = list_entry(temp, Passenger, list);

			if (p->src != elevator.Current_Floor)
				can_get_on = false;
	
			if ((elevator.Current_Load.pass_units + p->pass_units) >
				MAX_PASSENGER_UNITS)
				can_get_on = false;

			if ((elevator.Current_Load.weight_int + p->weight_int) >
				MAX_WEIGHT_INT)
				can_get_on = false;

			if ((elevator.Current_Load.weight_int + p->weight_int) ==
				MAX_WEIGHT_INT &&
				(elevator.Current_Load.weight_dec == 5 ||
				p->weight_dec == 5))
				can_get_on = false;
		
			if (p->dst == elevator.Current_Floor)
				remove = true;
		}
	}
	mutex_unlock(&elevator.mutex);

	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		if (can_get_on && !remove)
		{
			if (elevator.Current_Load.pass_units == 0)
				INIT_LIST_HEAD(&elev);

			elevator.Current_Load.pass_units += p->pass_units;

			elevator.Current_Load.weight_int += p->weight_int;

			if (elevator.Current_Load.weight_dec == 5 &&
				p->weight_dec == 5)
			{
				elevator.Current_Load.weight_int++;
				elevator.Current_Load.weight_dec = 0;
			}
			else
			{
				elevator.Current_Load.weight_dec += p->weight_dec;
			}

			elevator.Waiting_Passengers[elevator.Current_Floor - 1]--;
			
			list_move_tail(temp, &elev);
		}
		else if (remove)
		{
			elevator.Waiting_Passengers[elevator.Current_Floor - 1]--;
			list_del(temp);
			kfree(p);
		}
	}
	mutex_unlock(&elevator.mutex);

	return 0;
}


/* unload_elev() removes a passenger from the elevator as long
 * they are on their destination floor (removing them from elev)
 */ 
int unload_elev(Passenger * p)
{
	// declare some temporary pointers
	struct list_head * temp;
	struct list_head * dummy;

	// use this since you need to change the pointers
	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		list_for_each_safe(temp, dummy, &elev)
		{
			p = list_entry(temp, Passenger, list);
	
			if (p->dst == elevator.Current_Floor)
			{
				elevator.Current_Load.pass_units -= p->pass_units;

				elevator.Current_Load.weight_int -= p->weight_int;

				if (elevator.Current_Load.weight_dec == 0 &&
					p->weight_dec == 5)
				{
					elevator.Current_Load.weight_int--;
					elevator.Current_Load.weight_dec = 5;
				}
				else
				{
					elevator.Current_Load.weight_dec -=
					p->weight_dec;
				}
	
				elevator.Total_Passengers[p->src - 1]++;
	
				list_del(temp);	// init ver also reinits list
				kfree(p);		// remember to free allocated data
			}
		}
	}
	mutex_unlock(&elevator.mutex);

	return 0;
}


/*************************************************************************/


/* this function will be called when needing to find the next
 * closest floor to go to that is up. returns -1 if there
 * is none, otherwise it will return the floor number as an int */
int find_next_floor_up(int current_floor)
{
	struct list_head * temp;
    Passenger * p;

    int next_floor = -1;
    int closest_floor = 11; //set this initially 

	if (elevator.Current_Load.pass_units > 0)
	{
		list_for_each(temp, &elev)
   	 	{
    		p = list_entry(temp, Passenger, list);
				
			// for each passenger, if their dest is greater
			// than current floor
			// (i.e. they're going up), and if their dest is less
			// than the current closest_floor
			if (p->dst > current_floor && p->dst < closest_floor)
			{
				//make this our next_floor
				next_floor = p->dst;
				//update closest_floor to this particular passenger's 
				closest_floor = p->dst;
			}
		}	
	
		list_for_each(temp, &list)
	   	{
    	  	p = list_entry(temp, Passenger, list);
   
		    if (p->src > current_floor && p->src <= closest_floor &&
				p->dst > current_floor)
    	    {
        		next_floor = p->src;
 	          	closest_floor = p->src;
 			}   	
		}
	}

	return next_floor;
}


/* function for determining next floor going down.
 * operates the exact same way as find_next_floor_up
 * returns -1 if no passenger needs to go down
 */
int find_next_floor_down(int current_floor)
{
	struct list_head * temp;
    Passenger * p;

    int next_floor = -1;
    int closest_floor = 0;

	if (elevator.Current_Load.pass_units > 0)
	{	
	    list_for_each(temp, &elev)
		{
    		p = list_entry(temp, Passenger, list);
    	
			if (p->dst < current_floor && p->dst > closest_floor)
    		{
        		next_floor = p->dst;
            	closest_floor = p->dst;
 		   	}
		}

		list_for_each(temp, &list)
	   	{
      		p = list_entry(temp, Passenger, list);
    
			if (p->src < current_floor && p->src >= closest_floor &&
				p->dst < current_floor)
    		{
        		next_floor = p->src;
            	closest_floor = p->src;
 		   	}	
		}
	}

	return next_floor;
}


/*************************************************************************/


extern int (*STUB_issue_request)(int, int, int);
int my_issue_request(int p_type, int start_floor, int dest_floor)
{
	// issue_request implementation

	Passenger * p = NULL;
	struct list_head * temp;
	struct list_head * dummy;

	if (elevator.Current_State == IDLE)
		INIT_LIST_HEAD(&list);

	p = kmalloc(sizeof(Passenger), __GFP_RECLAIM);

	printk(KERN_NOTICE "MY_ISSUE_REQUEST FUNCTION ENTERED\n");

	if (p_type < 1 || p_type > 4 ||
	    start_floor < 1 || start_floor > 10 ||
	    dest_floor < 1 || dest_floor > 10)
	{
		return 1;
	}

	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		p->src = start_floor;
		p->dst = dest_floor;
		p->pass_units = 0;
		p->weight_int = 0;
		p->weight_dec = 0;

		switch (p_type)
		{
			case 1:
			{
				p->pass_units = 1;
				p->weight_int = 1;
				p->weight_dec = 0;
				break;
			}
		
			case 2:
			{
				p->pass_units = 1;
				p->weight_int = 0;
				p->weight_dec = 5;
				break;
			}
	
			case 3:
			{
				p->pass_units = 2;
				p->weight_int = 2;
				p->weight_dec = 0;
				break;
			}
			
			case 4:
			{
				p->pass_units = 2;
				p->weight_int = 3;
				p->weight_dec = 0;
				break;
			}
		}
	}
	mutex_unlock(&elevator.mutex);

	if (!stop)
	{
		if (mutex_lock_interruptible(&elevator.mutex) == 0)
		{
			list_add_tail(&p->list, &list);
			elevator.Waiting_Passengers[p->src - 1]++;
		}
		mutex_unlock(&elevator.mutex);

		if (elevator.Current_State == IDLE)
		{
			if (mutex_lock_interruptible(&elevator.mutex) == 0)
			{
				if (p->src == elevator.Current_Floor)
				{
					elevator.Current_State = LOADING;
					elevator.Next_Floor = p->dst;
				}
			}
			mutex_unlock(&elevator.mutex);

			if (mutex_lock_interruptible(&elevator.mutex) == 0)
			{
				if (elevator.Current_Floor != p->src)
					elevator.Next_Floor = p->src;
			
				if (elevator.Next_Floor > elevator.Current_Floor)
					elevator.Current_State = UP;
				else if (elevator.Next_Floor < elevator.Current_Floor)
					elevator.Current_State = DOWN;
			}
			mutex_unlock(&elevator.mutex);
		}
		else if (elevator.Current_State == UP)
		{
			if (elevator.Current_Load.pass_units == 0)
			{
				if (mutex_lock_interruptible(&elevator.mutex) == 0)
				{
					list_for_each_safe(temp, dummy, &list)
					{
						p = list_entry(temp, Passenger, list);
						elevator.Next_Floor = p->src;
						break;
					}		

					list_for_each_safe(temp, dummy, &list)
					{
						p = list_entry(temp, Passenger, list);

						if (p->src > elevator.Current_Floor &&
							p->src <= elevator.Next_Floor &&
							p->dst > elevator.Current_Floor)
    	    			{
							elevator.Next_Floor = p->src;
						}
					}
				}		
				mutex_unlock(&elevator.mutex);
			}
		}
		else if (elevator.Current_State == DOWN)
		{
			if (mutex_lock_interruptible(&elevator.mutex) == 0)
			{
				list_for_each_safe(temp, dummy, &list)
				{
					p = list_entry(temp, Passenger, list);
							
					if (p->src < elevator.Current_Floor &&
						p->src >= elevator.Next_Floor &&
						p->dst < elevator.Current_Floor)
					{
						elevator.Next_Floor = p->src;
					}
				}
			}
			mutex_unlock(&elevator.mutex);		
		}
	}

	return 0;
}


/*************************************************************************/


/* my_stop_elevator() defines the stop_elevator() system call;
 * it sets the elevator state to OFFLINE, but if there are still
 * passengers on the elevator, it takes them to their respective
 * dest_floors first
 */ 
extern int (*STUB_stop_elevator)(void);
int my_stop_elevator(void)
{
	// stop_elevator implementation
	
/*
	deactivates elevator, elevator will
	process no more new requests, but will
	offload all current passengers
*/

	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		stop = true;
	}
	mutex_unlock(&elevator.mutex);

	while (elevator.Current_Load.pass_units != 0){}
	
	if (mutex_lock_interruptible(&elevator.mutex) == 0)
	{
		elevator.Current_State = OFFLINE;
		elevator.Current_Floor = 0;
		elevator.Next_Floor = 0;
	}
	mutex_unlock(&elevator.mutex);

	return 0;
}


/*************************************************************************/


/* the elevator_service() function is the main thread of operation for
 * the simulated elevator; it runs while the module is inserted, and
 * currently does not perfectly attend to passengers, but the code which
 * sets elevator.Next_Floor appears to have nothing wrong with it
 */
int elevator_service(void * data)
{
	struct thread_parameter * parm = data;
	Passenger * p = NULL;
	struct list_head * temp;
	struct list_head * dummy;
	
	int i;
	bool waiting = false;

	printk(KERN_NOTICE "ELEVATOR_SERVICE FUNCTION ENTERED\n");

	while (!kthread_should_stop())
	{
		if (parm->Current_State != OFFLINE && parm->Current_State != IDLE)
		{
			if (parm->Current_State == LOADING)
			{
				ssleep(1);

				// if there are passengers on elevator, call unload_elev
				if (parm->Current_Load.pass_units > 0)
					unload_elev(p);
			
				if (!stop)
				{
					// if stop_elevator hasn't been called, call load_elev
					load_elev(p);
				}
				else
				{
					// if stop_elevator has been called, delete
					// all waiting passengers from list
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						list_for_each_safe(temp, dummy, &list)
						{
							p = list_entry(temp, Passenger, list);
							list_del(temp);
							kfree(p);
						}
					}
					mutex_unlock(&elevator.mutex);
				}
	
				if (mutex_lock_interruptible(&parm->mutex) == 0)
				{
					for (i = 0; i < 10; i++)
					{
						if (parm->Waiting_Passengers[i] > 0)
							waiting = true;			
					}
				}
				mutex_unlock(&elevator.mutex);
	
				// if there are no passengers on elevator
				// and no passengers waiting on any floor
				if (parm->Current_Load.pass_units == 0 && !waiting)
				{
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{		
						parm->Current_State = IDLE;
					}
					mutex_unlock(&elevator.mutex);
				}
				// if there are passengers on the elevator
				// and no passengers waiting on any floor
				else if (parm->Current_Load.pass_units > 0 && !waiting)
				{
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						list_for_each_safe(temp, dummy, &elev)
						{
							p = list_entry(temp, Passenger, list);

							parm->Next_Floor = p->dst;
							break;
						}	
					}
					mutex_unlock(&elevator.mutex);
						
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						if (parm->Next_Floor > parm->Current_Floor)
						{
							if (find_next_floor_up(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_up(
								parm->Current_Floor);
							}

							parm->Current_State = UP;
						}
						else
						{
							if (find_next_floor_down(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_down(
								parm->Current_Floor);
							}
								
							parm->Current_State = DOWN;
						}
					}
					mutex_unlock(&elevator.mutex);	
				}
				// if there are no passengers on the elevator
				// and at least one passenger is waiting on a floor
				else if (parm->Current_Load.pass_units == 0 && waiting)
				{
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						list_for_each_safe(temp, dummy, &list)
						{
							p = list_entry(temp, Passenger, list);
							parm->Next_Floor = p->src;

							break;
						}
					}
					mutex_unlock(&elevator.mutex);

					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						if (parm->Next_Floor > parm->Current_Floor)
						{
							if (find_next_floor_up(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_up(
								parm->Current_Floor);
							}

							parm->Current_State = UP;
						}
						else
						{
							if (find_next_floor_down(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_down(
								parm->Current_Floor);
							}
	
							parm->Current_State = DOWN;
						}
					}
					mutex_unlock(&elevator.mutex);
				}
				// if there is at least one passenger on the elevator
				// and at least one passenger waiting on a floor
				else
				{
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						list_for_each_safe(temp, dummy, &elev)
						{
							p = list_entry(temp, Passenger, list);
							parm->Next_Floor = p->dst;

							break;
						}
					}
					mutex_unlock(&elevator.mutex);

					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						if (parm->Next_Floor > parm->Current_Floor)
						{
							if (find_next_floor_up(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_up(
								parm->Current_Floor);
							}
					
							parm->Current_State = UP;
						}
						else
						{
							if (find_next_floor_down(
								parm->Current_Floor) > 0)
							{
								parm->Next_Floor =
								find_next_floor_down(
								parm->Current_Floor);
							}

							parm->Current_State = DOWN;
						}
					}
					mutex_unlock(&elevator.mutex);		
				}
			}
			else if (parm->Current_State == UP)
			{
				while (parm->Current_Floor != parm->Next_Floor)
				{
					ssleep(2);
					
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						parm->Current_Floor++;
					}
					mutex_unlock(&elevator.mutex);
				}

				if (mutex_lock_interruptible(&parm->mutex) == 0)
				{
					parm->Current_State = LOADING;	
				}
				mutex_unlock(&elevator.mutex);
			}

			else // (parm->Current_State == DOWN)
			{
				while (parm->Current_Floor != parm->Next_Floor)
				{
					ssleep(2);
					
					if (mutex_lock_interruptible(&parm->mutex) == 0)
					{
						parm->Current_Floor--;
					}
					mutex_unlock(&elevator.mutex);
				}

				if (mutex_lock_interruptible(&parm->mutex) == 0)
				{
					parm->Current_State = LOADING;	
				}
				mutex_unlock(&elevator.mutex);	
			}
		}
	}
		
	return 0;
}


/* thread_init_parameter() calls mutex_init and kthread_run,
 * which allow for mutual exclusion and start a running
 * kernel thread (which will be used for the mutual exclusion
 * as well as the elevator_service() function), respectively
 */
void thread_init_parameter(struct thread_parameter * parm)
{
	parm->Current_State = OFFLINE;

	mutex_init(&parm->mutex);

	parm->kthread = kthread_run(elevator_service, parm,
				    "elevator in service");	
}

/*************************************************************************/


/* elevator_proc_open() modifies the message variable so that it
 * contains the necessary information to be printed to the
 * /proc/elevator entry 
 */
int elevator_proc_open(struct inode *sp_inode, struct file *sp_file)
{
	char * buf = kmalloc (sizeof(char) * 100, __GFP_RECLAIM);	
	int i;

	if (buf == NULL)
	{
		printk(KERN_WARNING "print_time");
		return -ENOMEM;
	}

	printk(KERN_INFO "proc called open\n");
	printk(KERN_NOTICE "PROC_OPEN FUNCTION ENTERED\n");
	
	read_p = 1;
	message = kmalloc(sizeof(char) * ENTRY_SIZE,
			 __GFP_RECLAIM | __GFP_IO | __GFP_FS);

	if (message == NULL)
	{
		printk(KERN_WARNING "time_proc_open");
		return -ENOMEM;
	}

	strcpy(message, "");
	switch (elevator.Current_State)
	{
		case 0:
		{
			sprintf(buf, "State: OFFLINE\n");
			break;
		}
		
		case 1:
		{
			sprintf(buf, "State: IDLE\n");
			break;
		}

		case 2:
		{
			sprintf(buf, "State: LOADING\n");
			break;
		}
	
		case 3:
		{
			sprintf(buf, "State: UP\n");
			break;
		}
	
		case 4:
		{
			sprintf(buf, "State: DOWN\n");
			break;
		}
	
		strcat(message, buf);	
	
		sprintf(buf, "Current floor: %d\n", elevator.Current_Floor);
		strcat(message, buf);
	}

	sprintf(buf, "Next floor: %d\n", elevator.Next_Floor);
	strcat(message, buf);

	if (elevator.Current_Load.weight_int == 0 &&
		elevator.Current_Load.weight_dec == 0)
	{
		sprintf(buf,
		"Current load: %d passenger units, 0 weight units\n\n",
		elevator.Current_Load.pass_units);
	}		
	else
	{
		sprintf(buf,
		"Current load: %d passenger units, %d.%d weight units\n\n",
		elevator.Current_Load.pass_units,
		elevator.Current_Load.weight_int,
		elevator.Current_Load.weight_dec);
	}

	strcat(message, buf);

	for (i = 0; i < 10; i++)
	{
		sprintf(buf,
		"Floor %d: %d passengers waiting, %d passengers serviced\n",
		i + 1,
		elevator.Waiting_Passengers[i], elevator.Total_Passengers[i]);
		strcat(message, buf);
	}

	return 0;
}


/* elevator_proc_read() copies the contents of message
 * to the /proc/elevator entry 
 */
ssize_t elevator_proc_read(struct file *sp_file, char __user *buf,
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


/* elevator_proc_release() frees the data that was in the message
 * variable and releases the /proc/elevator entry 
 */ 
int elevator_proc_release(struct inode *sp_inode, struct file *sp_file)
{
	printk(KERN_NOTICE "proc called release\n");
	kfree(message);
	return 0;
}


/*************************************************************************/


/* elevator_init() maps the system call stubs to their respective
 * definition functions, creates the /proc/elevator file, sets
 * fops.open, fops.read, and fops.release, and calls
 * thread_init_parameter() to start the kthread which will be used
 * for the elevator_service() function and mutual exclusion
 */
static int elevator_init(void)
{
	// all initialization code

	stop = false;
	STUB_start_elevator = my_start_elevator;
	STUB_issue_request = my_issue_request;
	STUB_stop_elevator = my_stop_elevator;

	printk(KERN_NOTICE "/proc/%s create\n", ENTRY_NAME);

	fops.open = elevator_proc_open;
	fops.read = elevator_proc_read;
	fops.release = elevator_proc_release;
		
	if (!proc_create(ENTRY_NAME, PERMS, NULL, &fops))
	{
		printk(KERN_WARNING "proc create\n");
		remove_proc_entry(ENTRY_NAME, NULL);
		return -ENOMEM;
	}

	thread_init_parameter(&elevator);
	if (IS_ERR(elevator.kthread))
	{
		printk(KERN_WARNING "error spawning thread");
		remove_proc_entry(ENTRY_NAME, NULL);
		return PTR_ERR(elevator.kthread);
	}

	return 0;
}
module_init(elevator_init);


/* elevator_exit() stops the kthread, removes the /proc/elevator
 * entry, maps the system call stubs to the NULL pointer, and
 * calls mutex_destroy()
 */ 
static void elevator_exit(void)
{
	// all clean up code
	
	STUB_start_elevator = NULL;
	STUB_issue_request = NULL;
	STUB_stop_elevator = NULL;

	kthread_stop(elevator.kthread);
	remove_proc_entry(ENTRY_NAME, NULL);
	mutex_destroy(&elevator.mutex);
	printk(KERN_NOTICE "Removing /proc/%s\n", ENTRY_NAME);
}
module_exit(elevator_exit);
