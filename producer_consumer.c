#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/slab.h>

static int uid = 0;
static int buff_size = 0;
static int p = 0;
static int c = 0;
module_param(uid, int, 0);
module_param(buff_size, int, 0);
module_param(p, int, 0);
module_param(c, int, 0);

struct buffer
{

    struct task_struct *head;
    int capacity;
};

static struct buffer *buff;

static int total_elapsed_nanosecs = 0;

// need to keep track of each thread so you can stop each one from executing when you want to exit
static struct task_struct **consumer_threads;
static struct task_struct *producer_thread;

// init semaphores here
static struct semaphore buff_mutex;
static struct semaphore full;
static struct semaphore empty;
static struct semaphore total_time_mutex;

static int producer(void *data)
{

    struct task_struct *task = NULL; // where the fetched process is stored
    for_each_process(task)
    {
        while (!kthread_should_stop())
        {
            // need to check if the process fetched is one that our user owns

            if (down_interruptible(&empty)) //acquire empty; checks if any open places left in buffer
            {
                break; //is only evaluated when a signal is received from down_interruptible
            }
            if (down_interruptible(&buff_mutex)) //acquire buffer
            {
                break; //is only evaluated when a signal is received from down_interruptible
            }

            // put process task_struct in buffer
            //write details to kernel log, example print statement in the exit_function

            up(&buff_mutex); // release lock
            up(&full); // decrease full amt (by signaling its semaphore) by 1
        }
    }
    return 0;
}

static int consumer(void *data)
{
    while (true)
    {
        while (!kthread_should_stop())
        {

            if (down_interruptible(&full)) //waits to acquire full; checks if anything is currently in buffer
            {
                break; //is only evaluated when a signal is received from down_interruptinble
            }
            if (down_interruptible(&buff_mutex)) //acquire buffer 
            {
                break; //is only evaluated when a signal is received from down_interruptible
            }

            // remove an instance of task_struct from buffer

            up(&buff_mutex);// release buff lock
            up(&empty);// signal empty to make empty + 1 since we just consumed a process from buffer

            // operate on task_struct data here

            if (down_interruptible(&total_time_mutex)) // get a lock for total_elpased_nanosecs
            {
                break; //is only evaluated when a signal is received from down_interruptible
            }
            // add elpased time to total_elapsed_time here

            up(&total_time_mutex); // release it
        }
    }
    return 0;
}

int init_func(void)
{
    buff->capacity = buff_size;
    
    sema_init(&buff_mutex, 1);
    sema_init(&full, 0);
    sema_init(&empty, buff_size);
    sema_init(&total_time_mutex, 1);

   int k = 0;
    for (k = 0; k < p; k++)
    {
	  producer_thread = kthread_run(producer, NULL, "Producer-1");
    }

    consumer_threads = kmalloc(c*sizeof(struct task_struct), GFP_KERNEL);
    int i = 0;
    for (i = 0; i < c; i++)
    {
        consumer_threads[i] = kthread_run(consumer, NULL, "Consumer-%d", i);
    }
    return 0;
}

void exit_func(void)
{

    kthread_stop(producer_thread);

    int e = 0;
    for (e = 0; e < c; e++)
    {
        kthread_stop(consumer_threads[e]);
        consumer_threads[e] == NULL;
    }
    // logic for implmenting nanoseconds to HH:MM:SS here, and fill in the rest below
   // printk(KERN_INFO "The total elapsed time of all processes for UID %d is %d%d:%d%d:%d%d", );
}

module_init(init_func);
module_exit(exit_func);
MODULE_LICENSE("GPL");
