# CSE-330-Project-2
Implementation of a synchronized producer and consumer with a bounded buffer. A producer kthread loops through every process currently running, and if the process is owned by the user specified by the uuid parameter passed to the program, adds it to the linked list of processes owned by the specified user. The linked list nodes track the task_struct of the process, its index in the linked list, and tracks it as ith added node with the serial_no variable. As this is happening, the consumer thread(s) are removing nodes from the list 

*So many commits cuz couldnt get shared folder to work in VM, and had to be in VM to test it, so this was my channel for code updates... rip
