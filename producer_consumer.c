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

//the parameters we pass into our kernel module at the time it is mounted (insmod)
static int uuid = 0;
static int buffSize = 0;
static int prod = 0;
static int cons = 0;
module_param(uuid, int, 0);
module_param(buffSize, int, 0);
module_param(prod, int, 0);
module_param(cons, int, 0);

//our bounded buffer is implemented as a linked list since its size is decided at runtime
struct buff_node 
{
    
    struct task_struct *fetched_task; //the task we will be fetching with the for_each_process macro
    struct buff_node *next;
    struct buff_node *prev;
    int index; //the index of the node in the linked list 
    int serial_no; //the ith produced process (that belongs to the given uuid) total
};

static struct buff_node *head = NULL;
static struct buff_node *tail = NULL;

//each process has its elapsed time from the process' creation to the time it is consumed by a consumer thread; this is 
//the sum of all the processes we add to our buffer's elapsed times
static uint64_t total_elapsed_nanosecs = 0;

//consumer threads is an array of threads because we can have multiple potential consumer threads
static struct task_struct **consumer_threads;
static struct task_struct *producer_thread;

//controls access to the buffer so that only one thread at a time can access it
static struct semaphore buff_mutex; 
//tracks the number of spots filled in the buffer; used to tell the consumer thread if it can consume anything
static struct semaphore full; 
//tracks the number of spots empty in the buffer; used to tell the producer thread if it can add anything
static struct semaphore empty;
//controls access to the total_elapsed_time variable by multiple consumer threads; while technically not needed, as the
//consumer threads could just do this in their critical section when they are consuming a process from the buffer, ideally
//you want to release the lock on the buffer as quickly as possible so another thread can obtain it, since the buffer is likely
//the bottleneck for thr throughput of this program
static struct semaphore total_time_mutex;

//used when rmmod is called and we want the threads to stop execution after we signal their semaphores to reawaken them and stop
//them from being blocked; ran into weird behavior with kthread_stop and up being called right after each other 
//(though I'm not 100% sure kfree was still being called which also couldve been- and was definitely confirmed to cause 
//seg faults- the issue) so this is a cleaner way to stop the thread's execution
static int should_stop;

//keep track of the total number of processes we produce with our producer thread that have the uuid given as a parameter
static int tasks_so_far = 1;

static int producer(void *data)
{
    struct task_struct *task; // where the fetched process is stored
    
    for_each_process(task)
    {
        
        if (task->cred->uid.val == uuid) // need to check if the process fetched is one that our user owns
        {

            //checks if any open places left in buffer
            if (down_interruptible(&empty)) 
            {
                //this would only ever be entered if a user signal interrupts the asleep thread (if it is blocked
                //and waiting for a semphore); in this program it is never entered as we never send any user signal to the threads
                break; 
            }

            //in the case where there are no consumers the producer thread will be stuck here when it fills the buffer 
            //indefinitely; this persists until we call rmmod on which case we don't actually want to put anything more in the
            //buffer, we just want to end the producer thread. Since we must signal the producer thread in order to awaken it,
            //we must stop it here because otherwise it would keep trying to put things in the buffer because the way it keeps 
            //track of if the buffer is full is through the semaphore we just signaled 
            if(should_stop){
                break;
            }

            //acquire the buffer
            if (down_interruptible(&buff_mutex)) 
            {
                break; 
            }
            
            if (head != NULL)
            {
                struct buff_node *curr_tail = tail;
                struct buff_node *new_buff_node = kmalloc(sizeof(struct buff_node), GFP_KERNEL);

                //we insert in our linked list at the tail
                tail->next = new_buff_node;
                tail = new_buff_node;

                //initializing new node
                tail->next = NULL;
                tail->prev = curr_tail;
                tail->index = curr_tail->index + 1;
                tail->serial_no = tasks_so_far;
                tail->fetched_task = task;

                //dont need a semaphore for this since only one will be accessing their critical section at a time
                tasks_so_far++; 
            }
            else
            { 
                //for some reason head isn't allocated statically so we must allocate memory to it here
                struct buff_node *new_buff_node = kmalloc(sizeof(struct buff_node), GFP_KERNEL);
                head = new_buff_node;

                //initializing new node
                head->fetched_task = task;
                head->next = NULL;
                head->prev = NULL;
                head->index = 0;
                head->serial_no = tasks_so_far;

                tasks_so_far++;

                //only one node in the list so far
                tail = head;
            }

            printk(KERN_INFO "%s Produced Item#-%d at buffer index: %d for PID:%d\n", current->comm, tail->serial_no, tail->index, task_pid_nr(task));

            up(&buff_mutex); 
            up(&full); // decrease amount of available spots in buffer by 1
        }
    }
    return 0;
}

static int consumer(void *data)
{
    while (!kthread_should_stop())
    {
        //checks if anything in buffer for it to consume
        if (down_interruptible(&full)) 
        {
            break;
        }

        if(should_stop){ 
            break;
        }

        // acquire a lock on the buffer
        if (down_interruptible(&buff_mutex)) 
        {
            break; 
        }

        //store our node that contains the process we will consume so we can release the buffer and then calculate its elapsed time
        struct buff_node *temp = tail;

        //we consume processes from the tail of the linked list, so the linked list is really a stack
        struct buff_node *new_tail = tail->prev;
        
        kfree(tail);

        //if the node we are removing is not the last in the buffer
        if (new_tail != NULL)
        { 
            new_tail->next = NULL;
            tail = new_tail;
        }
        //if the node we are removing is the last in the buffer
        else
        {
            head = NULL;
            tail = head;
        }

        up(&buff_mutex);

        // signal empty to make empty + 1 since we just consumed a process from buffer 
        up(&empty);      

        //calculates the elapsed time from the fetched process' start of execution to the current (the time at this code section's 
        //execution) time
        unsigned long long int nanosecs_elapsed = 0;
        nanosecs_elapsed = ktime_get_ns() - temp->fetched_task->start_time;

        //we convert from nanoseconds elapsed to a HH:MM:SS format here
        unsigned long long int secs_elapsed = 0;
        secs_elapsed = nanosecs_elapsed/(1000000000);
         unsigned long long int hours_elapsed = 0;
        hours_elapsed = secs_elapsed / 3600;
         unsigned long long int minutes_elapsed = 0;
        hours_elapsed = (secs_elapsed % 3600) / 60;
         unsigned long long int secs_elapsed_remaining = 0;
        secs_elapsed_remaining = secs_elapsed - hours_elapsed * 3600 - minutes_elapsed * 60;

        printk(KERN_INFO "%s Consumed Item#-%d on buffer index:%d PID:%d Elapsed Time- %llu:%llu:%llu\n", current->comm, temp->serial_no, temp->index, task_pid_nr(temp->fetched_task), hours_elapsed, minutes_elapsed, secs_elapsed_remaining); 

        //add the fetched process' elapsed time to the total elpased time for all fetched processes
        if (down_interruptible(&total_time_mutex)) 
        {
            break; 
        }
        total_elapsed_nanosecs += nanosecs_elapsed;  

        up(&total_time_mutex); 
    }
    return 0;
}

int init_func(void)
{
    sema_init(&buff_mutex, 1); //used to control access to the buffer
    sema_init(&full, 0); //full tracks the number spots currently filled in the buffer
    sema_init(&empty, buffSize); //empty tracks the number of spots empty in the buffer
    sema_init(&total_time_mutex, 1); //use to control access to the total_elapsed_time var for multiple consumer threads
    
    should_stop = 0;
    
    int k = 0;
    for (k = 0; k < prod; k++)
    {
        producer_thread = kthread_run(producer, NULL, "Producer-1");
    }

    //buffer size is variable so needs to be established at runtime
    if(cons > 0){ 
    //consumer_threads = kmalloc(cons * sizeof(struct task_struct), GFP_KERNEL); 

    int i;
    for (i = 0; i < cons; i++)
    {
        consumer_threads[i] = kthread_run(consumer, NULL, "Consumer-%d", i);
    }
    
    }
    return 0;
}

void exit_func(void)
{
    should_stop = 1; //both producer and consumer threads use it to exit instead of using kthread_stop

    if (producer_thread != NULL)
    {
        up(&empty); //same logic i explained for the consumer thread but this is only used in the case of having no consumer threads

    }
    
    if (consumer_threads != NULL) //this was originally for when i thought we had to use kthread_stop and kfree to avoid seg faults 
    {

        //at the end of the process the consumers will have consumed everything and be asleep waiting for the semaphore
        //to reawaken them when available, but since the production thread will have ended, we must signal the semaphores
        //manually for each consumer thread in order for the threads to be released since they have completed all the work
        int f;
        for(f = 0; f<cons; f++){  
            up(&full);
        }
    }

    //while the calculating elapsed time could be its own seperate function, the message it prints to is different for the 
    //other implementation of this similar logic, so since its just 2x used, doing it seperately would just makes things more
    //complicated 
    unsigned long long int secs_elapsed = 0;
    secs_elapsed = total_elapsed_nanosecs /(1000000000);
    unsigned long long int hours_elapsed = 0;
    hours_elapsed = secs_elapsed / 3600;
    unsigned long long int minutes_elapsed = 0;
    minutes_elapsed = (secs_elapsed % 3600) / 60;
    unsigned long long int secs_elapsed_remaining = 0;
    secs_elapsed_remaining = secs_elapsed - hours_elapsed * 3600 - minutes_elapsed * 60;
    printk(KERN_INFO "The total elapsed time of all processes for uuid %d is %llu:%llu:%llu\n", uuid, hours_elapsed, minutes_elapsed, secs_elapsed_remaining);
    
    //apparently if a kthread exits its allocated function on its own, its cleaned up automatically by the kernel, so since
    //our functions exit on their own, using kthread_stop would just cause a seg fault beceuase by the time kthread_stop would
    //be called the thread has already finished

    //didnt end up using any thread stopping or freeing of memory. i do need to clear the buffnodes though
}

module_init(init_func);
module_exit(exit_func);
MODULE_LICENSE("GPL");
