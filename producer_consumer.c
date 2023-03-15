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

module_param(uid, int, 0);
module_param(buff_size, int, 0);
module_param(p, int, 0);
module_param(c, int, 0);

struct buffer
{

    struct task_struct *head;
    int capactity;
};

static struct buffer *buff;
buff->capacity = buff_size;

static int total_elapsed_nanosecs = 0;

// need to keep track of each thread so you can stop each one from executing when you want to exit
static struct task_struct *consumer_threads[c];
static struct task_struct *producer_thread;

// init semaphores here
static struct semaphore buff_mutex;
static struct semaphore full;
static struct semaphore empty;
static struct semaphore total_time_mutex;

static int producer(void)
{

    struct task_struct *current; // where the fetched process is stored
    for_each_process(current)
    {
        while (!kthread_should_stop(producer_thread))
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

            up(buff_mutex); // release lock
            up(full); // decrease full amt (by signaling its semaphore) by 1
        }
    }
}

static int consumer(void)
{
    while (true)
    {
        while (!kthread_should_stop(producer_thread))
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

            up(buff_mutex);// release buff lock
            up(empty);// signal empty to make empty + 1 since we just consumed a process from buffer

            // operate on task_struct data here

            if (down_interruptible(&total_time_mutex)) // get a lock for total_elpased_nanosecs
            {
                break; //is only evaluated when a signal is received from down_interruptible
            }
            // add elpased time to total_elapsed_time here

            up(total_time_mutex); // release it
        }
    }
}

int init_func(void)
{

    sema_init(&buff_mutex, 1)
    sema_init(&full, 0);
    sema_init(&empty, buff_size);
    sema_init(&total_time_mutex, 1);

    for (int k = 0; k < p; k++)
    {
        producer_thread = kthread_run(producer, NULL, "Producer-1");
    }

    for (int i = 0; i < c; i++)
    {
        consumer_thread[i] = kthread_run(consumer, NULL, "Consumer-%d", i);
    }
}

void exit_func(void)
{

    kthread_stop(producer_thread);

    for (int e = 0; int e < c; e++)
    {
        kthread_stop(consumer_threads[e]);
        consumer_threads[e] == NULL;
    }
    // logic for implmenting nanoseconds to HH:MM:SS here, and fill in the rest below
    printk(KERN_INFO "The total elapsed time of all processes for UID %d is %d%d:%d%d:%d%d", );
}

module_init(init_func);
module_exit(exit_func);