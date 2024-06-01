# Driver code for FPGA developed to brute-force the Traveling Salesman Problem

https://uoy.atlassian.net/wiki/spaces/RTS/overview?homepageId=35684366

This repo contains my hurried implementation of FreeRTOS onto a Zybo Z-10 v1 FPGA.

The OS had four scheduled tasks:
* To collect input from the user
* To serialise and send said input to the problem server
* To run the hardware once a problem was received
* To serealise and send the computed solution to the problem server to check the validity of the answer.

Additionally, lwip was used to handle the received communications from the problem server. 
The handler had to be non-blocking and so was only responsible for copying data out of the packet buffer, clearing the buffer and displaying an output based on the data received. 
All further processing happened in one of the then unblocked tasks subsequently. 

This code could be improved as FreeRTOS does have proper ways of suspending and restarting tasks based on some generated registers, but due to time constraints that was not fully implemented at the time. 
I do think this was a good solution overall to the problem at hand!
