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
#define RETRY  120 //millisecond
#define window_size 10

int next_seqno = 0;
int send_base = 0;

// const int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

int window[window_size];                                                                // create a window with the ID of every packet being sent
tcp_packet *window_packets[window_size];                                                // list to store the pointers where the packets are stored
int lens[window_size];
int access_window;                                                                      // bool to decide who is acessing the window at a time: the sender or receiver
int stopTimer;                                                                          // if the receiver side recives an ACK, then stop the timer
int end_loop = 0;

// lock logic
pthread_mutex_t lock;

int packetCounter = 0;
int ackCounter = 0;

struct args_send_packet
{
    FILE *file;
};

struct args_rec_ack {
    // int ack;
};


void resend_packets(int sig)
{
    VLOG(INFO, "Timeout happend");
    printf("Resend packets\n");
    
    if (sig == SIGALRM)
    {
        for (int i=0;i<window_size;i++) {
            printf("entered for loop of resend packages \n");
            // ================================================
            // PRINTING (CAN BE TAKEN OUT)
            printf("Window = [");
            for (int i = 0; i<window_size-1; i++) 
            {
                printf("%d, ", window[i]);
            }
            printf("%d",window[window_size-1]);
            printf("]\n\n");
            // printf("Lenghts = [");
            // for (int i = 0; i<window_size-1; i++) 
            // {
            //     printf("%d, ", lens[i]);
            // }
            // printf("%d",lens[window_size-1]);
            // printf("]\n");
            // ================================================
            if (window[i] == -1)
                break;

            sndpkt = window_packets[i];                                                 // fetch the packet from the list of pointers containing the packets
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

void *send_packet (void *arguments) 
{
    struct args_send_packet * args = (struct args_send_packet *) arguments;

    int len;
    char buffer[DATA_SIZE];
    FILE *fp = args->file;

    int location = -1;                                                                  // sets the position for the first "free" spot in the window

    send_base = 0;                                                                      // initialize the base
    next_seqno = 0;                                                                     // initialize the next sequence number

    init_timer(RETRY, resend_packets);                                                  // initialize the timer

    // int var = 0;

    while (1) 
    {
        if (window[window_size - 1] == -1 && access_window == 0) {                      // if the last element in the window is -1 it means that the window is not full, so send a new package
            
            access_window = 1;                                                          // because the access window bool is true, it means that the sender function can access and change the window, so turn to false so the receiver can not access the window at the same time
            len = fread(buffer, 1, DATA_SIZE, fp);                                      // read a number of DATA_SIZE bytes from the file

            for (int i=0; i<window_size; i++) {                                         // get the position of the window in which the next paacket ID will be located
                
                if (window[i] == -1 && len != 0) 
                {
                    window[i] = next_seqno;                                             // when that position is found, set the packet ID to the next_seqno, since it will be the ID of the packet being sent
                    lens[i] = len;                                                      // same thing to the length of the packet
                    VLOG (DEBUG, "Sending packet %d with length %d to %s", window[i], len ,inet_ntoa(serveraddr.sin_addr));
                    location = i;                                                       // get a location for the packet in the list to then add the packet to the window_packet
                    break;
                }
            }

            // ================================================
            // PRINTING (CAN BE TAKEN OUT)
            // printf("Window = [");
            // for (int i = 0; i<window_size-1; i++) 
            // {
            //     printf("%d, ", window[i]);
            // }
            // printf("%d",window[window_size-1]);
            // printf("]\n");
            // printf("Lenghts = [");
            // for (int i = 0; i<window_size-1; i++) 
            // {
            //     printf("%d, ", lens[i]);
            // }
            // printf("%d",lens[window_size-1]);
            // printf("]\n");
            // ================================================

            if (len <= 0)                                                               // if we reach EOF
            {
                printf("length is 0\n");
                access_window = 0;
                while (window[0] != -1) {}
                sndpkt = make_packet(0);

                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);
                VLOG(INFO, "End-of-file has been reached");
                // cellularGold
                FILE *senderTest = fopen("../ComputerNetworks_Project2/cellularGold","r");
                // rapidGold
                // FILE *senderTest = fopen("../ComputerNetworks_Project2/rapidGold","r");
                // highwayGold
                // FILE *senderTest = fopen("../ComputerNetworks_Project2/highwayGold","r");
                if (senderTest){
                    printf("opened sender\n");
                }
                // tests to see the error in mahimahi
                //----------------------------------------------------------------
                FILE *receiverTest = fopen("../com.c","r");
                if (receiverTest) {
                    printf("opened receiver\n");
                }
                fseek(senderTest,0,SEEK_END);
                long int sender_size=ftell(senderTest);
                printf("receiver size proper\n");
                fseek(senderTest,0,SEEK_SET);
                printf("fseek 1 proper\n");
                char* sender_contents; 
                sender_contents=(char*)malloc(sender_size);
                
                fread(sender_contents,sender_size,1,senderTest);

                fseek(receiverTest,0,SEEK_END);
                long int receiver_size=ftell(receiverTest);
                
                fseek(receiverTest,0,SEEK_SET);
                printf("fseek 2 proper\n");
                char* receiver_contents; 
                receiver_contents=(char*)malloc(receiver_size);
                fread(receiver_contents,receiver_size,1,receiverTest);

                int matchCounter = 0; 
                int wrongCounter = 0;
                
                if (sender_size == receiver_size) {
                    // for (int i = 0; i < sender_size; i++) {
                    //     if (sender_contents[i] == receiver_contents[i]) {
                    //         matchCounter++;
                    //         printf("match: %d\n",matchCounter);
                    //     }
                    //     else {
                    //         wrongCounter++;
                    //         printf("wrong: %d\n",wrongCounter);
                    //     }
                    // }      
                }
                else {
                    printf("sizes mismatch\n");
                }
                printf("sender contents proper, %ld\n",sender_size);
                printf("receiver size proper, %ld\n",receiver_size);
                free(sender_contents);
                printf("free sender memory\n");
                free(receiver_contents);
                printf("free receiver memory\n");
                //---------------------------------------------------------------- 
                end_loop = 1; 
                                                                     // let the program end when it reaches EOF
                return NULL;
            }
            
            send_base = window[0];                                                      // the send base will always be the first element in the window
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);

            sndpkt->hdr.seqno = next_seqno;
            next_seqno += len;                                                          // the next sequence number is increased by the size of the package sent

            window_packets[location] = sndpkt;                                          // add the packet to the list of packets
            // 2673216 2213120
            // if (next_seqno - len != 2673216 && next_seqno - len != 2213120 && next_seqno - len != 2971696 && next_seqno - len != 3313856)
            // {
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            // }

            if (next_seqno - len == 0)                                                  // start the timer if the sent packet is the first
            {   
                start_timer();
                // printf("Time initialized\n");
            }
            else if (window_size > 1) 
            {
                if (window[1] == -1)                                                    // if the timer is stopped because the window was empty and we just added a packet, then start the timer again
                {
                    // printf("Start time\n");
                    start_timer();
                }
            }
            access_window = 0;                                                          // let the receiver function access the window
        }
        usleep(100);
    }
    end_loop = 1;
    return NULL;
}

void *receive_ack (void *arguments) 
{
    int ack = -1;
    char buffer[DATA_SIZE]; 

    while (1) 
    {
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)     // receive packet from the reeiver containing the ACk
        {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;                                                 // create a packet with the data received by the receiver
        // printf("%d \n", get_data_size(recvpkt));
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack = recvpkt->hdr.ackno;                                                       // assign the acknowledgment recived to the local variable containing the variable
        
        // printf("Received ACK: %d, received size: %d\n", ack, recvpkt->hdr.data_size);
        if (ack != -1) 
        {
            while (access_window == 1)                                                  // function to wait for the sender function to free the window array (so the functions to not access it at the same time)
            {
                if (access_window == 0)                                                 // when the access window bool turns to 0 it means it is free, so it can be changed
                {
                    break;
                }
            }
            access_window = 1;
            for (int i=0; i<window_size; i++) 
            {
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
                    VLOG(DEBUG, "Received ACK %d, which corresponds to the %d elemnent in the window\n", ack, i+1);

                    printf("Timer initialized because a successful ACK was received\n");
                    //init_timer(RETRY, resend_packets);
                    // start_timer();

                    if (window[0] == -1)
                    {
                        printf("Timer stopped because the window is empty\n");
                        stop_timer(); 
                    }
                    break;
                }
            }
            access_window = 0;
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

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);
    // try that
    // serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind
    // if (bind(sockfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr))<0) {
    //     perror("Bind failed..\n");
    //     return -1;
    // }

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);

    pthread_t threads[2];                                                               // create two threads, one for sending the data and one for receving ACKs
    struct args_send_packet arguments_send;                                             // create a structure with the data to send to the function when the thread is created
    struct args_rec_ack arguments_receive;

    for (int i=0; i<window_size; i++)                                                   // set every element in the window to -1 to show that it is empty
    { 
        window[i] = -1;
        lens[i] = -1;
    }
    access_window = 0;                                                                  // only let one of the functions access the window at a time to avoid problems
    arguments_send.file = fp;

    if (pthread_create(&threads[0], NULL, &send_packet, (void *) &arguments_send) != 0){    // create thread to send the package
        printf("Error creating the thread to send the packets\n");
    }
    else{
        packetCounter++;
    }
    if (pthread_create(&threads[1], NULL, &receive_ack, (void *) &arguments_receive) != 0) {  // create thread to receive ACKs
        printf("Error creating the thread to receive ACKs\n");
    }
    else {
        ackCounter++;
    }
    
    while(end_loop == 0){}
    pthread_join(threads[0],NULL);
    
    // for (int i = 0; i < 2; i++) {
    //     pthread_join(threads[i],NULL);  
    //     printf("thread joined: i = %d\n",i);  
    // }

    return 0;

}