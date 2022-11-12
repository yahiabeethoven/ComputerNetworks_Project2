all: rdt_sender rdt_receiver

FTPclient: 
	gcc rdt_sender.c -o rdt_sender

FTPserver:
	gcc rdt_receiver.c -o rdt_receiver