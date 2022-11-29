#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120                                                                      // milliseconds
#define window_size 10                                                                  // define window size

int next_seqno = 0;
int send_base = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

int window[window_size];                                                                // create a window with the ID of every packet being sent
tcp_packet *window_packets[window_size];                                                // list to store the pointers where the packets are stored
int lens[window_size];
int stopTimer;                                                                          // if the receiver side recives an ACK, then stop the timer
int end_loop = 0;

pthread_mutex_t lock;                                                                   // lock to only allow exclusive access for sender or receiver to the window at a given moment

struct args_send_packet                                                                 // simple struct to be passed as the argument in the thread created when calling the function "send_packet"
{
    FILE *file;
};


struct args_rec_ack {};                                                                 /* simple struct to be passed as the argument in the thread created when calling the function "receive_ack"
                                                                                        it is currently empty, but we kept it in case we needed to pass more ack attributes in part 2 of the project */

void resend_packets(int sig)                                                            /* this function is only called when a timeout has occurred due to the receiver not sending an ACK for a certain packet-in-order,
                                                                                        that packet is then resent to the receiver in this function. The timeout is handled by the signal handler in init_timer(), which 
                                                                                        is responsible for pasisng the sig integer through this resend_packets function. */
{
    VLOG(INFO, "> Timeout happened: Resend packets");
    if (sig == SIGALRM)                                                                 // if we reach timeout
    {
        for (int i=0;i<window_size;i++) {                                               // iterate through all packets in current window that have not yet been ACKed

            if (window[i] == -1 || window_packets[i] == NULL)                           // if current packet was actually ACKed before, or if the dat avalue is non-existent (NULL), stop resending immediately 
                break;
            
            sndpkt = window_packets[i];                                                 /* packet object to be sent is set to current element in the window and then sent to receiver                                          
                                                                                        fetch the packet from the list of pointers containing the packets */
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                error("sendto");
        }
    }
}

void start_timer()                                                                      // function to start timer whenever it resets to track packets that have not been successfully ACKed
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()                                                                       // function to stop the timer when the expected ACK arrives successfully
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void init_timer(int delay, void (*sig_handler)(int))                                    // Initialize timer
{
    signal(SIGALRM, resend_packets);                                                    // sig_handler: signal handler function for re-sending unACKed packets
    timer.it_interval.tv_sec = delay / 1000;                                            // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;                                               // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

void *send_packet (void *arguments)                                                     // core function of this program, where a while loop keeps running trying to always send packets if possible
{
    struct args_send_packet * args = (struct args_send_packet *) arguments;             // store the values of the argument attributes for use in the function

    int len;                                                                            // length of the packet to be read and sent to receiver
    
    char buffer[DATA_SIZE];                                                             // buffer array to store the data contents from file and send to receiver in a packet
    
    FILE *fp = args->file;                                                              // file to be sent to receiver

    int location = -1;                                                                  // sets the position for the first "free" spot in the window

    send_base = 0;                                                                      // initialize the base
    next_seqno = 0;                                                                     // initialize the next sequence number

    init_timer(RETRY, resend_packets);                                                  // initialize the timer

    while (1) 
    {
        if (window[window_size - 1] == -1) {                                            /* if the last element in the window is -1 it means that the window is not full, so send a new package
                                                                                        once this is entered, the window array will changed so the lock must be locked to prevent 
                                                                                        receiver from making any changes to array and causing inconsistent results */
            pthread_mutex_lock(&lock);
            len = fread(buffer, 1, DATA_SIZE, fp);                                      // read a number of DATA_SIZE bytes from the file

            for (int i=0; i<window_size; i++) {                                         // get the position of the window in which the next paacket ID will be located
                
                if (window[i] == -1 && len != 0) 
                {
                    window[i] = next_seqno;                                             // when that position is found, set the packet ID to the next_seqno, since it will be the ID of the packet being sent
                    lens[i] = len;                                                      // same thing to the length of the packet
                    VLOG (DEBUG, "> Send packet %d to %s", next_seqno,inet_ntoa(serveraddr.sin_addr));
                    location = i;                                                       // get a location for the packet in the list to then add the packet to the window_packet
                    break;
                }
            }
            
            if (len <= 0)                                                               // if the length of bytes read from file is 0, it means we have reached the end of file
            {
                pthread_mutex_unlock(&lock);                                            // we no longer need the lock, so unlock so the receiver can edit the window arrays if needed
                
                while (window[0] != -1) {}                                              // waiting for the window to be empty before sending the last, terminating packet
                sndpkt = make_packet(0);                                                // make a packet of length zero to indicate end of file

                start_timer();                                                          // start timer in case the packet does not reach the receiver
                window[0] = 0;                                                          // add the element to the window, to be able to resend in case of timeout
                lens[0] = 0;                                                            // same thing for the length
                window_packets[0] = sndpkt;                                             // same thing for the packet
                send_base = window[0];                                                  // change the send base to the first element

                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);  // send the terminating packet to receiver to indicate EOF
                while(window[0] != -1) {}                                               // waiting to receive the specific ACK for the terminating packet 0 before ending the whole loop
                usleep(100);                                                            // wait for a moment before finally printing that the file has ended and terminating the program
                VLOG(INFO, "> End-of-file has been reached!");
                stop_timer();                                                           // stop the timer we just started earlier to make sure terminating packet actually reached receiver
                end_loop = 1;                                                           // let the program end when it reaches EOF
                return NULL;
            }
            
            send_base = window[0];                                                      // the send base will always be the first element in the window
            sndpkt = make_packet(len);                                                  // create packet with corresponding number of byte sread from file
            memcpy(sndpkt->data, buffer, len);                                          // copy data contents from buffer to packet object

            sndpkt->hdr.seqno = next_seqno;                                             // set the packet seq number to the next seq number as per TCP Protocol
            next_seqno += len;                                                          // the next sequence number is increased by the size of the package sent

            window_packets[location] = sndpkt;                                          // add the packet to the list of packets

            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)   // send the packet object to receiver and catch errors in sending
            {
                error("sendto");
            }

            
            if (next_seqno - len == 0)                                                  // start the timer if the sent packet is the first                                             
            {   
                start_timer();
            }
            
            else if (window_size > 1)                                                   // if the timer is stopped because the window was empty and we just added a packet, then start the timer again
            {
                if (window[1] == -1)                                                    
                {
                    start_timer();
                }
            }
            
            pthread_mutex_unlock(&lock);                                                // after all changes to the window arrays, we should now unlock the lock to allow the receiver to make necessary changes when needed
        }
        usleep(100);
    }
    
    end_loop = 1;                                                                       // if this loop is left, then all packets have been sent, including the terminating packet 0, so the program will jump to the end
    return NULL;
}

void *receive_ack (void *arguments)                                                     // important function to handle ACKs being received from the receiver
{
    int ack = -1;                                                                       // initialize ack local var to -1, and the buffer containing the data contents 
    char buffer[DATA_SIZE]; 

    while (1) 
    {   
                                                                                        // handle receiving error from receiver     
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)     // receive packet from the reeiver containing the ACk
        {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;                                                 // create a packet with the data received by the receiver
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack = recvpkt->hdr.ackno;                                                       // assign the acknowledgment recived to the local variable containing the variable
        
        
        if (ack != -1)                                                                  // if the ack is -1, this packet needs to be resent and so the list will be updated accordingly
        {
            pthread_mutex_lock(&lock);                                                  // we need to lock because the window arrays will be amended
            for (int i=0; i<window_size; i++) 
            {
                int current = ack - lens[i];                                            // recognize current packet to print in VLOG
                if (window[i] == ack - lens[i] && ack >= send_base)                     // find the position of the window that contains the packet for the ACK received
                {
                    send_base = window[i];                                              // say the k position was found, then all of the k-1 positions are also ACK'ed by the ACK received, so let the base be the packet for which the ACK was just received
                    for (int j = i+1; j<window_size; j++) 
                    {
                        window[j-i-1] = window[j];                                      // move up all of the packets to let the i position be first
                        lens[j-i-1] = lens[j];
                        window_packets[j-i-1] = window_packets[j];                      // same thing for the list containing the pointers to the packets

                        window[j] = -1;                                                 // let the i number of elements trailing in the window be free
                        lens[j] = -1;
                        window_packets[j] = NULL;                                       // same for the list containing the list of pointers to the packets

                    }
                    VLOG(DEBUG, "> Received successful ACK for packet %d", current);    // indicate packet ACK has been successfully received

                    if (window[0] == -1)                                                // if the first element has been acknowledged, list is empty, so stop timer until further notice
                    {
                        stop_timer(); 
                    }
                    break;                                                              // only do the above actions once when the packet for which the ACK was received is found, then break out of outer for loop
                }
            }
            
            pthread_mutex_unlock(&lock);                                                // no longer need to lock the window arrays after breaking outside the for loop
        }
        
        if (end_loop == 1)                                                              // keep running until the condition is 1 (from the function "send_packet"), where you break after
        {
            break;
        }  
        ack = -1;
        usleep(100);
    }
    return NULL;
}


int main (int argc, char **argv)
{
    int portno;
    char *hostname;
    FILE *fp;

    if (argc != 4) {                                                                    // check command line arguments
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    
    fp = fopen(argv[3], "r");                                                           // open file in read mode
    if (fp == NULL) {
        error(argv[3]);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);                                            // socket: create the socket
    if (sockfd < 0) 
        error("ERROR opening socket");

    bzero((char *) &serveraddr, sizeof(serveraddr));                                    // initialize server server details
    serverlen = sizeof(serveraddr);

    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {                               // covert host into network byte order
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    serveraddr.sin_family = AF_INET;                                                    // build the server's Internet address
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);                                                  // Stop and wait protocol

    pthread_t threads[2];                                                               // create two threads, one for sending the data and one for receving ACKs
    struct args_send_packet arguments_send;                                             // create a structure with the data to send to the function when the thread is created
    struct args_rec_ack arguments_receive;

    for (int i=0; i<window_size; i++)                                                   // set every element in the window to -1 to show that it is empty
    { 
        window[i] = -1;
        lens[i] = -1;
    }

    if (pthread_mutex_init(&lock, NULL) != 0) {                                         // try to initialize the lock used for exclusively changing the window arrows in send_packet and receive_ack, if failed exit the program
        printf("mutex init failed\n");
        return 1;
    }
    arguments_send.file = fp;

    if (pthread_create(&threads[0], NULL, &send_packet, (void *) &arguments_send) != 0){    // create the thread for sending packets by passing the arguments struct to send_packets 
        printf("Error creating the thread to send the packets\n");
    }
    
    if (pthread_create(&threads[1], NULL, &receive_ack, (void *) &arguments_receive) != 0) {  // create the thread for handling ACKs by passing the arguments struct to receive_ack
        printf("Error creating the thread to receive ACKs\n");
    }
    
    while(end_loop == 0){}                                                              // the condition by which the whole program runs (through send_packet)
    pthread_join(threads[0],NULL);                                                      //join the thread of packet send
    
    pthread_detach(threads[1]);                                                         // join the receive_ack thread

    pthread_mutex_destroy(&lock);                                                       // finally, destroy the lock we initialized earlier for amending window arrays
    VLOG(INFO, "> Terminating program...");

    return 0;
}