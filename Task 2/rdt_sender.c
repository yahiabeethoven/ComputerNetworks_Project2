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
#include <math.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120                                                                      // milliseconds
#define MAX_WINDOW 256                                                                  // define window size
#define SIZE_ACK 128
#define MAX(a,b) (((a)>(b))?(a):(b))
// ================ 
// VARIABLES DEFINED FOR TASK 2
float cwnd = 1;                                                                         // create a float variable to store the cwnd. It needs to tbe a float because in congestion avoidance the cwnd is increased by 1/cwnd
int ssthresh = 0;                                                                       // create the slow-start threshold, which will be set to 64 when the program starts
int stage = 0;                                                                          // allows the program to know which stage of the process it's in: (0) Slow-start, (1) Congestion avoidance, (2) Fast retransmit
int loc_buff_pkt = -1;
int ret_three_ack;
                                                                            
tcp_packet *buff_packets[MAX_WINDOW];                                                   /* These variable is created solely to be adjusted to our coding style. When the value of cwnd is decreased there may be some 
                                                                                        packets in the sliding window with value > cwnd that are "lost" in a way, so we decided to move them to another list so the sender can fetch
                                                                                        from this list instead of from the file, and when these list is empty it can stat fetching from the file again */
tcp_packet *temp_pkt;
double estimated_rtt = -1;                                                              // variable to calculate the timeout interval for the timer
double timeout_interval = 3000;                                                         // value at which to set the timer for the timeout timer
double sample_rtt = 0;                                                                  // RTT calculated for every successfuly received ACK
double dev_rtt = 20;                                                                    // calculates how much the sample RTT deviates from the estimated RTT
double alpha = 0.125;                                                                   // variables defined to calculate the estimated and deviated RTT
double beta = 0.25;
struct timeval time_list[MAX_WINDOW];                                                   // list in which to store all of the time sent for the packets
struct timeval current_time;
struct timeval graph_time;
struct timeval buff_time_list[MAX_WINDOW];
struct timeval temp_time;
int resend_ack = 0;
int ack_list[3] = {-1,-2,-3};
void graphCwnd();                                                                       // declare function early on to ease throughout
FILE *cwndFile;
// ================ 
int next_seqno = 0;
int send_base = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

tcp_packet *window_packets[MAX_WINDOW];                                                 // list to store the pointers where the packets are stored
int stopTimer;                                                                          // if the receiver side recives an ACK, then stop the timer
int end_loop = 0;
int finisher = 0;

pthread_mutex_t lock;                                                                   // lock to only allow exclusive access for sender or receiver to the window at a given moment

struct args_send_packet                                                                 // simple struct to be passed as the argument in the thread created when calling the function "send_packet"
{
    FILE *file;
};

struct args_rec_ack {};                                                                 /* simple struct to be passed as the argument in the thread created when calling the function "receive_ack"
                                                                                        it is currently empty, but we kept it in case we needed to pass more ack attributes in part 2 of the project */

void start_timer()                                                                      // function to start timer whenever it resets to track packets that have not been successfully ACKed
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()                                                                       // function to stop the timer when the expected ACK arrives successfully
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void resend_packets(int sig)                                                            /* this function is only called when a timeout has occurred due to the receiver not sending an ACK for a certain packet-in-order,
                                                                                        that packet is then resent to the receiver in this function. The timeout is handled by the signal handler in init_timer(), which 
                                                                                        is responsible for pasisng the sig integer through this resend_packets function. */
{
    if (window_packets[0] != NULL)
    {
        VLOG(DEBUG, "> Timeout happened: Resend packet %d", window_packets[0]->hdr.seqno);
        if (sig == SIGALRM)                                                                 // if we reach timeout
        {
            sndpkt = window_packets[0];                                                     /* packet object to be sent is set to current element in the window and then sent to receiver                                          
                                                                                            fetch the packet from the list of pointers containing the packets */
            resend_ack = 1;

            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                error("sendto");
            start_timer();
            
            // if the program is in slow-start, append the contents of buffer temporarily into window and empty buffer, then copy all the new 
            // contents of window back into buffer excluding the first packet so it is sent later to receiver in the expected order
            if (stage == 0)                                                                 // if a timeout occured during the slow-start phase, then let cwnd = 1 and ssthresh=MAX(cwnd/2, 2)
            {   
                // find the first null index in window packets
                for (int j=1;j<MAX_WINDOW;j++) 
                {
                    if (window_packets[j] == NULL)
                    {
                        loc_buff_pkt = j;
                        break;
                    }
                }
                // temporarily append buffer contents to window array, and empty buffer
                for (int x = 0; x < MAX_WINDOW; x++) {
                    if (buff_packets[x] != NULL) {
                        temp_pkt = buff_packets[x];
                        temp_time = buff_time_list[x];
                        window_packets[loc_buff_pkt] = temp_pkt;
                        time_list[loc_buff_pkt] = temp_time;
                        loc_buff_pkt++;
                        buff_packets[x] = NULL;
                    }
                    else {
                        break;
                    }
                }
                // copy new window contents back to buffer with the exception of the first element to be resent
                for (int x = 1; x < MAX_WINDOW; x++) {
                    if (window_packets[x] != NULL) {
                        temp_pkt = window_packets[x];
                        temp_time = time_list[x];
                        buff_packets[x-1] = temp_pkt;
                        buff_time_list[x-1] = temp_time;
                        window_packets[x] = NULL;
                    }
                    else {
                        break;
                    }
                }
                // set cwnd to 1 and ssthreshold to max(cwnd/2,2) as per tcp protocol, send the new cwnd value to the csv file
                cwnd = 1;
                ssthresh = MAX(cwnd/2, 2);
                graphCwnd();
            }
        }
    }
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

// function to handle the case of 3 duplicate ACKs, basiclaly implements fast retransmit phase of tcp
int check_acks()
{
    // if the latest 3 acks received are all identical (ie. 3 duplicate acks have been received)
    if (ack_list[0] == ack_list[1] && ack_list[1] == ack_list[2])                       // three duplicate acks
    {
        // assume that the packet with sequence number == ack number was lost, even if a timeout has not occurred yet
        stop_timer();
        // iterate through the window packets array to find the corrsponding packet for which 3 acks were received to retransmit it immediately
        for (int i=0;i<floor(cwnd);i++) 
        {
            // if we reach null or end of list then the packet actually does not exist ( there is nothing to retransmit)
            if (window_packets[i] == NULL)
            {
                break;
            }
            // if we do find that packet, we resend it immediately
            if (ack_list[0] == window_packets[i]->hdr.seqno) 
            {
                sndpkt = window_packets[i];
                resend_ack = 1;
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                    error("sendto");
                
                // we follow here the same logic of updating the window, buffer, and time lists as of the above resend packets function
                // look through the window packets for the first empty slot, and save that index
                for (int j=1;j<MAX_WINDOW;j++) 
                {
                    if (window_packets[j] == NULL)
                    {
                        loc_buff_pkt = j;
                        break;
                    }
                }
                // temporarily append buffer contents to window array, and empty buffer
                for (int x = 0; x < MAX_WINDOW; x++) {
                    if (buff_packets[x] != NULL) {
                        temp_pkt = buff_packets[x];
                        temp_time = buff_time_list[x];
                        window_packets[loc_buff_pkt] = temp_pkt;
                        time_list[loc_buff_pkt] = temp_time;
                        loc_buff_pkt++;
                        buff_packets[x] = NULL;
                    }
                    else {
                        break;
                    }
                }
                // copy new window contents back to buffer with the exception of the first element to be resent
                for (int x = 1; x < MAX_WINDOW; x++) {
                    if (window_packets[x] != NULL) {
                        temp_pkt = window_packets[x];
                        temp_time = time_list[x];
                        buff_packets[x-1] = temp_pkt;
                        buff_time_list[x-1] = temp_time;
                        window_packets[x] = NULL;
                    }
                    else {
                        break;
                    }
                }
                // set ssthreshold and cwnd as above values and send new cwnd value to the csv file, 
                // but also change the phase back to slow-start again
                ssthresh = MAX(cwnd/2, 2);
                cwnd = 1; 
                stage = 0; 
                graphCwnd(); 
                start_timer();
                break;
            }
        }
        // reset the values of the last 3 acks received since we have updated the window, buffer, and time lists and restarted the process
        ack_list[0] = -1;
        ack_list[1] = -2;
        ack_list[2] = -3;
        // return 1 to indicate to the receive ack function that there was indeed a triple duplicate set of acks received
        return (1);
    }
    // otherwise, simply return 0
    else
    {
        return (0);
    }
}

void *send_packet (void *arguments)                                                     // core function of this program, where a while loop keeps running trying to always send packets if possible
{
    struct args_send_packet * args = (struct args_send_packet *) arguments;             // store the values of the argument attributes for use in the function

    int len;  
    int tempSB;  
    
    char buffer[DATA_SIZE];                                                             // buffer array to store the data contents from file and send to receiver in a packet
    
    FILE *fp = args->file;                                                              // file to be sent to receiver

    send_base = 0;                                                                      // initialize the base
    next_seqno = 0;                                                                     // initialize the next sequence number

    init_timer(RETRY, resend_packets);                                                  // initialize the timer

    int floor_int = -1;
    // int example = 0;

    while (1) 
    {
        pthread_mutex_lock(&lock);
        floor_int = floor(cwnd);
        if (window_packets[floor_int - 1] == NULL) {                                    /* if the last element in the congestion window is -1 it means that the window is not full, so send a new package
                                                                                        once this is entered, the window array will changed so the lock must be locked to prevent 
                                                                                        receiver from making any changes to array and causing inconsistent results */
            
            
            if (buff_packets[0] == NULL)                                                // if there is any packet in the buff of packets made when the cwnd was recuced, then take the data from there instead
                len = fread(buffer, 1, DATA_SIZE, fp);                                  // read a number of DATA_SIZE bytes from the file
            else {
                len = buff_packets[0]->hdr.data_size;
                if (len > 0) {
                    next_seqno = buff_packets[0]->hdr.seqno;
                    memcpy(buffer,buff_packets[0]->data, len);
                    for (int i=0; i<MAX_WINDOW-1; i++)                                      // move all of the items up when one is sent
                    {
                        buff_packets[i] = buff_packets[i+1];
                        buff_time_list[i] = buff_time_list[i+1];
                    }   
                    buff_packets[MAX_WINDOW-1] = NULL;
                }
            }
            if (len <= 0)                                                               // if the length of bytes read from file is 0, it means we have reached the end of file
            {
                pthread_mutex_unlock(&lock);                                            // we no longer need the lock, so unlock so the receiver can edit the window arrays if needed
                while (window_packets[0] != NULL && buff_packets[0] != NULL) {}                                    // waiting for the window to be empty before sending the last, terminating packet
                sndpkt = make_packet(0);                                                // make a packet of length zero to indicate end of file

                start_timer();                                                          // start timer in case the packet does not reach the receiver
                sndpkt->hdr.seqno = 0;                                                  // add the element to the window, to be able to resend in case of timeout
                sndpkt->hdr.data_size = 0;                                              // same thing for the length
                window_packets[0] = sndpkt;                                             // same thing for the packet
                gettimeofday(&current_time,0);
                time_list[0] = current_time;
                
                send_base = window_packets[0]->hdr.seqno; 
                
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);  // send the terminating packet to receiver to indicate EOF

                while(window_packets[0] != NULL) {}                                    // waiting to receive the specific ACK for the terminating packet 0 before ending the whole loop
                usleep(100);                                                            // wait for a moment before finally printing that the file has ended and terminating the program
                VLOG(INFO, "> End-of-file has been reached!");
                stop_timer();                                                           // stop the timer we just started earlier to make sure terminating packet actually reached receiver
                end_loop = 1;                                                           // let the program end when it reaches EOF
                return NULL;
            }
            sndpkt = make_packet(len);                                                  // create packet with corresponding number of byte sread from file
            memcpy(sndpkt->data, buffer, len);                                          // copy data contents from buffer to packet object
            sndpkt->hdr.seqno = next_seqno;                                             // set the packet seq number to the next seq number as per TCP Protocol
            resend_ack = 0;
            // find the next seqno packet and save the time of finding it in the time lost for future refernce when sending the packets in order
            for (int i=0; i<floor(cwnd); i++) {                                                // get the position of the window in which the next paacket ID will be located
                if (window_packets[i] == NULL && len != 0) 
                {
                    window_packets[i] = sndpkt;
                    gettimeofday(&current_time,0);
                    time_list[i] = current_time;
                    VLOG (DEBUG, "> Send packet %d to %s", next_seqno,inet_ntoa(serveraddr.sin_addr));
                    break;
                }
            }
            send_base = window_packets[0]->hdr.seqno;

            next_seqno += len;                                                          // the next sequence number is increased by the size of the package sent

            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)   // send the packet object to receiver and catch errors in sending
            {
                error("sendto");
            }
            
            if (next_seqno - len == 0)                                                  // start the timer if the sent packet is the first                                             
            {   
                start_timer();
            }
            
            if (window_packets[1] == NULL)                                                    
            {
                start_timer();
            }    
        }
        // unlock the lock for other functions to be able to change window_packets and the buffer freely as we no longer need that here
        pthread_mutex_unlock(&lock); 
        usleep(100);
    }
    
    end_loop = 1;                                                                       // if this loop is left, then all packets have been sent, including the terminating packet 0, so the program will jump to the end
    return NULL;
}

void *receive_ack (void *arguments)                                                     // important function to handle ACKs being received from the receiver
{
    
    int ack_temp = -1;                                                                  // initialize ack local var to -1, and the buffer containing the data contents 
    char buffer[DATA_SIZE]; 

    while (1) 
    {     
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)     // receive packet from the reeiver containing the ACk
        {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;                                                 // create a packet with the data received by the receiver
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack_temp = recvpkt->hdr.ackno; 

        pthread_mutex_lock(&lock);                                                      // we need to lock because the window arrays will be amended
        // ===========================
        // NEW FOR TASK 2
        // if the program is already in congestion avoidance stage, increment the cwnd by 1/itself as per tcp protocol
        if (stage == 1)
        {
            cwnd += (1/cwnd);
        }
        // otherwise, increment the cwnd by 1 and then check if it reached the ss threshold, to determine whether to move to congestion avoidance or not
        if (stage == 0)                                                                 // if a successful ACK was received while in the slow-start stage, then increase cwnd by 1
        {
            cwnd += 1;                                                                  // increase cwnd by 1
            if (cwnd == ssthresh)  {                                                      // if the cwnd is equal to the slow start threshold, then enter congestion control
                stage += 1;  
            }                                                            // increase the stage by 1, which means reaching congestion control
        }
        // send the new cwnd value to the csv file for future graphing of the congestion window
        graphCwnd();
        
        // update the values of the 3 most recently received acks, to check if there were 3 duplicate acks or not
        ack_list[0] = ack_list[1];
        ack_list[1] = ack_list[2];
        ack_list[2] = ack_temp;
        
        // if there was indeed 3 duplicate acks, the function has taken care of the fast retransmit and has updated window packets,
        // the buffer, and the timestamps. It has also updated cwnd and ssthreshold, so we just need to unlock teh lock and not run 
        // the following redundant lines of code
        ret_three_ack = check_acks();
        if (ret_three_ack == 1)
        {
            pthread_mutex_unlock(&lock);
            continue;
        }

        // ===========================
        for (int i=0; i<floor(cwnd); i++)
        {
            // if this slot in the window packets list is vacant
            if (window_packets[i] != NULL)
            {
                // check if the received packet is valid (ie. it is equal to or greater than the send base), or if it is actually the terminating 0 packet
                if ((window_packets[i]->hdr.seqno == ack_temp - window_packets[i]->hdr.data_size && ack_temp > send_base && send_base <= window_packets[i]->hdr.seqno) || (window_packets[0]->hdr.seqno == 0 && window_packets[0]->hdr.data_size == 0 && ack_temp == 0))    // find the position of the window that contains the packet for the ACK received                
                {
                    // RECALCULATE TIMEOUT INTERVAL by updating the sample and estimated round-trip time if this packet is not a resent one
                    if (resend_ack == 0)
                    {
                        gettimeofday(&current_time,0);
                        sample_rtt = (current_time.tv_sec - time_list[i].tv_sec) * 1000.0f + (current_time.tv_usec - time_list[i].tv_usec) / 1000.0f;
                        if (estimated_rtt < 0)
                            estimated_rtt = sample_rtt;
                        else
                        {
                            estimated_rtt = (1-alpha)*estimated_rtt + alpha * sample_rtt;
                            dev_rtt = (1-beta) * dev_rtt + beta * fabs(sample_rtt - estimated_rtt);
                        }
                        timeout_interval = estimated_rtt + 4 * dev_rtt;
                    }
                    // update the send base, the point from which window sliding is determined
                    send_base = window_packets[i]->hdr.seqno+window_packets[i]->hdr.data_size;  

                    // At this point, the received ack is the correct one, and the timer should be rest
                    VLOG(DEBUG, "> Received successful ACK for packet %d", ack_temp - window_packets[i]->hdr.data_size);    // indicate packet ACK has been successfully received
                    stop_timer();

                    // go through the window packets to remove that packet because it has been successfully ACKed to prevent redundant resending
                    for (int j = i+1; j<floor(cwnd); j++) 
                    {
                        if (window_packets[j] != NULL)
                        {
                            window_packets[j-i-1] = window_packets[j];                  // move up all of the packets to let the i position be first
                            time_list[j-i-1] = time_list[j];
                            window_packets[j] = NULL;                                   // move up all of the packets to let the i position be first
                        }
                        else 
                        {
                            window_packets[j-i-1] = NULL;
                            break;
                        }
                    }
                    
                    // if the window packets list is empty or contains invalid negative values, stop the iteration immediately
                    if (window_packets[0] == NULL || window_packets[0] < 0)                                      // if the first element has been acknowledged, list is empty, so stop timer until further notice
                    {
                        stop_timer(); 
                        break;
                    }
                    // otherwise, start the timer again and call on resent packets in case of a timeout
                    init_timer(timeout_interval, resend_packets);
                }
            }
        }
 
        pthread_mutex_unlock(&lock);                                                    // no longer need to lock the window arrays after breaking outside the for loop
    
        if (end_loop == 1)                                                              // keep running until the condition is 1 (from the function "send_packet"), where you break after
        {
            break;
        }  
        ack_temp = -1;
        usleep(100);
    }
    return NULL;
}

// simple function that notes the current time and current cwnd value and writes it to a CSV file in the format of the
// timestamp,cwnd value
void graphCwnd() { 
    struct timeval innerTime;
    if (cwndFile) {
        gettimeofday(&innerTime,0);
    }
    else {
        VLOG(INFO,"could not write into CWND file");
        exit(1);
    }
    fprintf(cwndFile,"%ld,%f\n",innerTime.tv_sec,cwnd);
}

int main (int argc, char **argv)
{
    int portno;
    char *hostname;
    FILE *fp;

    gettimeofday(&graph_time,0);
    // initialize the cwnd csv file that will be updated every time cwnd changes
    cwndFile = fopen("CWND.csv","w");
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

    init_timer(timeout_interval, resend_packets);                                                  // Stop and wait protocol

    pthread_t threads[2];                                                               // create two threads, one for sending the data and one for receving ACKs
    struct args_send_packet arguments_send;                                             // create a structure with the data to send to the function when the thread is created
    struct args_rec_ack arguments_receive;

    for (int i=0; i<MAX_WINDOW; i++)                                                    // set every element in the window to -1 to show that it is empty
    { 
        window_packets[i] = NULL;
        // time_list[i] = NULL;
    }

    if (pthread_mutex_init(&lock, NULL) != 0) {                                         // try to initialize the lock used for exclusively changing the window arrows in send_packet and receive_ack, if failed exit the program
        VLOG(INFO,"mutex init failed");
        return 1;
    }
    arguments_send.file = fp;

    // ==================== 
    // NEW FOR TASK 2
    ssthresh = 64;                                                                      // initialize the slow-start threshold to 64
    stage = 0;                                                                          // start the program in the slow-start stage
    cwnd = 1;                                                                           // let the window size be 1 as the program has just started
    // ==================== 

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
    // close the cwnd csv file because all of the cwnd updates have been printed and teh program is terminating
    fclose(cwndFile);
    return 0;
}