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
#include<pthread.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond

int next_seqno = 0;
int send_base = 0;
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;   

struct args_for_function
{
    int length;
    char buff[DATA_SIZE];
    FILE* ptr;
    int current_value;
    int return_value;
};


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

void *send_packet_wait_ack (void *arguments) 
{
    struct args_for_function * args = (struct args_for_function *) arguments;
    int len = args->length;
    char buffer[DATA_SIZE];
    strcpy(buffer, args->buffer);
    char current_val = args->current_value;                              
    if (len <= 0)                                                           // if len <= 0 it means that there is nothing else to read, so sedn a final packet to the receiver explainign that it is end of file
    {
        VLOG(INFO, "End Of File has been reached");
        sndpkt = make_packet(0);
        sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (const struct sockaddr *)&serveraddr, serverlen);
        break;
    }
    sndpkt = make_packet(len);
    memcpy(sndpkt->data, buffer, len);
    sndpkt->hdr.seqno = send_base;
    //Wait for ACK
    do {

        VLOG(DEBUG, "Sending packet %d to %s", 
                send_base, inet_ntoa(serveraddr.sin_addr));
        /*
            * If the sendto is called for the first time, the system will
            * will assign a random port number so that server can send its
            * response to the src port.
            */
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }

        start_timer();
        //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
        //struct sockaddr *src_addr, socklen_t *addrlen);

        do
        {
            if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            printf("%d \n", get_data_size(recvpkt));
            assert(get_data_size(recvpkt) <= DATA_SIZE);
        }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
        stop_timer();
        /*resend pack if don't recv ACK */
    } while(recvpkt->hdr.ackno != next_seqno);      
    free(sndpkt);

    args->
    pthread_exit(NULL);
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

    int window[window_size];                                                    // window of unacked elements with maximum size the window size
    
    for (int i = 0; i<window_size; i++) 
    {
        window[i] = -1;
    }

    int cThread = 0;                                                            // number to keep track of which thread is currently free
    pthread_t threads[window_size];                                             // threads to send the packet and wait for the ACK
    struct args_for_function arguments[window_size];

    int whichFound;

    send_base = 0;
    next_seqno = 0;
    while (1) 
    {
        if (window[window_size - 1] == -1)                                                   // it can accept one more unacked 
        {
            len = fread(buffer, 1, DATA_SIZE, fp);

            arguments[cThread].length = len;
            strcpy(arguments[cThread].buff, buffer);
            arguments[cThread].ptr = fp;
            arguments[cThread].current_value = next_seqno;
            arguments[cThread].return_value = -1;

            for (int i = 0; i < window_size; i++) 
            {
                if (window[i] == -1) 
                {
                    window[i] = next_seqno;
                    break;
                }
            }
            next_seqno += len;
            
            if (pthread_create(&threads[cThread], NULL, &send_packet_wait_ack, (void *) &arguments[cThread]) != 0) // create thread and pass argument
                break;

            usleep(1000);
        }

        whichFound = -1;
        for (int i = 0; i < window_size; i++) 
        {
            if (arguments[i].return_value != -1) 
            {
                for (int j = 0; j < window_size; j++) 
                {
                    if (window[j] == arguments[i].return_value - arguments[i].length && window[j] > send_base) 
                    {
                        send_base = window[j];
                        for (int k = j; k<window_size; k++) 
                        {
                            window[k-j] = window[k];
                        }
                    }
                }
            }
            if (arguments[i].return_value != -1 && whichFound == -1) 
            {
                whichFound = arguments[i].return_value - arguments[i].length;
            }
            if (send_base == arguments[i].return_value - arguments[i].length) 
            {
                for (int j = 1; j < window_size; j ++) 
                {
                    window[j - 1] = window[j];
                }
                window[window_size - 1] = -1;
                send_base = window[0];
                whichFound = -1;
                break;
            }
        }
        if (whichFound != -1) 
        {

        }
    }

    init_timer(RETRY, resend_packets);

    while (1)
    {
        len = fread(buffer, 1, DATA_SIZE, fp);                                  // fread stores an amount of DATA_SIZE bytes into the buffer
        if (len <= 0)                                                           // if len <= 0 it means that there is nothing else to read, so sedn a final packet to the receiver explainign that it is end of file
        {
            VLOG(INFO, "End Of File has been reached");
            sndpkt = make_packet(0);
            sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (const struct sockaddr *)&serveraddr, serverlen);
            break;
        }
        send_base = next_seqno;
        next_seqno = send_base + len;
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = send_base;
        //Wait for ACK
        do {

            VLOG(DEBUG, "Sending packet %d to %s", 
                    send_base, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            start_timer();
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);

            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                printf("%d \n", get_data_size(recvpkt));
                assert(get_data_size(recvpkt) <= DATA_SIZE);
            }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
            stop_timer();
            /*resend pack if don't recv ACK */
        } while(recvpkt->hdr.ackno != next_seqno);      

        free(sndpkt);
    }

    return 0;

}



