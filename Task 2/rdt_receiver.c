#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include"common.h"
#include"packet.h"

#define size_of_buffer 64

tcp_packet *recvpkt;
tcp_packet *sndpkt;

tcp_packet *pkt_buffer[size_of_buffer];                                               // make a buffer to store the sequence numbers of the out-of-order packets

int main(int argc, char **argv) {
    int sockfd;                                                                         // socket
    int portno;                                                                         // port to listen on
    int clientlen;                                                                      // byte size of client's address
    struct sockaddr_in serveraddr;                                                      // server's addr
    struct sockaddr_in clientaddr;                                                      // client addr
    int optval;                                                                         // flag value for setsockopt
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    if (argc != 3) {                                                                    // check command line arguments 
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w+");
    if (fp == NULL) {
        error(argv[2]);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);                                            // socket: create the parent socket 
    if (sockfd < 0) 
        error("ERROR opening socket");

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    bzero((char *) &serveraddr, sizeof(serveraddr));                                    // build the server's Internet address
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);
    
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)          // bind: associate the parent socket with a port 
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    int current_packet = 0;

    for (int i=0; i<size_of_buffer; i++)                                                // initialize an empty buffer for out-of-order packets
    {
        pkt_buffer[i] = NULL;
    }

    int out_order_pkt = 0;

    while (1) {
        printf("Expecting %d\n", current_packet);
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0)        // recvfrom: receive a UDP datagram from a client
            error("ERROR in recvfrom");
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);  

        if (current_packet != recvpkt->hdr.seqno)                                       // if the packet received is not the exepcted packet in order, then continue because maybe it will come later
        {
            // printf("current_packet != recvpkt->hdr.seqno");
            if (recvpkt->hdr.data_size == 0) {                                          // if the data size is 0 it means that the packet is the last packet sent to represent EOF
                VLOG(INFO, "> End Of File has been reached!");
                fclose(fp);
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
                break;
            }

            if (recvpkt->hdr.seqno < current_packet)                                    // if the received packet has a sequence number less than the one received, sent an ACK since the previous ack probably was not received properly
            {
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
            }

            if (recvpkt->hdr.seqno > current_packet)                                    // the packet received is out-of-order, so buffer
            {
                for (int i=0; i<size_of_buffer; i++) 
                {
                    if (pkt_buffer[i] == NULL)                                          // find the first item that is empty
                    {
                        pkt_buffer[i] = recvpkt;                                        // add the ack number to the out-of-order packet buffer
                        break;
                    }
                }
                sndpkt = make_packet(0);                                                // sending duplicate ACK, which has the same value as the previous ACK sent
                sndpkt->hdr.ackno = out_order_pkt;                                      // let the ack number be the out-of-order pkt sequence number
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
            }
            continue;
        }
        else                                                                            // in task 2, this else means that the packet received is the first packet in the window, so there is a possibility that the out-of-order packets buffered are directly after
            current_packet += recvpkt->hdr.data_size;                                   // if it is the packet expected, then send acknowledgement

        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "> %lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        // printf("10\n");
        fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
        // printf("5\n");
        fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
        // printf("6\n");
        fflush(fp);
        // printf("7\n");
        sndpkt = make_packet(0);
        // printf("8\n");
        sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
        out_order_pkt = recvpkt->hdr.seqno + recvpkt->hdr.data_size;                    // set the ack for the out-of-order packet to be sent in case the next packet received is not the one expected
        // printf("9\n");
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
        // printf("Sent ACK!\n");
        // printf("1\n");
        for (int i=0; i<size_of_buffer; i++)                                            // go through the list of buffered packets to see if any of them is the one expected after the one received
        {
            // printf("11\n");
            if (pkt_buffer[i] != NULL)
            {
                // printf("11\n");
                if (pkt_buffer[i]->hdr.seqno < current_packet)                         // if the packet in the buffer was a duplicate and we have moved on from that one, then take it out of the buffer 
                {
                    // printf("12\n");
                    for (int j=i;j<size_of_buffer-1;j++)
                    {
                        // printf("13\n");
                        pkt_buffer[j] = pkt_buffer[j+1];
                        // printf("14\n");
                        if (pkt_buffer[j] == NULL && pkt_buffer[j+1] == NULL)
                            break;
                        // printf("15\n");
                    }
                    // printf("16\n");
                    pkt_buffer[size_of_buffer-1] = NULL;                                    // make sure the last one is also changed, since the for loop does not traverse through it
                    i -= 1;                                                                 // decrease i by one, since we have moved all of the elements and need to see the one holding the position where the previous one was
                    // printf("pkt_buffer[i]->hdr.seqno < current_packet");
                }
                else if (current_packet == pkt_buffer[i]->hdr.seqno)
                {
                    // printf("3\n");
                    current_packet += pkt_buffer[i]->hdr.data_size;                         // increase the current packet value by the size to see if the next one is also in 
                    gettimeofday(&tp, NULL);
                    VLOG(DEBUG, "> %lu, %d, %d (recovered from buffer)", tp.tv_sec, pkt_buffer[i]->hdr.data_size, pkt_buffer[i]->hdr.seqno);
                    fseek(fp, pkt_buffer[i]->hdr.seqno, SEEK_SET);
                    fwrite(pkt_buffer[i]->data, 1, pkt_buffer[i]->hdr.data_size, fp);
                    fflush(fp);
                    sndpkt = make_packet(0);
                    sndpkt->hdr.ackno = pkt_buffer[i]->hdr.seqno + pkt_buffer[i]->hdr.data_size;
                    sndpkt->hdr.ctr_flags = ACK;
                    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                        error("ERROR in sendto");
                    }
                    for (int j=i;j<size_of_buffer-1;j++)                                    // move all of the elements to erase the one that has just been found to be in order
                    {
                        pkt_buffer[j] = pkt_buffer[j+1];
                        if (pkt_buffer[j] == NULL && pkt_buffer[j+1] == NULL)
                            break;
                    }
                    i -= 1;                                                                 // decrease i by one, since we have moved all of the elements and need to see the one holding the position where the previous one was
                    pkt_buffer[size_of_buffer-1] = NULL;                                    // make sure the last one is also changed, since the for loop does not traverse through it
                    // printf("current_packet == pkt_buffer[i]->hdr.seqno");
                }
            }
            else
                break;
            // printf("4\n");
        }
    }

    VLOG(INFO, "> Terminating program...");

    return 0;
}