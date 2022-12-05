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
                                                                            
tcp_packet *buff_packets[MAX_WINDOW];                                                   /* These variable is created solely to be adjusted to our coding style. When the value of cwnd is decreased there may be some 
                                                                                        packets in the sliding window with value > cwnd that are "lost" in a way, so we decided to move them to another list so the sender can fetch
                                                                                        from this list instead of from the file, and when these list is empty it can stat fetching from the file again */
double estimated_rtt = -1;                                                              // variable to calculate the timeout interval for the timer
double timeout_interval = 1000;                                                         // value at which to set the timer for the timeout timer
double sample_rtt = 0;                                                                  // RTT calculated for every successfuly received ACK
double dev_rtt = 20;                                                                    // calculates how much the sample RTT deviates from the estimated RTT
double alpha = 0.125;                                                                   // variables defined to calculate the estimated and deviated RTT
double beta = 0.25;
struct timeval time_list[MAX_WINDOW];                                                   // list in which to store all of the time sent for the packets
struct timeval current_time;
struct timeval buff_time_list[MAX_WINDOW];
int resend_ack = 0;
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
void graphCwnd();

pthread_mutex_t lock;                                                                   // lock to only allow exclusive access for sender or receiver to the window at a given moment

struct args_send_packet                                                                 // simple struct to be passed as the argument in the thread created when calling the function "send_packet"
{
    FILE *file;
};

struct args_rec_ack {};                                                                 /* simple struct to be passed as the argument in the thread created when calling the function "receive_ack"
                                                                                        it is currently empty, but we kept it in case we needed to pass more ack attributes in part 2 of the project */

void start_timer()                                                                      // function to start timer whenever it resets to track packets that have not been successfully ACKed
{
    printf("START TIMER with time %f\n", timeout_interval);
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()                                                                       // function to stop the timer when the expected ACK arrives successfully
{
    printf("STOP TIMER\n");
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void resend_packets(int sig)                                                            /* this function is only called when a timeout has occurred due to the receiver not sending an ACK for a certain packet-in-order,
                                                                                        that packet is then resent to the receiver in this function. The timeout is handled by the signal handler in init_timer(), which 
                                                                                        is responsible for pasisng the sig integer through this resend_packets function. */
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
            // printf("    > ssthresh: %d\n",ssthresh);
            for (int i=0;i<MAX_WINDOW;i++) 
            {
                if (buff_packets[i] == NULL)
                {
                    loc_buff_pkt = i;
                    break;
                }
            }
            for (int i=1;i<cwnd;i++) 
            {
                buff_packets[loc_buff_pkt] = window_packets[i];
                buff_time_list[loc_buff_pkt++] = time_list[i];
                window_packets[i] = NULL;
                // time_list[i] = NULL;
            }
            cwnd = 1;
            graphCwnd();
            ssthresh = MAX(cwnd/2, 2);
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

void resend_three_ack()                                                                 // resend the first packet in case a packet is lost. We decided to do another diffferetne 
{
    printf("seqno triple ACK: %d\n", window_packets[0]->hdr.seqno);
    sndpkt = window_packets[0];                                                         /* packet object to be sent is set to current element in the window and then sent to receiver                                          
                                                                                        fetch the packet from the list of pointers containing the packets */
    resend_ack = 1;
    if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        error("sendto");
    start_timer();
}

void *send_packet (void *arguments)                                                     // core function of this program, where a while loop keeps running trying to always send packets if possible
{
    struct args_send_packet * args = (struct args_send_packet *) arguments;             // store the values of the argument attributes for use in the function

    int len;                                                                            // length of the packet to be read and sent to receiver
    
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
        floor_int = floor(cwnd);
        if (window_packets[floor_int - 1] == NULL) {                                    /* if the last element in the congestion window is -1 it means that the window is not full, so send a new package
                                                                                        once this is entered, the window array will changed so the lock must be locked to prevent 
                                                                                        receiver from making any changes to array and causing inconsistent results */
            pthread_mutex_lock(&lock);
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
                    // buff_time_list[MAX_WINDOW-1] = NULL;
                    buff_packets[MAX_WINDOW-1] = NULL;
                }
                // else {
                //     VLOG(INFO, "packet is strangely empty");
                // }
                
            }
            
            if (len <= 0)                                                               // if the length of bytes read from file is 0, it means we have reached the end of file
            {
                pthread_mutex_unlock(&lock);                                            // we no longer need the lock, so unlock so the receiver can edit the window arrays if needed
                while (window_packets[0] != NULL) {}                                    // waiting for the window to be empty before sending the last, terminating packet
                sndpkt = make_packet(0);                                                // make a packet of length zero to indicate end of file

                start_timer();                                                          // start timer in case the packet does not reach the receiver
                sndpkt->hdr.seqno = 0;                                                  // add the element to the window, to be able to resend in case of timeout
                sndpkt->hdr.data_size = 0;                                              // same thing for the length
                window_packets[0] = sndpkt;                                             // same thing for the packet
                gettimeofday(&current_time,0);
                time_list[0] = current_time;
                send_base = window_packets[0]->hdr.seqno;                               // change the send base to the first element

                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);  // send the terminating packet to receiver to indicate EOF
                while(window_packets[0] != NULL) {}                                     // waiting to receive the specific ACK for the terminating packet 0 before ending the whole loop
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
            for (int i=0; i<cwnd; i++) {                                                // get the position of the window in which the next paacket ID will be located
                if (window_packets[i] == NULL && len != 0) 
                {
                    window_packets[i] = sndpkt;
                    gettimeofday(&current_time,0);
                    time_list[i] = current_time;
                    VLOG (DEBUG, "> Send packet %d to %s", next_seqno,inet_ntoa(serveraddr.sin_addr));
                    break;
                }
            }

            send_base = window_packets[0]->hdr.seqno;                                   // the send base will always be the first element in the window
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
            
            pthread_mutex_unlock(&lock);                                                // after all changes to the window arrays, we should now unlock the lock to allow the receiver to make necessary changes when needed
        }
        usleep(100);
    }
    
    end_loop = 1;                                                                       // if this loop is left, then all packets have been sent, including the terminating packet 0, so the program will jump to the end
    return NULL;
}

void *receive_ack (void *arguments)                                                     // important function to handle ACKs being received from the receiver
{
    int ack_temp = -1;                                                                  // initialize ack local var to -1, and the buffer containing the data contents 
    char buffer[DATA_SIZE]; 
    int num_duplicate = 0;                                                              // counts the number of times a duplicate ACK has been received, so when it gets to three we do something about it

    // int example = 0;
    while (1) 
    {     
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)     // receive packet from the reeiver containing the ACk
        {
            error("recvfrom");
        }
        // scanf("%d", &example);

        recvpkt = (tcp_packet *)buffer;                                                 // create a packet with the data received by the receiver
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack_temp = recvpkt->hdr.ackno;                                                  // assign the acknowledgment recived to the local variable containing the variable
    
        pthread_mutex_lock(&lock);                                                      // we need to lock because the window arrays will be amended
        // printf("received ACK with value %d\n", recvpkt->hdr.ackno);
        // ===========================
        // NEW FOR TASK 2
        if (stage == 1)
        {
            printf("    > Stage 2: Congestion Avoidance\n");
            cwnd += (1/cwnd);
        }

        if (stage == 0)                                                                 // if a successful ACK was received while in the slow-start stage, then increase cwnd by 1
        {
            printf("    > Stage 1: Slow Start\n");
            cwnd += 1;                                                                  // increase cwnd by 1
            // if (cwnd == ssthresh)                                                       // if the cwnd is equal to the slow start threshold, then enter congestion control
            //     stage += 1;                                                             // increase the stage by 1, which means reaching congestion control
        }
        graphCwnd();
        // try putting this outside
        if (cwnd == ssthresh)  {                                                      // if the cwnd is equal to the slow start threshold, then enter congestion control
            stage += 1;   
        }

        if (ack_temp == send_base)                                                      // received a duplicate ACK
        {
            printf("    > Duplicate ACK\n");
            num_duplicate += 1;                                                         // increase the counter by 1
            if (num_duplicate == 3)                                                     // if it is a triple ACK, then a packet must be lost
            {
                printf("    > Triple duplicate ACK\n");
                // FAST RETRANSMIT PHASE                                                // enter the fast retransmit stage
                stop_timer();
                for (int i=0;i<MAX_WINDOW;i++) 
                {
                    if (buff_packets[i] == NULL)
                    {
                        loc_buff_pkt = i;
                        break;
                    }
                }
                for (int i=1;i<cwnd;i++) 
                {
                    buff_packets[loc_buff_pkt] = window_packets[i];
                    buff_time_list[loc_buff_pkt++] = time_list[i];
                    window_packets[i] = NULL;
                    // time_list[i] = NULL;
                }
                cwnd = 1;                                                               // let the cwnd be 1 again
                ssthresh = MAX(cwnd/2, 2);  
                graphCwnd();                                            // let the new ssthresh be max(cwnd/2,2)
                stage = 0;                                                              // go back to slow start phase
                num_duplicate = 0;                                                      // change the counter back to 0 after the fast retransmit phase
                resend_three_ack();
            }
            // continue;
        }
        // ===========================
        // printf("1\n");

        for (int i=0; i<cwnd; i++)
        {
            if (window_packets[i] != NULL)
            {
                // printf("2\n");
                // printf("Por position %d, if %d == %d and %d >= %d\n", i, window_packets[i]->hdr.seqno, ack_temp - window_packets[i]->hdr.data_size, ack_temp, send_base);
                // printf("ACK received for seqno: %d\nSeqno: %d\nSend base: %d\n",ack_temp - window_packets[i]->hdr.data_size, window_packets[i]->hdr.ackno, send_base);
                if (window_packets[i]->hdr.seqno == ack_temp - window_packets[i]->hdr.data_size && ack_temp >= send_base)    // find the position of the window that contains the packet for the ACK received
                {
                    // RECALCULATE TIMEOUT INTERVAL
                    if (resend_ack == 0)
                    {
                        gettimeofday(&current_time,0);
                        sample_rtt = (current_time.tv_sec - time_list[i].tv_sec) * 1000 + (current_time.tv_usec - time_list[i].tv_usec) / 1000;
                        printf("Sample RTT: %f\n", sample_rtt);
                        if (estimated_rtt < 0)
                            estimated_rtt = sample_rtt;
                        else
                        {
                            estimated_rtt = (1-alpha)*estimated_rtt + alpha * sample_rtt;
                            dev_rtt = (1-beta) * dev_rtt + beta * abs(sample_rtt - estimated_rtt);
                        }
                        timeout_interval = estimated_rtt + 4 * dev_rtt;
                    }
                    // printf("3\n");
                    send_base = window_packets[i]->hdr.seqno;                           // say the k position was found, then all of the k-1 positions are also ACK'ed by the ACK received, so let the base be the packet for which the ACK was just received
                    // printf("5\n");
                    VLOG(DEBUG, "> Received successful ACK for packet %d", ack_temp - window_packets[i]->hdr.data_size);    // indicate packet ACK has been successfully received
                    for (int j = i+1; j<floor(cwnd); j++) 
                    {
                        if (window_packets[j] != NULL)
                        {
                            window_packets[j-i-1] = window_packets[j];                  // move up all of the packets to let the i position be first
                            time_list[j-i-1] = time_list[j];
                            window_packets[j] = NULL;                                   // move up all of the packets to let the i position be first
                            // time_list[j] = NULL;
                        }
                        else 
                        {
                            window_packets[j-i-1] = NULL;
                            // time_list[j-i-1] = NULL;
                            break;
                        }
                        // printf("7\n");
                    }
                    init_timer(timeout_interval, resend_packets);
                    // if (window_packets[0] != NULL)
                    //     printf("First packet: %d\n",window_packets[0]->hdr.seqno);
                    // printf("6\n");
                    if (window_packets[0] == NULL)                                      // if the first element has been acknowledged, list is empty, so stop timer until further notice
                    {
                        stop_timer(); 
                    }
                    // printf("8\n");
                }
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

void graphCwnd() {
    FILE *cwndFile;
    cwndFile = fopen("CWND.csv","a");
    float timeToPrint; 
    struct timeval innerTime;

    if (cwndFile) {
        
        gettimeofday(&innerTime,0);
        
        timeToPrint = (innerTime.tv_sec - current_time.tv_sec) * 1000.0f + (innerTime.tv_usec - current_time.tv_usec) / 1000.0f;
    }
    else {
        VLOG(INFO,"could not write into CWND file");
        exit(1);
    }
    fprintf(cwndFile,"%f,%f,%d\n",timeToPrint,cwnd,ssthresh);
    VLOG(INFO,"printed to file: %f, %f, %d", timeToPrint,cwnd,ssthresh);
    fclose(cwndFile);

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
        printf("mutex init failed\n");
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

    return 0;
}