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

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond

int next_seqno = 0;
int send_base = 0;
const int window_size = 10;
int current_packet = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

int window[window_size];                                                                // create a window with the ID of every packet being sent
int lens[window_size];   

struct args_send_packet
{
    FILE *file;
};

struct args_rec_ack {
    // int ack;
};

void *send_packet (void *arguments) 
{
    struct args_send_packet * args = (struct args_send_packet *) arguments;

    int len;
    char buffer[DATA_SIZE];
    FILE *fp = args->file;

    send_base = 0;
    next_seqno = 0;

    while (1) 
    {
        if (window[-1] == -1) {                                                         // if the last element in the window is -1 it means that the window is not full, so send a new package
            len = fread(buffer, 1, DATA_SIZE, fp);

            for (int i=0; i<window_size; i++) {                                         // get the position of the window in which the next paacket ID will be located
                if (window[i] == -1) 
                {
                    window[i] = next_seqno;                                             // when that position is found, set the packet ID to the next_seqno, since it will be the ID of the packet being sent
                    lens[i] = len;
                    break;
                }
            }

            if ( len <= 0)
            {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0, (const struct sockaddr *)&serveraddr, serverlen);
                break;
            }
            
            send_base = window[0];                                                      // the send base will always be the first element in the window
            next_seqno = send_base + len;                                               // the next sequence number is increased by the size of the package sent
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = send_base;

            VLOG (DEBUG, "Sending packet %d to %s\n", send_base, inet_ntoa(serveraddr.sin_addr));

            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
        }
    }
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
        printf("%d \n", get_data_size(recvpkt));
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        ack = recvpkt->hdr.ackno;                                                       // assign the acknowledgment recived to the local variable containing the variable

        if (ack != -1) 
        {
            for (int i=0; i<window_size; i++) 
            {
                if (window[i] == ack - lens[i] && ack >= send_base)                     // find the position of the window that contains the packet for the ACK received
                {
                    send_base = window[i];                                              // say the k position was found, then all of the k-1 positions are also ACK'ed by the ACK received, so let the base be the packet for which the ACK was just received
                    for (int j = i; j<window_size; j++) 
                    {
                        window[j-i] = window[j];                                        // move up all of the packets to let the i position be first
                        lens[j-i] = lens[j];                                            // same thing for the lengths

                        window[i - j - 1] = -1;                                         // let the i number of elements trailing in the window be free
                        lens[i - j -1] = -1;                                            // same but for the lengths

                        VLOG(DEBUG, "Received ACK %d, which corresponds to the %d elemnent in the window\n", ack, i);
                    }
                }
            }
        }
        ack = -1;
    }
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
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


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
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

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);

    pthread_t threads[2];                                                               // create two threads, one for sending the data and one for receving ACKs
    struct args_send_packet arguments_send;                                             // create a structure with the data to send to the function when the thread is created
    struct args_rec_ack arguments_receive;

    int window[window_size];                                                            // create a window with the ID of every packet being sent
    for (int i=0; i<window_size; i++)                                                   // set every element in the window to -1 to show that it is empty
        window[i] = -1;

    arguments_send.file = fp;

    if (pthread_create(&threads[0], NULL, &send_packet, (void *) &arguments_send) != 0)    // create thread to send the package
        printf("Error creating the thread to send the packets\n");

    if (pthread_create(&threads[0], NULL, &send_packet, (void *) &arguments_receive) != 0)  // create thread to receive ACKs
        printf("Error creating the thread to receive ACKs\n");

    return 0;

}



