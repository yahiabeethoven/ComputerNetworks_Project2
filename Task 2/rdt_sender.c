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
void graphCwnd();
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
    // printf("START TIMER with time %f\n", timeout_interval);
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()                                                                       // function to stop the timer when the expected ACK arrives successfully
{
    //printf("STOP TIMER\n");
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
            if (stage == 0)                                                                 // if a timeout occured during the slow-start phase, then let cwnd = 1 and ssthresh=MAX(cwnd/2, 2)
            {   
                // VLOG(INFO, "Entered Resend Packets Stage 0 ");
                // printf("    > ssthresh: %d\n",ssthresh);
                for (int j=1;j<MAX_WINDOW;j++) 
                {
                    if (window_packets[j] == NULL)
                    {
                        loc_buff_pkt = j;
                        break;
                    }
                }
                
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

                cwnd = 1;
                ssthresh = MAX(cwnd/2, 2);
                graphCwnd();
            }
            else {
                // VLOG(INFO, "timeout occurred during congestion avoidance!!!");
            }
            // printf("\n(RE-SEND Packets)\n");
            // printf("buffer packets: ["); 
            // for (int j=0;j<MAX_WINDOW;j++) 
            //     {
            //         if (buff_packets[j] != NULL)
            //         {
            //             printf("%d, ",buff_packets[j]->hdr.seqno);
            //         }
            //     }
            // printf("]\n");
            // printf("window packets: [");
            //     for (int j=0;j<floor(cwnd);j++) 
            //     {
            //         if (window_packets[j] != NULL) {
            //             printf("%d, ",window_packets[j]->hdr.seqno);
            //         }
            //     } 
            // printf("]\n");  
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

int check_acks()
{
    if (ack_list[0] == ack_list[1] && ack_list[1] == ack_list[2])                       // three duplicate acks
    {
        stop_timer();
        // ack_list[0] = -1;
        // ack_list[1] = -2;
        // ack_list[2] = -3;
        for (int i=0;i<floor(cwnd);i++) 
        {
            if (window_packets[i] == NULL)
            {
                // VLOG(INFO,"IT WAS NOT FOUND IN THE WINDOW");
                for (int j=0;j<MAX_WINDOW;j++)
                {
                    if (buff_packets[j] != NULL) 
                    {
                        if (buff_packets[j]->hdr.seqno == ack_list[0])
                        {
                            // printf("IT WAS THERE!\n");
                        }
                    }
                }
                break;
            }
            if (ack_list[0] == window_packets[i]->hdr.seqno) 
            {
                //VLOG(DEBUG,"TRIPLE ACK FOUND! PACKET LOST: %d", window_packets[i]->hdr.seqno);
                sndpkt = window_packets[i];
                resend_ack = 1;
                // graphCwnd();
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                    error("sendto");
                
                
                for (int j=1;j<MAX_WINDOW;j++) 
                {
                    if (window_packets[j] == NULL)
                    {
                        loc_buff_pkt = j;
                        break;
                    }
                }
                
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
                ssthresh = MAX(cwnd/2, 2);
                cwnd = 1; 
                stage = 0; 
                graphCwnd(); 
                start_timer();
            
                break;
            }
        }
        ack_list[0] = -1;
        ack_list[1] = -2;
        ack_list[2] = -3;
        return (1);
    }
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
    // tcp_packet* tempFirst;                                                                        // length of the packet to be read and sent to receiver
    
    char buffer[DATA_SIZE];                                                             // buffer array to store the data contents from file and send to receiver in a packet
    
    FILE *fp = args->file;                                                              // file to be sent to receiver

    send_base = 0;                                                                      // initialize the base
    next_seqno = 0;                                                                     // initialize the next sequence number

    init_timer(RETRY, resend_packets);                                                  // initialize the timer

    int floor_int = -1;
    // int example = 0;

    while (1) 
    {
        // scanf("%d", &example);
        pthread_mutex_lock(&lock);
        floor_int = floor(cwnd);
        if (window_packets[floor_int - 1] == NULL) {                                    /* if the last element in the congestion window is -1 it means that the window is not full, so send a new package
                                                                                        once this is entered, the window array will changed so the lock must be locked to prevent 
                                                                                        receiver from making any changes to array and causing inconsistent results */
            
            
            // VLOG(INFO, "entered send packets");
            if (buff_packets[0] == NULL)                                                // if there is any packet in the buff of packets made when the cwnd was recuced, then take the data from there instead
                len = fread(buffer, 1, DATA_SIZE, fp);                                  // read a number of DATA_SIZE bytes from the file
            else {
                // printf("First buffer element is: %d\n",buff_packets[0]->hdr.seqno);
                len = buff_packets[0]->hdr.data_size;
                if (len > 0) {
                    next_seqno = buff_packets[0]->hdr.seqno;
                    memcpy(buffer,buff_packets[0]->data, len);
                    for (int i=0; i<MAX_WINDOW-1; i++)                                      // move all of the items up when one is sent
                    {
                        buff_packets[i] = buff_packets[i+1];
                        buff_time_list[i] = buff_time_list[i+1];
                    }   
                    // buff_time_list[MAX_WINDOW-1] = NULL;
                    buff_packets[MAX_WINDOW-1] = NULL;
                }
                else {
                    //VLOG(INFO, "packet is strangely empty");
                }
                // pthread_mutex_unlock(&lock);
                // usleep(200);
                // continue;
            }
            tempSB = send_base;
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
                //if (tempSB != send_base) {
                //    VLOG(DEBUG, "(Len == 0) SEND BASE CHANGED FROM %d TO %d",tempSB, send_base);
                //}                              // change the send base to the first element
                
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);  // send the terminating packet to receiver to indicate EOF
                // while(window_packets[0] != NULL) {
                //     VLOG(DEBUG, "window packets: %d",window_packets[0]);
                //     // VLOG(INFO,"Waiting for last ACK !!!!!!!!!");
                // } 
                while(window_packets[0] != NULL)
                {
                                    //while(finisher == 0) {
                    // VLOG(DEBUG, "window packets: %p",window_packets[0]);
                    // VLOG(INFO,"Waiting for last ACK !!!!!!!!!");
                }                                    // waiting to receive the specific ACK for the terminating packet 0 before ending the whole loop
                usleep(100);                                                            // wait for a moment before finally printing that the file has ended and terminating the program
                VLOG(INFO, "> End-of-file has been reached!");
                stop_timer();                                                           // stop the timer we just started earlier to make sure terminating packet actually reached receiver
                end_loop = 1;                                                           // let the program end when it reaches EOF
                return NULL;
            }
            // tempFirst = window_packets[0];
            sndpkt = make_packet(len);                                                  // create packet with corresponding number of byte sread from file
            memcpy(sndpkt->data, buffer, len);                                          // copy data contents from buffer to packet object
            sndpkt->hdr.seqno = next_seqno;                                             // set the packet seq number to the next seq number as per TCP Protocol
            resend_ack = 0;
            for (int i=0; i<floor(cwnd); i++) {                                                // get the position of the window in which the next paacket ID will be located
                if (window_packets[i] == NULL && len != 0) 
                {
                    if (i == 0) {
                        if (sndpkt) {
                            // VLOG(DEBUG, "Window[0] is: %d",sndpkt->hdr.seqno);
                        }
                    }
                    window_packets[i] = sndpkt;
                    gettimeofday(&current_time,0);
                    time_list[i] = current_time;
                    VLOG (DEBUG, "> Send packet %d to %s", next_seqno,inet_ntoa(serveraddr.sin_addr));
                    break;
                }
            }
            send_base = window_packets[0]->hdr.seqno;
            if (tempSB != send_base) {
                if (send_base < tempSB) {
                    // VLOG(DEBUG, "(Send Packet) SEND BASE DECREASED! <<<<<<<<< FROM %d TO %d", tempSB, send_base);
                }
                else {
                    // VLOG(DEBUG, "(Send Packet) SEND BASE CHANGED FROM %d TO %d",tempSB, send_base);
                }
            }
            
             
            
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

            // ==================================
            // PRINT THE WINDOW
            // printf("Window= [");
            // for (int i=0;i<floor_int-1;i++) 
            // {
            //     if (window_packets[i] == NULL)
            //         printf("-1,");
            //     else
            //     printf("%d,", window_packets[i]->hdr.seqno);
            // }
            // if (window_packets[floor_int-1] == NULL)
            //     printf("-1]\n");
            // else
            //     printf("%d]\n", window_packets[floor_int-1]->hdr.seqno);
            // ==================================
            // printf("\n(Send Packets)\n");
            // printf("buffer packets: ["); 
            // for (int j=0;j<MAX_WINDOW;j++) 
            //     {
            //         if (buff_packets[j] != NULL)
            //         {
            //             printf("%d, ",buff_packets[j]->hdr.seqno);
            //         }
            //     }
            // printf("]\n");
            // printf("window packets: [");
            //     for (int j=0;j<cwnd;j++) 
            //     {
            //         if (window_packets[j] != NULL) {
            //             printf("%d, ",window_packets[j]->hdr.seqno);
            //         }
            //     } 
            // printf("]\n");                                        // after all changes to the window arrays, we should now unlock the lock to allow the receiver to make necessary changes when needed
            
            
        }
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
    // int num_duplicate = 0;                                                              // counts the number of times a duplicate ACK has been received, so when it gets to three we do something about it

    // int example = 0;
    while (1) 
    {     
        // VLOG(INFO, "receiver ack while loop iteration");
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)     // receive packet from the reeiver containing the ACk
        {
            error("recvfrom");
        }
        // scanf("%d", &example);

        recvpkt = (tcp_packet *)buffer;                                                 // create a packet with the data received by the receiver
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack_temp = recvpkt->hdr.ackno; 
        // VLOG(DEBUG, "Received: %d, send base: %d",recvpkt->hdr.ackno, send_base);                                                 // assign the acknowledgment recived to the local variable containing the variable

        pthread_mutex_lock(&lock);                                                      // we need to lock because the window arrays will be amended
        // printf("received ACK with value %d\n", recvpkt->hdr.ackno);
        // ===========================
        // NEW FOR TASK 2
        // try putting this outside
        
        if (stage == 1)
        {
            // VLOG(INFO,"    > Stage 2: Congestion Avoidance");
            cwnd += (1/cwnd);
        }

        if (stage == 0)                                                                 // if a successful ACK was received while in the slow-start stage, then increase cwnd by 1
        {
            // VLOG(INFO,"    > Stage 1: Slow Start");
            cwnd += 1;                                                                  // increase cwnd by 1
            // if (cwnd == ssthresh)                                                       // if the cwnd is equal to the slow start threshold, then enter congestion control
            //     stage += 1; 
            if (cwnd == ssthresh)  {                                                      // if the cwnd is equal to the slow start threshold, then enter congestion control
                stage += 1;  
                // VLOG(INFO, "stage has been increased"); 
            }                                                            // increase the stage by 1, which means reaching congestion control
        }
        graphCwnd();
        
        // if (ack_temp == window_packets[0]->hdr.seqno)
        // if (ack_temp == send_base)                                                      // received a duplicate ACK
        // {
        //     // if (ack_temp - window_packets[0]->hdr.data_size == 0) {
        //     //     VLOG(INFO, "received zero ack from client!!!!!");
        //     //     window_packets[0] = NULL;
        //     //     stop_timer(); 
        //     //     break;
        //     // }
        //     VLOG(INFO,"    > Duplicate ACK");
        //     num_duplicate += 1;                                                         // increase the counter by 1
        //     if (num_duplicate == 3)                                                     // if it is a triple ACK, then a packet must be lost
        //     {
        //         VLOG(INFO,"    > Triple duplicate ACK");
        //         // FAST RETRANSMIT PHASE                                                // enter the fast retransmit stage
        //         stop_timer();
        //         for (int i=0;i<MAX_WINDOW;i++) 
        //         {
        //             if (buff_packets[i] == NULL)
        //             {
        //                 loc_buff_pkt = i;
        //                 break;
        //             }
        //         }
        //         for (int i=1;i<cwnd;i++) 
        //         {
        //             buff_packets[loc_buff_pkt] = window_packets[i];
        //             buff_time_list[loc_buff_pkt++] = time_list[i];
        //             window_packets[i] = NULL;
        //             // time_list[i] = NULL;
        //         }
        //                                                                     // go back to slow start phase
        //                                                               // change the counter back to 0 after the fast retransmit phase
        //         resend_three_ack();
        //         usleep(1000);
        //         num_duplicate = 0;
        //         // continue;
        //     }
            
        // }
        ack_list[0] = ack_list[1];
        ack_list[1] = ack_list[2];
        ack_list[2] = ack_temp;
        ret_three_ack = check_acks();
        if (ret_three_ack == 1)
        {
            // printf("\n(Three ACKs)\n");
            // printf("buffer packets: ["); 
            // for (int j=0;j<MAX_WINDOW;j++) 
            //     {
            //         if (buff_packets[j] != NULL)
            //         {
            //             printf("%d, ",buff_packets[j]->hdr.seqno);
            //         }
            //     }
            // printf("]\n");
            // printf("window packets: [");
            //     for (int j=0;j<floor(cwnd);j++) 
            //     {
            //         if (window_packets[j] != NULL) {
            //             printf("%d, ",window_packets[j]->hdr.seqno);
            //         }
            //     } 
            // printf("]\n");                                          // after all changes to the window arrays, we should now unlock the lock to allow the receiver to make necessary changes when needed
            pthread_mutex_unlock(&lock);
            continue;
        }
        // usleep(1000);

        // ===========================
        // printf("1\n");

        for (int i=0; i<floor(cwnd); i++)
        {
            if (window_packets[i] != NULL)
            {
                // VLOG(DEBUG, "window packet: %d",window_packets[i]->hdr.seqno);
                // printf("2\n");
                // printf("Por position %d, if %d == %d and %d >= %d\n", i, window_packets[i]->hdr.seqno, ack_temp - window_packets[i]->hdr.data_size, ack_temp, send_base);
                // printf("ACK received for seqno: %d\nSeqno: %d\nSend base: %d\n",ack_temp - window_packets[i]->hdr.data_size, window_packets[i]->hdr.ackno, send_base);
                if ((window_packets[i]->hdr.seqno == ack_temp - window_packets[i]->hdr.data_size && ack_temp > send_base && send_base <= window_packets[i]->hdr.seqno) || (window_packets[0]->hdr.seqno == 0 && window_packets[0]->hdr.data_size == 0 && ack_temp == 0))    // find the position of the window that contains the packet for the ACK received                
                // if (window_packets[i]->hdr.seqno == ack_temp - window_packets[i]->hdr.data_size && send_base <= window_packets[i]->hdr.seqno)    // find the position of the window that contains the packet for the ACK received
                // if (window_packets[i]->hdr.seqno == ack_temp - window_packets[i]->hdr.data_size && ack_temp > window_packets[0]->hdr.seqno && window_packets[0]->hdr.seqno <= window_packets[i]->hdr.seqno) 
                {
                    // RECALCULATE TIMEOUT INTERVAL
                    // printf("1\n");
                    if (resend_ack == 0)
                    {
                        gettimeofday(&current_time,0);
                        sample_rtt = (current_time.tv_sec - time_list[i].tv_sec) * 1000.0f + (current_time.tv_usec - time_list[i].tv_usec) / 1000.0f;
                        // VLOG(DEBUG,"Sample RTT: %f", sample_rtt);
                        if (estimated_rtt < 0)
                            estimated_rtt = sample_rtt;
                        else
                        {
                            estimated_rtt = (1-alpha)*estimated_rtt + alpha * sample_rtt;
                            dev_rtt = (1-beta) * dev_rtt + beta * fabs(sample_rtt - estimated_rtt);
                        }
                        timeout_interval = estimated_rtt + 4 * dev_rtt;
                    }
                    // printf("3\n");
                    // int tempSB = send_base;
                    send_base = window_packets[i]->hdr.seqno+window_packets[i]->hdr.data_size;  
                    // VLOG(DEBUG, "Value of window packet[i]: %d", window_packets[i]->hdr.seqno);

                                            // say the k position was found, then all of the k-1 positions are also ACK'ed by the ACK received, so let the base be the packet for which the ACK was just received
                    // printf("5\n");
                    VLOG(DEBUG, "> Received successful ACK for packet %d", ack_temp - window_packets[i]->hdr.data_size);    // indicate packet ACK has been successfully received
                    stop_timer();
                    // VLOG(INFO, "WIndow Packets: [");
                    // printf("Window Packets : [");
                    for (int j = i+1; j<floor(cwnd); j++) 
                    {
                        if (window_packets[j] != NULL)
                        {
                            // printf("%d, ",window_packets[j]->hdr.seqno);
                            // VLOG(DEBUG, "%d, ",window_packets[j]->hdr.seqno);
                            window_packets[j-i-1] = window_packets[j];                  // move up all of the packets to let the i position be first
                            time_list[j-i-1] = time_list[j];
                            window_packets[j] = NULL;                                   // move up all of the packets to let the i position be first
                            // time_list[j] = NULL;
                        }
                        else 
                        {
                            window_packets[j-i-1] = NULL;
                            //if (j-i-1 == 0) {
                            //    VLOG(INFO, "only first left ######");
                            //}
                            // stop_timer();
                            // time_list[j-i-1] = NULL;
                            break;
                        }
                        
                        // printf("7\n");
                    }
                    
                    if (window_packets[0] == NULL || window_packets[0] < 0)                                      // if the first element has been acknowledged, list is empty, so stop timer until further notice
                    {
                        // finisher = 1;
                        // send_base = window_packets[0]->hdr.seqno;
                        printf("Happens here!\n");
                        stop_timer(); 
                        break;
                    }
                    // if (window_packets[1] == NULL && window_packets[0] < 0 && buff_packets)
                    init_timer(timeout_interval, resend_packets);
                    // if (window_packets[0] != NULL)
                    //     printf("First packet: %d\n",window_packets[0]->hdr.seqno);
                    // printf("6\n");
                    
                    // printf("8\n");
                }
                // else if (ack_temp < send_base) {
                //     VLOG(DEBUG, "ACK less than expected ********");
                // }
                // else {
                //     VLOG(DEBUG, "Received weird ACK: %d",ack_temp);
                // }
            }
            // else if (buff_packets[i] != NULL)
            // {
            //     // printf("Checking?\n");
            //     if (buff_packets[i]->hdr.seqno == ack_temp - buff_packets[i]->hdr.data_size)    // if the ACK received is for an element in the buff of packets and not the normal window, then look also inside of it
            //     {
            //         // printf("4\n");
            //         for (int j=i; i<MAX_WINDOW-1; i++)
            //         {
            //             buff_time_list[j] = buff_time_list[j+1];
            //             buff_packets[j] = buff_packets[j+1];
            //         }
            //         buff_packets[MAX_WINDOW-1] = NULL;
            //         buff_time_list[MAX_WINDOW-1] = NULL;
            //     }
            // }

        }
        
        // printf("\n(Receive ACK)\n");
        // printf("buffer packets: ["); 
        // for (int j=0;j<MAX_WINDOW;j++) 
        //     {
        //         if (buff_packets[j] != NULL)
        //         {
        //             printf("%d, ",buff_packets[j]->hdr.seqno);
        //         }
        //     }
        // printf("]\n");
        // printf("window packets: [");
        //     for (int j=0;j<floor(cwnd);j++) 
        //     {
        //         if (window_packets[j] != NULL) {
        //             printf("%d, ",window_packets[j]->hdr.seqno);
        //         }
        //     } 
        // printf("]\n");  
        pthread_mutex_unlock(&lock);   
        // VLOG(INFO, "unlocked lock");    // indicate packet ACK has been successfully received
                                                 // no longer need to lock the window arrays after breaking outside the for loop
    
        if (end_loop == 1)                                                              // keep running until the condition is 1 (from the function "send_packet"), where you break after
        {
            break;
        }  
        ack_temp = -1;
        // VLOG(INFO, "ack set to -1");
        usleep(100);
    }
    return NULL;
}

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
    // VLOG(INFO,"printed to file: %f, %f, %d", timeToPrint,cwnd,ssthresh);
    // fclose(cwndFile);
    // VLOG(INFO, "closed cwnd file properly");

}

int main (int argc, char **argv)
{
    int portno;
    char *hostname;
    FILE *fp;

    gettimeofday(&graph_time,0);
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
    fclose(cwndFile);
    return 0;
}