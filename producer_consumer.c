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

static int uuid = 0;
static int buffSize = 0;
static int prod = 0;
static int cons = 0;
module_param(uuid, int, 0);
module_param(buffSize, int, 0);
module_param(prod, int, 0);
module_param(cons, int, 0);

struct buff_node
{

    struct task_struct *fetched_task;
    struct buff_node *next;
    struct buff_node *prev;
    int index;
    int serial_no;
};

static struct buff_node *head = NULL;
static struct buff_node *tail = NULL;

static uint64_t total_elapsed_nanosecs = 0;

// need to keep track of each thread so you can stop each one from executing when you want to exit
static struct task_struct **consumer_threads;
static struct task_struct *producer_thread;

// init semaphores here
static struct semaphore buff_mutex;
static struct semaphore full;
static struct semaphore empty;
static struct semaphore total_time_mutex;

static int should_stop;

static int tasks_so_far = 1;

static int producer(void *data)
{
    struct task_struct *task; // where the fetched process is stored
    printk(KERN_INFO "TESING 1");
    for_each_process(task)
    {
        if(task)
        printk(KERN_INFO "fetch task succesfully? fetched uid:%d, needed uid %d", task->cred->uid.val, uuid);
        else
        printk(KERN_INFO "fetched task is null");
        if (task->cred->uid.val == uuid) // need to check if the process fetched is one that our user owns
        {

            if (down_interruptible(&empty)) // acquire empty; checks if any open places left in buffer
            {
                printk(KERN_INFO "leaving producer for_each_process");
                break; // is only evaluated when a signal is received from down_interruptible
            }
            if(should_stop){
                break;
            }
            printk(KERN_INFO "made it past first producer semaphore");
            if (down_interruptible(&buff_mutex)) // acquire buffer
            {
                break; // is only evaluated when a signal is received from down_interruptible
            }
            // insert at tail, take from tail
            if (head != NULL)
            {
                printk(KERN_INFO "start head not null");
                struct buff_node *curr_tail = tail; // put process task_struct in buffer
                struct buff_node *new_buff_node = kmalloc(sizeof(struct buff_node), GFP_KERNEL);
                tail->next = new_buff_node;
                tail = new_buff_node;
                tail->next = NULL;
                tail->prev = curr_tail;
                tail->index = curr_tail->index + 1;
                tail->serial_no = tasks_so_far;
                tail->fetched_task = task;
                tasks_so_far++; // dont need a semaphore for this since only one will be accessing their critical section at a time
                printk(KERN_INFO "end head not null");
            }
            else
            { // head should already be allocated statically
            printk(KERN_INFO "start head IS null");
                struct buff_node *new_buff_node = kmalloc(sizeof(struct buff_node), GFP_KERNEL);
                head = new_buff_node;
                head->fetched_task = task;
                head->next = NULL;
                head->prev = NULL;
                head->index = 0;
                head->serial_no = tasks_so_far;
                tasks_so_far++;
                tail = head;
                printk(KERN_INFO "end head IS null");
            }
            printk(KERN_INFO "%s Produced Item#-%d at buffer index: %d for PID:%d", current->comm, tail->serial_no, tail->index, task_pid_nr(task));
            // write details to kernel log, example print statement in the exit_function

            up(&buff_mutex); // release lock
            up(&full);       // decrease full amt (by signaling its semaphore) by 1
        }
    }
    printk(KERN_INFO "TESING");
    return 0;
}

static int consumer(void *data)
{
    printk(KERN_INFO "Reaches consumer thread");
    while (!kthread_should_stop())
    {
        printk(KERN_INFO "in consumer loop");
        if (down_interruptible(&full)) // waits to acquire full; checks if anything is currently in buffer
        {
            printk(KERN_INFO "kthread stopped worked for consumer");
            break; // is only evaluated when a signal is received from down_interruptinble
        }
        if(should_stop){
            break;
        }
        printk(KERN_INFO "past full semaphore");
        if (down_interruptible(&buff_mutex)) // acquire buffer
        {
            break; // is only evaluated when a signal is received from down_interruptible
        }
        printk(KERN_INFO "in consumer made it past semaphores");
        struct buff_node *temp = tail; // remove an instance of task_struct from buffer
        struct buff_node *new_tail = tail->prev;
        if (new_tail != NULL)
        { // shouldnt ever need this since we check this condition with a semaphore already
            new_tail->next = NULL;
            tail = new_tail;
            //kfree(temp);
        }
        else
        {
            head = NULL;
            tail = head;
            //kfree(temp);
        }
        up(&buff_mutex); // release buff lock
        up(&empty);      // signal empty to make empty + 1 since we just consumed a process from buffer
        uint64_t nanosecs_elapsed = ktime_get_ns() - temp->fetched_task->start_time;
        uint64_t secs_elapsed = nanosecs_elapsed * (1, 000, 000, 000);
        uint64_t hours_elapsed = secs_elapsed / 3600;
        uint64_t minutes_elapsed = (secs_elapsed % 3600) / 60;
        uint64_t secs_elapsed_remaining = secs_elapsed - hours_elapsed * 3600 - minutes_elapsed * 60;
        printk(KERN_INFO "%s Consumed Item#-%d on buffer index:%d PID:%d Elapsed Time- %d:%d:%d", current->comm, temp->serial_no, temp->index, task_pid_nr(temp->fetched_task), hours_elapsed, minutes_elapsed, secs_elapsed_remaining); // operate on task_struct data here

        if (down_interruptible(&total_time_mutex)) // get a lock for total_elpased_nanosecs
        {
            break; // is only evaluated when a signal is received from down_interruptible
        }
        total_elapsed_nanosecs += nanosecs_elapsed; // add elpased time to total_elapsed_time here

        up(&total_time_mutex); // release it
    }
    printk(KERN_INFO "LEAVING CONSUMER THREAD?");
    return 0;
}

int init_func(void)
{
    printk(KERN_INFO "TESING S");
    sema_init(&buff_mutex, 1);
    sema_init(&full, 0); //spots filled
    sema_init(&empty, buffSize); //spots left
    sema_init(&total_time_mutex, 1);
    should_stop = 0;
    printk(KERN_INFO "TESING S1");
    int k = 0;
    for (k = 0; k < prod; k++)
    {
        producer_thread = kthread_run(producer, NULL, "Producer-1");
    }
    if (producer_thread == NULL)
    {
        printk(KERN_INFO "PRODUCER IS NULL");
    }

    if(cons > 0){
    consumer_threads = kmalloc(cons * sizeof(struct task_struct), GFP_KERNEL);

    int i = 0;
    for (i = 0; i < cons; i++)
    {
        consumer_threads[i] = kthread_run(consumer, NULL, "Consumer-%d", i);
    }
    if (consumer_threads[0] == NULL)
    {
        printk(KERN_INFO "CONSUMER IS NULL");
    }
    }
    printk(KERN_INFO "TESING S2");
    return 0;
}

void exit_func(void)
{
    printk(KERN_INFO "reached exit func");
    should_stop = 1;
    if (producer_thread != NULL)
    {
        printk(KERN_INFO "inside exit producer deallocation");
        up(&empty); 
        kthread_stop(producer_thread);
        //i think having kfree here originally created a race condition
    }
    printk(KERN_INFO "released producer thread");
    if (consumer_threads != NULL)
    {
        printk(KERN_INFO "inside exit consumer deallocation");
        int f;
        for(f = 0; f<cons; f++){
            up(&full);
        }
        int e;
        for (e = 0; e < cons; e++)
        {
            if(consumer_threads[e]){
                kthread_stop(consumer_threads[e]);
            }
        }
        printk(KERN_INFO "stopped consumer threads");
    }
    printk(KERN_INFO "released consumer threads");
    // logic for implmenting nanoseconds to HH:MM:SS here, and fill in the rest below
    uint64_t secs_elapsed = total_elapsed_nanosecs * (1, 000, 000, 000);
    uint64_t hours_elapsed = secs_elapsed / 3600;
    uint64_t minutes_elapsed = (secs_elapsed % 3600) / 60;
    uint64_t secs_elapsed_remaining = secs_elapsed - hours_elapsed * 3600 - minutes_elapsed * 60;
    printk(KERN_INFO "The total elapsed time of all processes for uuid %d is %d:%d:%d", uuid, hours_elapsed, minutes_elapsed, secs_elapsed_remaining);
    //kfree(producer_thread);
    //producer_thread = NULL;
    //kfree(consumer_threads);
    //consumer_threads == NULL;
}

module_init(init_func);
module_exit(exit_func);
MODULE_LICENSE("GPL");
