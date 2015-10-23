/*
 * main.cpp
 *
 *  Created on: Aug 27, 2013
 *  Author: streaming
 */

#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <inttypes.h>
#include <assert.h>
#include <math.h>
#include <algorithm>

#include<signal.h>

#include <list>
using std::list;
#include <vector>
using std::vector;

#include "main_send.h"

//#define NUM 2
#define VIDEO_RATE 90000
#define RTCP_BW 0.05*2097152/8 /*2Mbps video*/

#define ROUND_ROBIN 0

pthread_mutex_t list_mutex;
pthread_mutex_t rtcpsr_mutex;
pthread_mutex_t rtcprr_mutex;

vector<struct status *> path_status;
vector<struct mprtphead *> mrheader;

//list<struct rtcprrbuf *> rtcp_rr_packets;
std::vector <list <struct rtcprrbuf *> > rtcp_rr_packets(NUM); 	//Create vector of list of structures for rtcprr packets

std::vector<enum path_t> thread_path(NUM, INIT);
std::vector<enum path_t> lastpath(NUM, INIT);

list<struct rtppacket *> packets_to_send; //Create list of packets!!!!
struct rtcpsrbuf rtcpsr;

int64_t ts0 = -1;
int64_t st0 = -1;
static int term_flag = 0;
FILE *log_path = NULL;
FILE *log_rtcp_s = NULL;
FILE *log_rtcp_r = NULL;
FILE *stup = NULL;
FILE *path_par = NULL;
FILE *log_rtp = NULL;
FILE *log_all = NULL;
int i_senders;
int RTCP_RR_flag = 0;

int fdrtcp[NUM]; //descriptor for rtcp socket
pthread_t threadrtp[2], threadrtcpr[2], threadrtcps[2];
bool globalTerminationFlag = false;
static int64_t p_rrtime;

int main(int argc, char* argv[])
{
	int n = NUM;
	i_senders = n;
	char filename[255]="";
	int port[NUM]={4000,4002};
	char ip[NUM][20]={IP1,IP2};
	char ipout[NUM][20]={IP1,IP2};
	char iptx[20] = {IPTX};
	int txport = 4402;

	getargs(argc, argv, filename, port, ip, iptx, &txport, ipout);
	// For threads termination
	/*install signal handler*/

	struct sigaction sigAction;
	sigAction.sa_handler = signalHandler;
	//sigAction.__sigaction_handler = &signalHandler;

	sigemptyset(&sigAction.sa_mask);
	sigAction.sa_flags = 0;
	assert(0 == sigaction(SIGINT, &sigAction, NULL));

	for (int i = 0; i < n; i++)
	{
		printf("ipout = %s \n", ipout[i]);

		path_status.push_back(new struct status);
		memset(path_status.back(), 0, sizeof(struct status));

		mrheader.push_back(new struct mprtphead);
		memset(mrheader.back(), 0, sizeof(struct mprtphead));
		MPRTP_Init_subflow(mrheader.at(i), i);
	}

	stup = fopen("/home/streaming/MPRTP/stup_file", "w");

	log_all = fopen("/home/streaming/MPRTP/log_all", "w");
	if (log_all == NULL)
	{
		printf("Impossible to open or create txt log file for RTP!!!");
	}
	// Create log file for monitor RTP packets
	log_rtp = fopen("/home/streaming/MPRTP/log_rtp_file", "w");
	if (log_rtp == NULL)
	{
		printf("Impossible to open or create txt log file for RTP!!!");
	}
	else
		fprintf(log_rtp, "%4s %6s %10s %10s %16s \n", "Path", "Length", "Seq", "ts", "send_time");

	log_rtcp_r = fopen("/home/streaming/MPRTP/log_rtcp_r", "w");
	if (log_rtcp_r == NULL)
	{
		printf("Impossible to open or create txt rtcp_r!!!");
	} else
		fprintf(log_rtcp_r, "path     ts 		ehsn 	fraclost	totallost	jitter		LSR : LSR_FRAC		DLSR:DLSR_FRAC	      Packet_sent\n");

	//Create 3 threads for each interface: 1) for RTP; 2) RTCP send; 3)RTCP receive
	create_threads(n, ip, port, ipout);

	// Open RTP file
	int fd = openfile(filename);

	path_par = fopen("/home/streaming/MPRTP/path_parameters", "w");
	if (path_par == NULL)
	{
		printf("Impossible to open or create txt rtcp_r!!!");
	}
	else
		fprintf(path_par,
				"%5s %10s %9.6s %17s %14s %14s %14s %18s %10s %7s %7s %7s %7s\n",
				"path", "rtt", "PLR", "Bandwidth", "payload", "sender_oc",
				"last_sender_oc", "rrtime", "delta_t", "EHSN", "LAST_EHSN",
				"dif_pc", "total loss");

	log_rtcp_s = fopen("/home/streaming/MPRTP/log_rtcp_s", "w");
	if (log_rtcp_s == NULL)
		throw std::runtime_error("File creation failed");
	else
		fprintf(log_rtcp_s, "path	ts 		sender_oc 	sender_pc \n");

	log_path = fopen("/home/streaming/MPRTP/log_path_file", "w");
	if (log_path == NULL)
	{
		printf("Impossible to open or create txt log file for PATH!!!");
		return -1; // Exception!!!!
	}
	else
		fprintf(log_path, "path 	   seq		ts \n");

	//Read the packets from RTP File

	int totalbytes = 0;

	bool allread = false;

	while (term_flag == 0)
	{
		fprintf(log_all, "In the beggining of while loop \n");

		struct rtppacket *packet = NULL;

		while (allread == false)
		{
			packet = new struct rtppacket;
			memset(packet->buf, 0, sizeof(packet->buf));

			if (readpacket(packet, fd) == 0)
			{
				int64_t t = now()%10000000000;
				fprintf(log_all, " %" PRId64 " End of file!\n", t);
				allread = true;
				break;
			}
			else
			{
				fprintf(log_all, " Read %7d %6d %16d \n", packet->packetlen, packet->seq, packet->ts);
				/*	fprintf(log, "%d 	%d		%" PRIu32 " \n", packet->packetlen,
				 packet->seq, packet->dump_ts);*/
			}

			if (ts0 == -1)
			{
				//In my case ts0 always = 0!!!Because we start to count from 0 in ts of packets
				ts0 = packet->dump_ts;
				st0 = now();
			}
			totalbytes += packet->packetlen;
			//printf("Totalbytes %d was written to the list\n", totalbytes);
			fprintf(log_all, "st0 = %" PRId64 " \n", st0);

			if (pthread_mutex_lock(&list_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			if (!packets_to_send.empty() && packets_to_send.back()->ts != packet->ts)
			{
				if (pthread_mutex_unlock(&list_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

				insert_data_to_list(packet);
				break;
			}

			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			insert_data_to_list(packet);
		}

		/*number of clocks to wait before sending packet
		 * Check the packets in the list. We should wait only in case when*/

		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		fprintf(log_all, "Size of the list %d \n", packets_to_send.size());

		for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);
			if (count == NUM)
			{
				//Sleep until wake (in microsec)

				int64_t t2 = now()%10000000000;
				const int64_t wake = ((int64_t) (*it)->dump_ts - ((ts0 * (int64_t) 1000000)) / (int64_t) VIDEO_RATE) + st0;
				fprintf(log_all, "%" PRId64 " wake = %" PRId64 ", seq = %" PRIu16 "\n", t2, wake%10000000000, (*it)->seq);

				if (pthread_mutex_unlock(&list_mutex) != 0)
					throw std::runtime_error("Mutex unlock failed");

				waiting(wake);
				int64_t t3 = now()%10000000000;
				fprintf(log_all, "%" PRId64 " after waiting, really waiting = %" PRId64 " \n", t3, t3-t2);

				if (pthread_mutex_lock(&list_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

				break;
			}
		}

		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		//Select the paths for data transmission

#if ROUND_ROBIN
		thread_path = path_select(lastpath);
#else
		thread_path = path_scheduling(lastpath);
#endif

		//Call RTP thread
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (!packets_to_send.empty())
		{
			for (uint i = 0; i < thread_path.size(); i++)
			{
				if (thread_path.at(i) == SENT)
				{
					int64_t t = now()%10000000000;
					fprintf(log_all, "%" PRId64 " Signal to rtp_send thread %d \n", t, i);
					pthread_cond_signal(&path_status.at(i)->rtp_cond);
				}
			}

			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
		else
		{
			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}

		lastpath = thread_path;

		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (packets_to_send.empty() == 1)
		{
			term_flag = 1;
			fprintf(log_all, "term_flag = %d \n", term_flag);

			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
		else
		{
			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
	}

	if (fclose(log_rtp) == 0)
		printf("Log_rtp file was successfully closed \n");
	else
		printf("Error during the procedure of closing log file: %d \n", errno);

	if (fclose(log_rtcp_r) == 0)
		printf("Log_rtcp_r file was successfully closed \n");
	else
		printf("Error during the procedure of closing log_rtcp_r file: %d \n",
		errno);

	if (fclose(log_rtcp_s) == 0)
		printf("Log_rtcp_s file was successfully closed \n");
	else
		printf("Error during the procedure of closing Log_rtcp_s file: %d \n",
		errno);

	if (fclose(log_all) == 0)
		printf("Log_all file was successfully closed \n");
	else
		printf("Error during the procedure of closing log_all file: %d \n",
				errno);

	if (term_flag == 1)
	{
		for (int i = 0; i < n; i++)
		{
			printf("Total number of packet sent to path %d = %d \n", i,
					path_status.at(i)->sender_pc);
			printf("Total lost for path %d = %f \n", i,
					path_status.at(i)->loss_rate);
		}
	}

	return 0;
}

void getargs(int argc, char* argv[], char * filename, int * port, char ip[][20], char iptx[], int * txport, char ipout[][20])
{
  int c; int len; char * tmp;
  while((c=getopt(argc, argv, "hp:a:b:c:d:e:g:f:"))!=-1)
  {
	switch(c)
	{
		case 'a':
			tmp = strstr(optarg, ":");
			len = tmp-optarg;
			strncpy(ip[0], optarg, len);
			ip[0][len]='\0';
			*port = atoi(tmp+1);
			break;
		case 'b':
			tmp = strstr(optarg, ":");
			len = tmp-optarg;
			strncpy(ip[1], optarg, len);
			ip[1][len]='\0';
			*(port+1) = atoi(tmp+1);
			break;
		case 'c':
			tmp = strstr(optarg, ":");
			len = tmp-optarg;
			strncpy(ip[2], optarg, len);
			ip[2][len]='\0';
			*(port+2) = atoi(tmp+1);
			break;
		case 'd':
			tmp = strstr(optarg, ":");
			len = tmp-optarg;
			strncpy(iptx, optarg, len);
			iptx[len]='\0';
			*txport = atoi(tmp+1);
			break;
		case 'e':
			strcpy(ipout[0], optarg);
		//	tmp = strstr(optarg, ":");
		//	len = tmp-optarg;
		//	strncpy(ipout[0], optarg, len);
		//	ipout[0][len]='\0';
			break;
		case 'g':
			strcpy(ipout[1], optarg);
		//	tmp = strstr(optarg, ":");
		//	len = tmp-optarg;
		//	strncpy(ipout[1], optarg, len);
		//	ipout[1][len]='\0';
			break;
		case 'f':
			strcpy(filename, optarg);
			break;
		case 'h':
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
			printf("To run the program as multipath sender:\n\n");
			printf("MPRTP_imp -a 192.168.10.10:4000 -b 192.168.12.30:4002 -c 192.168.14.50:4004 -e 192.168.14.50:4006 -g 192.168.14.50:4008 -f im2.rtp \n");
			printf("where -a, -b, -c - addresses for receiver \n");
			printf("where -e, -g - addresses for sender \n");
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");

			exit(0);
		default:
			printf("One or more invalid arguments\n");
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<SELAAB HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
			printf("To run the program as multipath sender:\n\n");
			printf("MPRTP_imp -a 192.168.10.10:4000 -b 192.168.12.30:4002 -c 192.168.14.50:4004 -f im2.rtp \n");
			printf("where -a, -b, -c - addresses for receiver \n");
			printf("where -e, -g - addresses for sender \n");
			printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");

			exit(1);
			break;
	}
   }
}

void signalHandler(int sig)
{
	assert(sig == SIGINT && !globalTerminationFlag);
	printf("Got signal %d. Terminating. \n", sig);
	globalTerminationFlag = true;
}

int64_t now() {
	struct timeval timenow;
	gettimeofday(&timenow, NULL);
	return (int64_t) timenow.tv_sec * 1000000 + timenow.tv_usec;
}

void MPRTP_update_subflow(struct mprtphead * mrheader, char * buf, int x)
{
	path_status.at(x)->seq_num = ntohs(mrheader->subflowseq);

	//	y = ntohs(mrheader->subflowseq);
	/*set x bit*/
	buf[0] = buf[0] | EXT_MASK;
	fprintf(log_all, "MPRTP Subflowseq for path %d = %" PRIu16 "\n", x, path_status.at(x)->seq_num);
	memcpy(buf + 12, mrheader, sizeof(struct mprtphead));
	path_status.at(x)->seq_num++;
	mrheader->subflowseq = htons(path_status.at(x)->seq_num);
	printf("Inscrease subflowseq %" PRIu16 "\n", ntohs(mrheader->subflowseq));
//	mrheader->subflowseq = htons(path_status.at(x)->seq_num++);
}

void insert_data_to_list(struct rtppacket * packet)
{
	if (pthread_mutex_lock(&list_mutex) != 0)
	{
		throw std::runtime_error("Mutex lock failed");
		perror("Scheduler : inserting data to list ");
	}
	int64_t t = now()%10000000000;
	packets_to_send.push_back(packet);
	fprintf(log_all,"%" PRId64 " Push packet to the list %" PRIu16 " \n", t, packet->seq);

	if (pthread_mutex_unlock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");
}

int create_threads(int n, char ipnew[][20], int * txportnew, char ipout[][20])
{
//	char ipnew[2][20] = { "130.149.49.28", "130.149.49.28"};
//	int txportnew[2] = { 49155, 49157 };
	for (int i = 0; i < n; i++)
	{
		strncpy(path_status[i]->ip, ipnew[i], 20);
		strncpy(path_status[i]->ipout, ipout[i], 20);

		printf("interface is %d, IP is %s\n", i, path_status[i]->ip);
		path_status[i]->allread = 0;
		path_status[i]->txport = txportnew[i];
		path_status[i]->obytes = 0;
		path_status[i]->rtt = -1.0;
		path_status[i]->Bps = -1.0;
		path_status[i]->loss_rate = -1.0;

		printf("Port: %d \n", txportnew[i]);

		if (pthread_mutex_init(&path_status[i]->rtp_mutex, NULL) != 0) // Initialize mutex for RTP thread
				{
			perror("mutex init failed");
			return 0;
		}
		if (pthread_cond_init(&path_status.at(i)->rtp_cond, NULL) != 0) // Initialize conditional variable for RTP thread
				{
			perror("cond init failed");
			return 0;
		}
		/*	if(pthread_mutex_init(&rtcpsr.rtcpsr_mutex,NULL)!=0)  // Initialize mutex for RTCP SR structure
		 throw std::runtime_error("rtcpsr_mutex  initialization failed");
		 */
		if (pthread_cond_init(&path_status.at(i)->rtcp_cond, NULL) != 0) // Initialize conditional variable for RTP thread
		{
			perror("cond init failed");
			return 0;
		}
		if ((pthread_create(&threadrtp[i], NULL, rtp_send, (void *) i)) != 0)
		{
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		}
		else
			printf("RTP thread created!!! \n");
		if ((pthread_create(&threadrtcpr[i], NULL, recv_rtcp, (void *) i)) != 0)
		{
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		}
		else
			printf("RTCPR recv thread created %d!!! \n", i);
		if ((pthread_create(&threadrtcps[i], NULL, send_rtcp, (void *) i)) != 0)
		{
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		}
		else
			printf("RTCPS send thread created %d!!! \n", i);
	}
	return 1;
}

int openfile(char *filename)
{
	struct stat statbuf;
//	char filename[255] = "/home/streaming/MPRTP/output_dump1";

	int fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		printf("Error opening file : %s\n", strerror(errno));
	}
	else
	{
		if (fstat(fd, &statbuf) != 0)
		{
			printf("Error fstat %d", errno);
			close(fd);
			return -1;
		}
	}

	/*seek to the end of #!rtpplay1.0 address/port\n in rtp file
	 * The next read operation will ensure only packets are read. */

	char tmp[50];
	if (read(fd, tmp, 1) != 1)
	{
		printf("Error while reading file \n");
		exit(0);
	}
	if (tmp[0] == '#')
	{
		while (1) {
			if (read(fd, tmp, 1) != 1)
			{
				printf("While reading file (length read: 1): %d, %s\n", errno,
						strerror(errno));
				break;
			}
			//	printf("char read %x\n", tmp[0] );
			if (tmp[0] == 0x0A)
				break;
		}

		if (read(fd, tmp, 16) != 16) {
			printf("While reading file (length read: 1): %d, %s\n", errno,
					strerror(errno));
		}

	}
	else
	{
		printf("YEYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY\n");
		if (lseek(fd, 0, SEEK_SET) < 0)
			printf("OH SHIT!!!\n");
	}
	return fd;
}

int readpacket(struct rtppacket *packet, int fd) {
	if (fd < 0) {
		printf("Read called without opening file\n");
		return 0;
	}

	int16_t len; /*length of packet*/
	if (read(fd, &len, 2) != 2) {
		printf("While reading file : %d, %s\n", errno, strerror(errno));
		return 0;
	}

	char tmp[20];
	if (read(fd, tmp, 2) != 2) {
		printf("While reading file : %d, %s\n", errno, strerror(errno));
		return 0;
	} else {
		//for (int i = 0; i < 7; i ++)
		fprintf(stup,
				" %" PRIu16 "   %" PRIu16 "        %" PRIu16 "   %" PRIu16 "   %" PRIu16 "   %" PRIu16 "  \n",
				(uint16_t) tmp[0], (uint16_t) tmp[1], (uint16_t) tmp[2],
				(uint16_t) tmp[3], (uint16_t) tmp[4], (uint16_t) tmp[5]);
	}
	/*Let us use the timestamps of packet recording in rtpdump. They are measured in milliseconds since the start of recording*/
	if (read(fd, tmp + 2, 4) != 4) {
		printf("While reading file : %d, %s\n", errno, strerror(errno));
		return 0;
	}

	static int first_dump_ts = 0;
	packet->dump_ts = ntohl(*(uint32_t*) (tmp + 2));
	if (first_dump_ts == 0) {
		first_dump_ts = packet->dump_ts;
	}
	packet->dump_ts -= first_dump_ts;

	//convert from milisec into microsec
	packet->dump_ts = packet->dump_ts * 1000;

	//printf("dump_ts = %" PRIu32 " \n", packet->dump_ts);

	len = htons(len);
	packet->packetlen = len + 4; /*this includes the mprtp header*/
	packet->payloadlen = len - 20;

	if (read(fd, packet->buf, 12) != 12) //!!! read 12 byte from file which is pointed by fd to buf
			{
		printf("While reading file (length read: %d): %d, %s\n", packet->packetlen, errno, strerror(errno));
		return 0;
	}
	/*0 the mprtp header of 12 bytes*/
	memset(packet->buf + 12, 0, 12);
	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	if (read(fd, packet->buf + 24, packet->payloadlen) != packet->payloadlen)
	{
		printf("While reading file (length read: %d): %d, %s\n",
				packet->packetlen, errno, strerror(errno));
		return 0;
	}

	static int firstts = 0;
	packet->ts = ntohl(*(uint32_t*) (packet->buf + 4));

	if (firstts == 0)
	{
		firstts = packet->ts;
	}
	packet->ts -= firstts;
	//????????????
	*(int*) (packet->buf + 4) = htonl(packet->ts);

	/*Convert ts into sec????*/
	//We should divide time on 90000 because of RTP timestamp resolution
	packet->ts = (packet->ts * 1000000.0) / VIDEO_RATE;

	packet->seq = ntohs(*(uint16_t*) (packet->buf + 2));

	//	fprintf(log,"%d 	%d		%d \n", *packetlen, *seq, *ts);
	return 1;
}

void* rtp_send(void *arg)
{
	int x = (long) ((int *) arg);
	int64_t time1 = -1;
	int byte_sent = 0;

	//Initialize socket for rtp connection
	if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	int fds = UDP_Init( x, path_status.at(x)->ip, 55000 + (2 * x), path_status.at(x)->ipout);
	if (fds < 0)
	{
		printf("UDP socket initialize for RTP failed");
		pthread_exit(NULL);
	}

	//Initialize socket for rtcp connection
	path_status.at(x)->rtcp_sock = UDP_Init(x, path_status.at(x)->ip, 55000 + (2 * x) + 1, path_status.at(x)->ipout);

	//	SetnonBlocking(path_status.at(x)->rtcp_sock);

	if (path_status.at(x)->rtcp_sock < 0)
	{
		printf("UDP socket initialize for RTCP failed");
		pthread_exit(NULL);
	}

	if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	while (globalTerminationFlag == false)
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		struct rtppacket *p = NULL;

		while (p == NULL)
		{
			int64_t t1 = now()%10000000000;
			fprintf(log_all, "%" PRId64 " before FOR rtp_send \n",t1);

			for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end() && p == NULL; )
			{
				//If the number of path in (struct rtppacket *packet) is equal to the number of interface -> send the packet
				//	if ((*it)->path == x)

				int count_sent = std::count((*it)->path.begin(), (*it)->path.end(), SENT);
				int count_nsent = std::count((*it)->path.begin(), (*it)->path.end(), NSENT);

				fprintf(log_all, "RTP_send: #%d count_sent = %d, count_nsent = %d \n", (*it)->seq, count_sent, count_nsent);

				/*		if ((std::count((*it)->path.begin(), (*it)->path.end(), DELETE)) == NUM)
				 (*it)->erase = 1;*/

				if (count_sent == NUM || count_nsent != 0 || (*it)->erase == 0)
				{
					//	printf("Packet number %d path %d \n", (*it)->seq, x);
					if ((*it)->path.at(x) == SENT)
					{
						byte_sent = sending_rtp(x, fds, *it);
						(*it)->path.at(x) = DELETE;

						for (uint i = 0; i < (*it)->path.size(); i++)
						{
							if ((*it)->path.at(i) == NSENT)
								(*it)->path.at(i) = DELETE;

							fprintf(log_all, "!!!path.at(%d) = enum %d, seq = %d \n", x, (*it)->path.at(i), (*it)->seq);
							printf("!!!path.at(%d) = enum %d, seq = %d \n", x, (*it)->path.at(i), (*it)->seq);
						}
//!
			/*			if ((std::count((*it)->path.begin(), (*it)->path.end(),	DELETE)) == NUM)
						{
							fprintf(log_all, "Erase flag for packet # %d \n", (*it)->seq);
							(*it)->erase = 1;
						}*/

						p = *it;
					}
				}

				if ((std::count((*it)->path.begin(), (*it)->path.end(),	DELETE)) == NUM)
				{
					printf("Erase packet %d from the list by thread %d \n", p->seq, x);
					fprintf(log_all, "Erase packet %d from the list by thread %d \n", p->seq, x);
					p->erase = 1;
					packets_to_send.erase(it);
				}
				else
					++it;
			}
			int64_t t2 = now()%10000000000;
			fprintf(log_all, "%" PRId64 " after FOR rtp_send \n",t2);


			if (p == NULL)
			{
				int64_t t1 = now()%10000000000;
				fprintf(log_all, "%" PRId64 " before waiting in rtp_send \n",t1);
				pthread_cond_wait(&path_status.at(x)->rtp_cond, &list_mutex);
				int64_t t2 = now()%10000000000;
				fprintf(log_all, "%" PRId64 " after waiting in rtp_send, Send time in waiting = %" PRId64 " \n", t2, t2-t1);
			}
		}

		if (p->erase == 1)
		{
			fprintf(log_all, "%d Delete packet p #%d \n", x, p->seq);
			printf("%d Delete packet p #%d \n", x, p->seq);
			delete p;
		}

		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		path_status.at(x)->sender_oc += byte_sent; 	// counts in payload octets, e.t. in bytes
		path_status.at(x)->sender_pc += 1; 			// counts in quantity of packets (1,2,3,....)


		/*add information to path characteristics about the number of packets sent between two consecutive RR packets*/
		path_status.at(x)->rtp_int_count += 1;

		fprintf(log_all, "%d RTP count between two consecutive RTCP RR = %" PRId16 " \n", x, path_status.at(x)->rtp_int_count);

		fprintf(log_all, "%d %6d = seq %10" PRIu32 " = sender_oc, %5" PRIu32 " = sender_pc \n", x, p->seq, path_status.at(x)->sender_oc,
				path_status.at(x)->sender_pc);
		printf("%d %6d = seq %10" PRIu32 " = sender_oc, %5" PRIu32 " = sender_pc \n", x, p->seq, path_status.at(x)->sender_oc, path_status.at(x)->sender_pc);

		if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");


		// Call the rtcp send thread

		// How to initialize it????? what is the value????
		int avg_rtcp_size;
		fprintf(log_all, "avg_rtcp_size = %d \n", avg_rtcp_size);

		if (time1 == -1)
		{
			printf("st0 later = %" PRId64" \n", st0);
			time1 = st0;
			printf("time1 = %" PRId64" \n", time1);
		}

		int64_t fb_interval = rtcp_interval(i_senders * 2, i_senders, RTCP_BW, 1, 56/*RTCPSR+UDP+IP*/, &avg_rtcp_size, 1, 0.1);
		printf("fb_interval = %" PRId64" \n", fb_interval);

		int64_t time2 = now();

		printf("time2 = %" PRId64"\n", time2);

		printf("time2_int = %" PRId64"\n", time2 / 1000000);
		printf("time2_frac = %" PRId64"\n", time2 % 1000000);

		printf("t2-t1 = %" PRId64" thread %d \n", time2 - time1, x);

		//	if(((t2.tv_sec-t1.tv_sec)+((t2.tv_usec-t1.tv_usec)/1000000)) > fb_interval)
		if (time2 - time1 > fb_interval)
		{
			getrtcpsr(&rtcpsr, time2, x);
			fprintf(log_all, "Sending RTCP report \n");
			if (pthread_cond_signal(&path_status.at(x)->rtcp_cond) != 0)
				throw std::runtime_error("Pthread_cond_signal failed");

			time1 = now();
		}
	}
	return NULL;
}

void MPRTP_Init_subflow(struct mprtphead * mrheader, uint16_t x)
{
	mrheader->prospec = htons(RTP_H_EXTID);
	mrheader->wordlen = htons(2);
	mrheader->hextid = htons(MPR_H_EXTID);
	mrheader->len = htons(6);
//	mrheader->mprtype = htons(MPR_TYPE_SUBFLOW);
	mrheader->subflowid = htons(x);
	mrheader->subflowseq = htons(1);
	mrheader->mprtype = htons(MPR_TYPE_SUBFLOW);
}

void waiting(const int64_t wake) {
	/*int64_t time_now = now();
	 printf("time_now = %" PRId64 " \n", time_now);
	 // Calculate how much time gone from st0 until this ts
	 int64_t time_gone = time_now - st0;
	 if(wake > time_gone)
	 {
	 if(wake - time_gone < 1000000)
	 {
	 usleep(wake - time_gone);
	 }
	 else
	 {
	 sleep((wake - time_gone)/1000000);
	 time_now = now();
	 time_gone = time_now - st0;
	 if (wake > time_gone)
	 usleep(wake - time_gone);
	 }
	 }*/
	int64_t time_now = now();
//	printf("time_now = %" PRId64 " \n", time_now);
	if (wake > time_now)
	{
		if (wake - time_now < 1000000)
		{
//			printf("Waiting %" PRId64 " \n", wake - time_now);
			usleep(wake - time_now);
		}
		else
		{
			sleep((wake - time_now) / 1000000);
			time_now = now();
			if (wake > time_now)
				usleep(wake - time_now);
		}
	}
}

void *send_rtcp(void *arg)
{
	int x = (long) ((int *) arg);

	while (globalTerminationFlag == false)
	{
		pthread_cond_wait(&path_status.at(x)->rtcp_cond, &path_status.at(x)->rtcp_thread_mutex);
		struct sockaddr_in rtcpsender;

		memset((char *) &rtcpsender, 0, sizeof(rtcpsender));
		rtcpsender.sin_family = PF_INET;
		rtcpsender.sin_port = htons(path_status.at(x)->txport + 1);
		if (inet_aton((char *) path_status.at(x)->ip, &rtcpsender.sin_addr) == 0)
		{
			fprintf(stderr, "inet_aton() failed\n");
			pthread_exit(NULL);
		}
		int64_t t = now()%10000000000;
		if (sendto(path_status.at(x)->rtcp_sock, &rtcpsr, sizeof rtcpsr, 0, (struct sockaddr *) &rtcpsender, sizeof(rtcpsender)) < 0)
		{
			perror("rtcpsr send");
			pthread_exit(NULL);
		}
		else
		{
			printf(	"Send RTCP SR on path %d, rtp count = %" PRIu32 ", ts = %" PRIu32 " \n", x, rtcpsr.sender_pc, rtcpsr.rtpts);
			fprintf(log_all, "%" PRId64 " Send RTCP SR on path %d, rtp count = %" PRIu32 ", ts = %" PRIu32 " \n",t, x, rtcpsr.sender_pc, rtcpsr.rtpts);

			if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

		//	fprintf(log_rtp, "%d %10" PRIu64 " %10" PRIu32 " = sender_oc, %5" PRIu32 " = sender_pc \n", x, t, path_status.at(x)->sender_oc, path_status.at(x)->sender_pc);

			if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
	}

	return NULL;
}

int getrtcpsr(struct rtcpsrbuf * rtcpsr, int64_t time2, int x)
{
	if (pthread_mutex_lock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	rtcpsr->b1 = RTCP_SR_B1;
	rtcpsr->b2 = RTCP_SR_B2;
	rtcpsr->len = htons(28);
	rtcpsr->ssrc = 0xaaaabbbb;

	uint32_t ntpts = time2 / 1000000;
	rtcpsr->ntpts = htonl(ntpts);

	int32_t tmp = time2 % 1000000 / 1e6 * (65536);

	int32_t tmp1 = tmp << 16;	// !!! It is equal to multiplication by 65536

	rtcpsr->ntpts_frac = htonl(tmp1);

	printf("ntps_frac = %" PRIu32 " \n", rtcpsr->ntpts_frac);

	rtcpsr->rtpts = 0;

	if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	rtcpsr->sender_oc = path_status.at(x)->sender_oc;
	rtcpsr->sender_pc = path_status.at(x)->sender_pc;
	printf("sender_oc in rtcpsr = %" PRIu32 " \n", rtcpsr->sender_oc);

	if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	if (pthread_mutex_unlock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	if (pthread_mutex_lock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");
	fprintf(log_rtcp_s, "%d  %" PRId64 "   %10" PRIu32 " %10" PRIu32 " \n", x,
			time2, rtcpsr->sender_oc, rtcpsr->sender_pc);
	printf("%d  %" PRId64 "   %10" PRIu32 " %10" PRIu32 " \n", x, time2,
			rtcpsr->sender_oc, rtcpsr->sender_pc);

	if (pthread_mutex_unlock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	printf(" Time of sending RTCP Report %" PRId64 " : %" PRId64 " \n",
			(int64_t) rtcpsr->ntpts, (int64_t) rtcpsr->ntpts_frac);
	return 28;
}

void* recv_rtcp(void *arg)
{
	int x = (long) ((int *) arg);
	printf("x = %d \n", x);

	struct sockaddr_in reciever;
	struct rtcprrbuf rtcprr;
	socklen_t reciever_size;
	struct rtcprrbuf rtcpbuf;

	printf("path_status.at(x)->rtcp_sock = %d \n", path_status.at(x)->rtcp_sock);

	while (path_status.at(x)->rtcp_sock == 0)
		usleep(100000);

	int sock = path_status.at(x)->rtcp_sock;

	/*check for rtcp rr*/

	while (globalTerminationFlag == false)
	{
		int64_t time1 = now()%10000000000;
//		fprintf(log_all, "%" PRId64 " Time of receiving RR before select \n", time1);

		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(sock, &rset);

		int sel = select(sock + 1, &rset, NULL, NULL, NULL);

		if (sel == -1)
		{
			perror("error in select!");
			throw std::runtime_error("Bad");
		}
		else
		{
			//	rtcpbuf = new char[BUFLEN];
			if (FD_ISSET(sock, &rset))
			{
				reciever_size = sizeof reciever;
				printf("Waiting for RCTP RR packet \n");

				int datalen = recvfrom(sock, &rtcpbuf, 100, 0, (struct sockaddr *) &reciever, &reciever_size);
				if (datalen <= 0)
				{
					perror("rtcp recv");
					throw std::runtime_error("Bad");
				}
				else
				{
					if (!(unsigned char) rtcpbuf.b1 ^ RTCP_RR_B2)
					{
						printf("RTCP RR received...\n");

						int64_t time2 = now()%10000000000;;
						printf("%d  %" PRId64 " Time of receiving RR after select \n", x, time2);
						fprintf(log_all, "%d Time of receiving RR after select %" PRId64 "\n", x, time2);
						fprintf(log_all, " Difference %" PRId64 "\n", time2-time1);

						//fprintf(log_rtcp_r,"%d" , datalen);
						printf("datalen = %d \n", datalen);
						RTCP_RR_flag = 1;
						rtcprr_list(&rtcprr, &rtcpbuf, x);
						path_status.at(x)->rrcount++;

						rtcprr.rtp_int_sent = path_status.at(x)->rtp_int_count;

						printf("%d rtp_int_sent = %d \n", x, rtcprr.rtp_int_sent);

						if (pthread_mutex_lock(&rtcprr_mutex) != 0)
							throw std::runtime_error("Mutex lock failed");

						rtcp_rr_packets.at(x).push_back(&rtcprr);

						if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
							throw std::runtime_error("Mutex unlock failed");

						path_status.at(x)->rtp_int_count = 0;
						fprintf(log_all, "%d rrcount in recv_rtcp %d \n", x, path_status.at(x)->rrcount);
					}
				}
			}
			else
				printf("Don't see handle of a socket!!!");
		}
	}
	return NULL;
}

void rtcprr_list(struct rtcprrbuf * rtcprr, struct rtcprrbuf * buf, int x)
{
	int64_t rrtime = 0;
	rrtime = now();
	fprintf(log_all,"rrtime before = %" PRId64" \n", rrtime);
	p_rrtime = rrtime;
	rtcprr->rrtime = rrtime;
	uint16_t rrtime_int = (uint16_t) (rrtime / 1000000);

	rrtime = (int64_t) rrtime_int * 1000000 + rrtime % 1000000;

	fprintf(log_all,"rrtime = %" PRId64" \n", rrtime);
	fprintf(log_all,"rrtime_int = %" PRIu16" \n", rrtime_int);

	rtcprr->len = ntohs(buf->len);
	rtcprr->ssrc = ntohl(buf->ssrc);
	rtcprr->ssrc1 = ntohl(buf->ssrc1);
	rtcprr->fraclost = buf->fraclost;
	rtcprr->totallost = buf->totallost;
	rtcprr->ehsn = ntohl(buf->ehsn);
	rtcprr->jitter = ntohl(buf->jitter);
	rtcprr->lsr = ntohs(buf->lsr);
	rtcprr->lsr_frac = ntohs(buf->lsr_frac);
	rtcprr->dlsr = ntohs(buf->dlsr);
	rtcprr->dlsr_frac = ntohs(buf->dlsr_frac);

	printf("Len of RTCP = %" PRIu32 " \n", rtcprr->len);

	// calculate RTT in microsec

	int64_t lsr = (int64_t) rtcprr->lsr_frac * 1000000 / 65536 + (int64_t) rtcprr->lsr * 1000000;
	int64_t rtt = rrtime - (int64_t) rtcprr->dlsr * 1000000 - (int64_t) rtcprr->dlsr_frac * 1000000 / 65536 - lsr;
	path_status.at(x)->loss_rate = rtcprr->totallost;

	printf("rtt = %" PRId64 " \n", rtt);

	//calculate PLR
	double lossrate = ((double) (rtcprr->fraclost)) / 256;

	double payload = ((double) path_status.at(x)->sender_oc - (double) path_status.at(x)->lastsender_oc) * 8;

	/*	double payload = ((double) path_status.at(x)->sender_oc - (double) path_status.at(x)->lastsender_oc) * 8 /
	 ((double) path_status.at(x)->sender_pc - (double) path_status.at(x)->lastsender_pc);*/

	printf("fracloss = %f \n", (double) rtcprr->fraclost);
	printf("lossrate = %f \n", lossrate);

	fprintf(log_rtcp_r,
			"% d  %" PRId64 " %10" PRIu32 " %10f %" PRId32 "   %10" PRIu32 " %20" PRIu16 ":%" PRIu16 " %10" PRIu16 ":%" PRIu16 " %10" PRIu32 "\n",
			x, rrtime, rtcprr->ehsn, lossrate, (int32_t) rtcprr->totallost,
			rtcprr->jitter, rtcprr->lsr, rtcprr->lsr_frac, rtcprr->dlsr,
			rtcprr->dlsr_frac, path_status.at(x)->sender_pc);

	if (path_status.at(x)->rrcount == 0)
	{
		fprintf(path_par,
				"%5d %10" PRId64 " %9.6f %17s %14f %14f %14f %18" PRId64 " %10s %7" PRIu32 " %7" PRIu32 " %7s %7" PRId32 " %10f \n",
				x, rtt, lossrate, "0", payload,
				(double) path_status.at(x)->sender_oc * 8,
				(double) path_status.at(x)->lastsender_oc * 8, p_rrtime, "0",
				rtcprr->ehsn, path_status.at(x)->last_ehsn, "0",
				(int32_t) rtcprr->totallost, 1-lossrate);
	}

	if (path_status.at(x)->rrcount > 0) {
		// Calculate Bandwidth on a path
		double delta_t = ((double) p_rrtime - (double) path_status.at(x)->last_rrtime) / 1000000;
		printf("p_rrtime = %" PRId64 " \n", p_rrtime);
		printf("last_rrtime = %" PRId64 " \n", path_status.at(x)->last_rrtime);
		printf("delta_t = %f \n", delta_t);

	//	double Bnd = (payload * ((double) rtcprr->ehsn - (double) path_status.at(x)->last_ehsn)) / delta_t;
		double Bnd = payload * (1-lossrate) / delta_t;
		printf("EHSN = %f \n", (double) rtcprr->ehsn - (double) path_status.at(x)->last_ehsn);

		uint32_t dif_pc = path_status.at(x)->sender_pc
				- path_status.at(x)->lastsender_pc;

		printf("%d Bnd = %f, sender_oc (%f), lastsender_oc (%f), sender_pc (%f), last_sender_pc (%f), ehsn(%" PRIu32 ") \n",
						x, Bnd, (double) path_status.at(x)->sender_oc,
						(double) path_status.at(x)->lastsender_oc,
						(double) path_status.at(x)->sender_pc,
						(double) path_status.at(x)->lastsender_pc, rtcprr->ehsn);

		fprintf(log_all, "%d Bnd = %f, sender_oc (%f), lastsender_oc (%f), sender_pc (%f), last_sender_pc (%f), ehsn(%" PRIu32 ") \n",
				x, Bnd, (double) path_status.at(x)->sender_oc,
				(double) path_status.at(x)->lastsender_oc,
				(double) path_status.at(x)->sender_pc,
				(double) path_status.at(x)->lastsender_pc, rtcprr->ehsn);

		fprintf(log_all, "Totallost = %" PRId32 ", fraclost = %f  \n", (int32_t) rtcprr->totallost, (double) rtcprr->fraclost);

		fprintf(path_par, "%5d %10" PRId64 " %9.6f %17f %14f %14f %14f %18" PRId64 " %10f %7" PRIu32 " %7" PRIu32 " %7" PRId32 " %7" PRId32 " %10f\n",
				x, rtt, lossrate, Bnd, payload,
				(double) path_status.at(x)->sender_oc * 8,
				(double) path_status.at(x)->lastsender_oc * 8, p_rrtime,
				delta_t, rtcprr->ehsn, path_status.at(x)->last_ehsn, dif_pc,
				rtcprr->totallost, 1-lossrate);
	}
	// save last received characteristics
	path_status.at(x)->last_rrtime = p_rrtime;
	path_status.at(x)->lastsender_oc = path_status.at(x)->sender_oc;
	path_status.at(x)->lastsender_pc = path_status.at(x)->sender_pc;
	path_status.at(x)->last_ehsn = rtcprr->ehsn;
}

int UDP_Init(int x, char * ip, int localport, char * ipout)
{
	int s;
	struct sockaddr_in mpaddr1;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		perror("socket");
		return -1;
	}
	if (s > 0)
	{
		printf("UDP socket has been initialized %s:%d\n", ip, localport);
	}
	//BIND
	mpaddr1.sin_family = AF_INET;
	mpaddr1.sin_port = htons(localport);
	if(inet_aton(ipout, &mpaddr1.sin_addr) == 0)
		perror("Cannot get an out ip address");
//	mpaddr1.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (struct sockaddr*) &mpaddr1, sizeof(mpaddr1)) < 0)
		perror("bind failed");
	return s;
}

int sending_rtp(int x, int fds, struct rtppacket * packet)
{
	struct sockaddr_in udpserver;

	memset((char *) &udpserver, 0, sizeof(udpserver));
	udpserver.sin_family = PF_INET;
	udpserver.sin_port = htons(path_status[x]->txport);

	if (inet_aton((char *) path_status[x]->ip, &udpserver.sin_addr) == 0)
	{
		fprintf(stderr, "inet_aton() failed\n");
		pthread_exit(NULL);
	}

	fprintf(log_all, "subflowseq before update = %" PRIu16 " \n", mrheader.at(x)->subflowseq);
	fprintf(log_all,"ID flow = %" PRIu16 " \n", ntohs(mrheader.at(x)->subflowid));
	MPRTP_update_subflow(mrheader.at(x), packet->buf, x);

	int64_t sent_time = now();

	/* We can use sendto in non-blocking way for rescheduling information for another path (using MSG_DONTWAIT flag)
	 * (Don't forget to check the return value after that!)*/

	int bytes_sent = sendto(fds, packet->buf, packet->packetlen, MSG_DONTWAIT, (struct sockaddr *) &udpserver, sizeof(udpserver));
//	int64_t spent_time = now();

	printf("bytes_sent = %d, seq = %d \n", bytes_sent, packet->seq);
	fprintf(log_all, "bytes_sent = %d, seq = %d \n", bytes_sent, packet->seq);
	if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EMSGSIZE))
	{
		printf("Buffer is overloaded...\n");
		fprintf(log_all, "Buffer is overloaded...\n");
		bytes_sent = packet->packetlen;
		printf("bytes_sent = packetlen = %d \n", bytes_sent);
		//	return 0;
	}

	fprintf(log_rtp, "%4d %6d %10d %10" PRIu32 " %16" PRIu64 " %16" PRIu16 "\n", x, packet->packetlen, packet->seq, packet->dump_ts, sent_time, path_status.at(x)->seq_num-1);

	return bytes_sent;
}

void SetnonBlocking(int s)
{
	int opts;
	opts = fcntl(s, F_GETFL); //!!! Get the file access mode and the file status flags
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		exit(1);
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(s, F_SETFL, opts) < 0)
	{
		perror("fcntl(F_SETFL)");
		exit(1);
	}
	return;
}

vector<path_t> path_select(const vector<path_t>& lastpath)
{
	if (pthread_mutex_lock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	thread_path = lastpath;
	int ts = -1;

	for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
	{
		int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

		if (count != NUM)
		{
		//	fprintf(log_all, "packet #%d already scheduled \n", (*it)->seq);
			continue;
		}
		else
		{
			//check if it is the first packet of Frame
			if(ts == -1)
				ts = (*it)->ts;
		}

		if (ts == (*it)->ts)
		{

			if (lastpath.at(0) == SENT)
			{
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(1) = SENT;
				(*it)->path.at(0) = NSENT;
			}
			else
			{
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(0) = SENT;
				(*it)->path.at(1) = NSENT;
			}

			thread_path = (*it)->path;

			for (uint i = 0; i < thread_path.size(); i++)
			{
				if (thread_path.at(i) == SENT)
				{
					fprintf(log_path, "%d 	%d		%d \n", i, (*it)->seq, (*it)->ts);
					fprintf(log_all, "%d 	%d		was scheduled ROUND ROBIN \n", i, (*it)->seq);
				}
			}
		}
		else
			break;

#if 0
		if ((*it)->path == -1)
		{
			(*it)->path = (lastpath == 1) ? 0 : 1; //If (lastpath == 1) = TRUE , return 0, otherwise 1
			printf("Packet # %d was selected for path # %d \n", (*it)->seq, (*it)->path);
			z = (*it)->path;

			fprintf(log_path, "%d 	%d		%d \n", z, (*it)->seq, (*it)->ts);
			//	fprintf(log_path,"%d 	%d		%d \n", z, (*it)->seq, (*it)->ts);
		}
#endif
	}

	if (pthread_mutex_unlock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

		return thread_path;
}

int64_t rtcp_interval(int members, int senders, double rtcp_bw, int we_sent,
		int packet_size, int *avg_rtcp_size, int initial,
		double rtcp_min_time)
{
	rtcp_min_time = 0.5;
	/*
	 * Minimum time between RTCP packets from this site (in seconds).
	 * This time prevents the reports from `clumping' when sessions
	 * are small and the law of large numbers isn't helping to smooth
	 * out the traffic.  It also keeps the report interval from
	 * becoming ridiculously small during transient outages like a
	 * network partition.
	 */
	/*
	 * Fraction of the RTCP bandwidth to be shared among active
	 * senders.  (This fraction was chosen so that in a typical
	 * session with one or two active senders, the computed report
	 * time would be roughly equal to the minimum report time so that
	 * we don't unnecessarily slow down receiver reports.) The
	 * receiver fraction must be 1 - the sender fraction.
	 */
	double const RTCP_SENDER_BW_FRACTION = 0.25;
	double const RTCP_RCVR_BW_FRACTION = (1 - RTCP_SENDER_BW_FRACTION);
	/*
	 * Gain (smoothing constant) for the low-pass filter that
	 * estimates the average RTCP packet size (see Cadzow reference).
	 */
	double const RTCP_SIZE_GAIN = (1. / 16.);

	double t; /* interval */
	int n; /* no. of members for computation */

	/*
	 * Very first call at application start-up uses half the min
	 * delay for quicker notification while still allowing some time
	 * before reporting for randomization and to learn about other
	 * sources so the report interval will converge to the correct
	 * interval more quickly.  The average RTCP size is initialized
	 * to 128 octets which is conservative (it assumes everyone else
	 * is generating SRs instead of RRs: 20 IP + 8 UDP + 52 SR + 48
	 * SDES CNAME).
	 */
	if (initial)
	{
		rtcp_min_time /= 2;
		*avg_rtcp_size = 128;
	}
	/*
	 * If there were active senders, give them at least a minimum
	 * share of the RTCP bandwidth.  Otherwise all participants share
	 * the RTCP bandwidth equally.
	 */
	n = members;
	if (senders > 0 && senders < members * RTCP_SENDER_BW_FRACTION)
	{
		if (we_sent) {
			rtcp_bw *= RTCP_SENDER_BW_FRACTION;
			n = senders;
		} else {
			rtcp_bw *= RTCP_RCVR_BW_FRACTION;
			n -= senders;
		}
	}
	/*
	 * Update the average size estimate by the size of the report
	 * packet we just sent.
	 */
	*avg_rtcp_size += (packet_size - *avg_rtcp_size) * RTCP_SIZE_GAIN;

	/*
	 * The effective number of sites times the average packet size is
	 * the total number of octets sent when each site sends a report.
	 * Dividing this by the effective bandwidth gives the time
	 * interval over which those packets must be sent in order to
	 * meet the bandwidth target, with a minimum enforced.  In that
	 * time interval we send one report so this time is also our
	 * average time between reports.
	 */
	t = (*avg_rtcp_size) * n / rtcp_bw;
	if (t < rtcp_min_time)
		t = rtcp_min_time;

	/*
	 * To avoid traffic bursts from unintended synchronization with
	 * other sites, we then pick our actual next report interval as a
	 * random number uniformly distributed between 0.5*t and 1.5*t.
	 */
	//	printf("FB INTERVAL:  %f = (avg_rtcp_size) %d * (no. of members for computation) %d / (rtcp_bw) %f (rtcp_min_time) [%f]\n", t, *avg_rtcp_size, n, rtcp_bw, rtcp_min_time);
	double probe = t * (drand48() + 0.5);
	printf("probe fb_interval = %f \n", probe);

	return probe * 1000000;
}

/*Functions for path scheduling */

vector<path_t> path_scheduling(const vector<path_t>& lastpath)
{
	thread_path = lastpath;
	int ts = -1;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0)
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

			if (count != NUM)
			{
		//		fprintf(log_all, "packet #%d already scheduled \n", (*it)->seq);
				continue;
			}
			else
			{
				//if it is the first packet of Frame
				if(ts == -1)
					ts = (*it)->ts;
			}

			fprintf(log_all, "ts of the frame in scheduling %d \n", ts);
			if (ts == (*it)->ts)
			{
				for (uint i = 0; i < (*it)->path.size(); i++)
				{
					(*it)->path.at(i) = SENT;
					thread_path.at(i) = SENT;
					printf("!!!%d 	%d		%d \n", i, (*it)->seq, (*it)->ts);
					fprintf(log_path, "!!!%d 	%d		%d \n", i, (*it)->seq, (*it)->ts);
					int64_t t = now()%10000000000;
					fprintf(log_all, "%" PRId64 " Scheduled %7d %7d %16d \n",t, i, (*it)->seq, (*it)->ts);
				}
			}
			else
				break;

		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
	else
	{
//		path_smart_select(lastpath);
		thread_path = path_select(lastpath);
	}
	return thread_path;
}

#if 0
vector <path_t> path_smart_select (const vector <path_t>& lastpath)
{
	vector <uint8_t> losses(NUM, 0);
	vector <int> sched_count(NUM, 0);

	if (pthread_mutex_lock(&rtcprr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	for (uint i = 0; i < rtcp_rr_packets.size(); i++)
	{
		printf("path_smart_select for a list #%u \n", i);

		if(rtcp_rr_packets.at(i).empty() == 0)
		{
			//	losses.at(i) = rtcp_rr_packets.at(i).back()->fraclost;
			printf("losses for path %d = " PRIu8 " \n", i);

			for(int j = 0; j < losses.size(); j++)
			{
				if (rtcp_rr_packets.at(i).back()->fraclost > losses.at(j) && losses.at(j) == 0)
				{
					losses.at(i) = rtcp_rr_packets.at(i).back()->fraclost;
					sched_count.at(i) = rtcp_rr_packets.at(i).back()->rtp_int_sent / 10; 				//take 10% from the packets sent between two consecutive packets
					fprintf(log_all,"Smart scheduling SCHED_COUNT(%d) = %d \n", i, sched_count.at(i));
					printf("Smart scheduling SCHED_COUNT(%d) = %d \n", i, sched_count.at(i));
				}
			}
		}
		else
		{
			fprintf(log_all, "RTCP RR LIST is empty \n");
			printf( "RTCP RR LIST is empty \n");
		}
	}

	if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");


}
#endif
