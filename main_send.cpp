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
#include <numeric>
#include <utility>
#include <iterator>

#include<signal.h>

#include <list>
using std::list;
#include <vector>
using std::vector;

#include "main_send.h"

#define VIDEO_RATE 90000
#define alpha 0.8
#define MTU 1492				//define MTU size of a network
#define FPS 3					//reflects the quantity of Frame Per Sec layers of a video
#define LAYERS 4				//reflects the quantity of layers in a video sequence including base layer
#define MPRTP_HEADER 12			//Size of MPRTP header

//#define Media_Rate 500000.0
#define RTCP_BW 0.05*2097152/8 /*2Mbps video*/

#define ROUND_ROBIN 0
#define PACKETS 0
#define FRAMES 0
#define SMART 1
#define TRACING 0

pthread_mutex_t list_mutex;
pthread_mutex_t rtcpsr_mutex;
pthread_mutex_t rtcprr_mutex;
pthread_mutex_t frame_mutex;
pthread_mutex_t media_mutex;
pthread_t threadrtp[2], threadrtcpr[2], threadrtcps[2];

vector<struct status *> path_status;
vector<struct mprtphead *> mrheader;

//list<struct rtcprrbuf *> rtcp_rr_packets;
std::vector<list<struct rtcprrbuf *> > rtcp_rr_packets(NUM); //Create a vector of lists of structures for rtcprr packets
std::vector<std::pair<double, int> > AvB(NUM, std::make_pair(0.0, 0)); //Create a vector for Average Bandwidth values. Each element of a vector is a pair (bandwidth, path)

std::vector<enum path_t> thread_path(NUM, INIT);
std::vector<enum path_t> lastpath(NUM, INIT);
std::vector<int64_t> tp(NUM, 0);
std::vector<std::vector<double> > Media_bitrate(LAYERS, std::vector<double>(FPS, 0));
std::pair<int, int> Media_index(LAYERS, FPS);

list<struct trace *> frame_list; //Create a list of frames (with different layers information) based of trace file
list<struct rtppacket *> packets_to_send;   //Create list of packets!!!!
struct rtcpsrbuf rtcpsr;
//struct rtcprrbuf rtcprr;
//struct rtcprrbuf *rtcprr = NULL;

FILE *log_path = NULL;
FILE *log_rtcp_s = NULL;
FILE *log_rtcp_r = NULL;
FILE *SB_log = NULL;
FILE *path_par = NULL;
FILE *log_rtp = NULL;
FILE *log_all = NULL;
FILE *log_quantity = NULL;

uint64_t seq = 0;
int64_t ts0 = -1;
int64_t st0 = -1;
int64_t fb_interval = 0;
int64_t sent_time = 0;
double simple = 0;

int i_senders;
int RTCP_RR_flag = 0;
int fdrtcp[NUM]; //descriptor for rtcp socket

bool globalTerminationFlag = false;

static int term_flag = 0;
static int64_t p_rrtime;

int main(int argc, char* argv[])
{
	int n = NUM;
	i_senders = n;
	char filename[255] = "";
	int port[NUM] = { 4000, 4002 };
	char ip[NUM][20] = { IP1, IP2 };
	char ipout[NUM][20] = { IP1, IP2 };
	char iptx[20] = { IPTX };
	int txport = 4402;
	int64_t time1 = 0;

	vector<int> counter(NUM, 0);
	std::vector<std::pair<double, int> > SB_total(NUM, std::make_pair(0.0, 0));
	std::vector<std::pair<double, int> > SB_total_sum(NUM, std::make_pair(0.0, 0));

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

	SB_log = fopen("/home/ekaterina/experiment/SB_file", "w");

	log_all = fopen("/home/ekaterina/experiment/log_all", "w");
	if (log_all == NULL)
		printf("Impossible to open or create txt log file for RTP!!!\n");

	// Create log file for monitor RTP packets
	log_rtp = fopen("/home/ekaterina/experiment/log_rtp_file", "w");
	if (log_rtp == NULL)
		printf("Impossible to open or create txt log file for RTP!!! \n");
	else
		fprintf(log_rtp, "%4s %6s %10s %10s %10s %16s \n", "Path", "Length", "Seq_No", "Frame_No"
				"ts", "send_time");

	log_quantity = fopen("/home/ekaterina/experiment/log_qu", "w");
	if (log_quantity == NULL)
		fprintf(log_all,"Impossible to open or create txt quantity!!!\n");
	else
		fprintf(log_quantity, "%s %8s %10s %10s \n", "Frame", "packets", "Real time", "timestamp");

	log_rtcp_r = fopen("/home/ekaterina/experiment/log_rtcp_r", "w");
	if (log_rtcp_r == NULL)
		printf("Impossible to open or create txt rtcp_r!!!\n");
	else
		fprintf(log_rtcp_r, "path     ts 		ehsn 	fraclost	totallost	jitter		LSR : LSR_FRAC		DLSR:DLSR_FRAC	      Packet_sent\n");

	//Create 3 threads for each interface: 1) for RTP; 2) RTCP send; 3)RTCP receive
	create_threads(n, ip, port, ipout);

	//Open trace file and put frames into frame list

#if TRACING

	// Open RTP file

	int fd = openfile(filename);
#else
//	 FILE fd = open_tracefile(filename);

#endif

	path_par = fopen("/home/ekaterina/experiment/path_parameters", "w");
	if (path_par == NULL)
		printf("Impossible to open or create txt rtcp_r!!!\n");

	else
		fprintf(path_par,
				"%5s %10s %9.6s %17s %14s %14s %14s %18s %10s %7s %7s %7s %7s\n",
				"path", "rtt", "PLR", "Bandwidth", "payload", "sender_oc",
				"last_sender_oc", "rrtime", "delta_t", "EHSN", "LAST_EHSN",
				"dif_pc", "total loss");

	log_rtcp_s = fopen("/home/ekaterina/experiment/log_rtcp_s", "w");

	if (log_rtcp_s == NULL)
		throw std::runtime_error("File creation failed");
	else
		fprintf(log_rtcp_s, "path	ts 		sender_oc 	sender_pc \n");

	log_path = fopen("/home/ekaterina/experiment/log_path_file", "w");
	if (log_path == NULL)
	{
		printf("Impossible to open or create txt log file for PATH!!!");
		return -1; // Exception!!!!
	}
	else
		fprintf(log_path, "%s %5s %5s %5s \n", "path", "seq_No", "frame_No", "ts");

#if TRACING

	while (term_flag == 0)
	{
		fprintf(log_all, "In the beginning of while loop \n");

		struct rtppacket *packet = NULL;

		while (allread == false)
		{
			packet = new struct rtppacket;
			memset(packet->buf, 0, sizeof(packet->buf));

			if (readpacket(packet, fd) == 0)
			{
				int64_t t = now() % 10000000000;
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
				time1 = st0;
			}
			totalbytes += packet->packetlen;
			//printf("Totalbytes %d was written to the list\n", totalbytes);
			fprintf(log_all, "st0 = %" PRId64 " \n", st0);

			if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

			if (!packets_to_send.empty() && packets_to_send.back()->ts != packet->ts)
			{
				if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

				insert_data_to_list(packet);
				break;
			}

			if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

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
				int64_t t2 = now() % 10000000000;
				const int64_t wake = ((int64_t) (*it)->dump_ts - ((ts0 * (int64_t) 1000000)) / (int64_t) VIDEO_RATE) + st0;
				fprintf(log_all, "%" PRId64 " wake = %" PRId64 ", seq = %" PRIu16 "\n", t2, wake % 10000000000, (*it)->seq);

				if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

				waiting(wake);
				int64_t t3 = now() % 10000000000;
				fprintf(log_all,"%" PRId64 " after waiting, really waiting = %" PRId64 " \n", t3, t3 - t2);

				if (pthread_mutex_lock(&list_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

				break;
			}
		}

		if (pthread_mutex_unlock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");
#else
	/* Read frames from trace file and push them to the frame_list*/

	readtrace(filename);

	while (term_flag == 0)
	{
		fprintf(log_all, "In the beginning of while loop \n");

		/* It is neccesary to ctreate packets for sending */
		create_packet();

		if (pthread_mutex_lock(&frame_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if(ts0 == -1)
		{
			ts0 = 0;
			st0 = now();
			time1 = st0;
		}

		for (std::list<struct trace *>::iterator it = frame_list.begin(); it != frame_list.end(); ++it)
		{
			if ((*it)->number == 0 || (*it)->location == NONALLOC)
			{
				fprintf(log_all, "st0 = %" PRId64 " \n", st0);
				fprintf(log_all, "t_frame = %" PRId64 " \n", (int64_t)((*it)->time*1000000));

				//Sleep until wake (in microsec)
				int64_t t2 = now();
				const int64_t wake = ((int64_t) ((*it)->time*1000000) - ((ts0 * (int64_t) 1000000)) / (int64_t) VIDEO_RATE) + st0;
				fprintf(log_all, "%" PRId64 " wake = %" PRId64 ", frame_seq = %" PRIu16 "\n", t2, wake, (*it)->number);

				if (pthread_mutex_unlock(&frame_mutex) != 0)
					throw std::runtime_error("Mutex unlock failed");

				waiting(wake);
				int64_t t3 = now();
				fprintf(log_all, "%" PRId64 " after waiting, really waiting = %" PRId64 " \n", t3, t3 - t2);

				if (pthread_mutex_lock(&frame_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

				break;
			}
		}

		if (pthread_mutex_unlock(&frame_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

#endif
		//Select the paths for data transmission

#if ROUND_ROBIN
		thread_path = path_select(lastpath);
#endif
		/*Scheduling for packets*/
#if PACKETS
		thread_path = packets_path_scheduling(lastpath);
#endif
#if FRAMES
		/*Scheduling for frames*/
		thread_path = path_scheduling(lastpath);
#endif
#if SMART
		/*Sending Bit rate calculation!!!!*/

		int64_t time2 = now();
		int64_t T_scheduler = fb_interval * (drand48() + 0.5) /** 1000000*/;
		fprintf(log_all, "T_scheduler = %" PRId64 "\n", T_scheduler);
		fprintf(log_all, "time2 (%" PRId64 ") - time1(%" PRId64 ") = %" PRId64 "\n", time2, time1, time2 - time1);

		/*Renew Sending Bit Rate values if recalculation interval is expired or we received the first RTCP RR packet*/
		if (time2 - time1 > T_scheduler || RTCP_RR_flag == 1)
		{
			/*Calculate Sending Bit rate for each path*/

			fprintf(log_all, "Time for recalculation of scheduler is expired \n");
			SB_total = SB_calculation();

			time1 = time2;
		}


		/*Allocate packet to an appropriate path*/
		thread_path = path_SB_scheduling(SB_total);
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
					int64_t t = now();
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

		/*For calculation a total average bandwidth for a path*/
		for (uint i = 0; i < SB_total.size(); i++)
		{
			if (path_status.at(i)->rrcount == 0 && path_status.at(i)->rrcount_prev == 0)
			{
				path_status.at(i)->rrcount_prev = -1;
				counter.at(i)++;
				SB_total_sum.at(SB_total_sum.at(i).second).first +=	SB_total.at(i).first;
				//	SB_total_sum.at(i).second = SB_total.at(i).second;
				fprintf(SB_log, "!!!%d %10f %d %d \n", SB_total_sum.at(i).second = SB_total.at(i).second,
						SB_total_sum.at(i).first, path_status.at(i)->rrcount, path_status.at(i)->rrcount_prev);
			}

			if (path_status.at(i)->rrcount != path_status.at(i)->rrcount_prev)
			{
				if (path_status.at(i)->rrcount != 0)
				{
					SB_total_sum.at(SB_total_sum.at(i).second).first += SB_total.at(i).first;
					counter.at(i)++;
					//	SB_total_sum.at(i).second = SB_total.at(i).second;
					fprintf(SB_log, "%d %10f %d \n", SB_total_sum.at(i).second = SB_total.at(i).second, SB_total_sum.at(i).first,
							path_status.at(i)->rrcount);
					path_status.at(i)->rrcount_prev = path_status.at(i)->rrcount;
				}
			}
		}

		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		fprintf(log_all, "Packets_to_send.empty = %d \n", packets_to_send.empty());

		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		///////////////////////////////////////////////////////////////////////////////////////////////////////
		/*Erase frame from a list after creating packets on the basis of it*/
		if (pthread_mutex_lock(&frame_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		std::list<struct trace *>::iterator it;
		for (it = frame_list.begin(); it != frame_list.end(); ++it)
		{
			if ((*it)->location == ALLOC)
			{
				fprintf(log_all, "Erase frame %" PRIu16 " from the list \n", (*it)->number);
				it = frame_list.erase(it);
			}
		}

		if (pthread_mutex_unlock(&frame_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		//////////////////////////////////////////////////////////////////////////////////////////////////////

		if (pthread_mutex_lock(&frame_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (packets_to_send.empty() == 1 && frame_list.empty() == 1)
		{
			if (pthread_mutex_unlock(&frame_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			term_flag = 1;
			fprintf(log_all, "term_flag = %d \n", term_flag);

			/*Remove all elements from lists of RTCP RR packets*/
			if (pthread_mutex_lock(&rtcprr_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			for (uint i = 0; i < rtcp_rr_packets.size(); i++)
			{
				rtcp_rr_packets.at(i).clear();
				fprintf(log_all, " Cleaned rtcp rr list #%d \n", i);
				printf("Cleaned rtcp rr list #%d \n", i);
			}

			if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
		else
		{
			if (pthread_mutex_unlock(&list_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

			if (pthread_mutex_unlock(&frame_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

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

	if (fclose(log_quantity) == 0)
		printf("Log_quantity file was successfully closed \n");
	else
		printf("Error during the procedure of closing Log_quantity file: %d \n",
		errno);

	if (fclose(log_all) == 0)
		printf("Log_all file was successfully closed \n");
	else
		printf("Error during the procedure of closing log_all file: %d \n",
		errno);

	if (term_flag == 1)
	{
		for (int i = 0; i < n; i++) {
			printf("Total number of packet sent to path %d = %d \n", i,
					path_status.at(i)->sender_pc);
			printf("Total lost for path %d = %f \n", i,
					path_status.at(i)->total_lossrate);
		}

		int64_t t_finish = now();
		printf("Total_time = %" PRId64 " \n", t_finish-st0);

#ifdef SMART

		for (uint i = 0; i < SB_total_sum.size(); i++)
		{
			SB_total_sum.at(i).first = SB_total_sum.at(i).first / counter.at(i);

			printf("Size of SB_total_sum for path %d = %u \n", i,
					counter.at(i));
			printf("Average sending bit rate for path %d = %f \n",
					SB_total_sum.at(i).second, SB_total_sum.at(i).first);
		}
#endif
	}

	return 0;
}

void getargs(int argc, char* argv[], char * filename, int * port, char ip[][20],
		char iptx[], int * txport, char ipout[][20]) {
	int c;
	int len;
	char * tmp;
	while ((c = getopt(argc, argv, "hp:a:b:c:d:e:g:f:")) != -1) {
		switch (c) {
		case 'a':
			tmp = strstr(optarg, ":");
			len = tmp - optarg;
			strncpy(ip[0], optarg, len);
			ip[0][len] = '\0';
			*port = atoi(tmp + 1);
			break;
		case 'b':
			tmp = strstr(optarg, ":");
			len = tmp - optarg;
			strncpy(ip[1], optarg, len);
			ip[1][len] = '\0';
			*(port + 1) = atoi(tmp + 1);
			break;
		case 'c':
			tmp = strstr(optarg, ":");
			len = tmp - optarg;
			strncpy(ip[2], optarg, len);
			ip[2][len] = '\0';
			*(port + 2) = atoi(tmp + 1);
			break;
		case 'd':
			tmp = strstr(optarg, ":");
			len = tmp - optarg;
			strncpy(iptx, optarg, len);
			iptx[len] = '\0';
			*txport = atoi(tmp + 1);
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
			printf(
					"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
			printf("To run the program as multipath sender:\n\n");
			printf(
					"MPRTP_imp -a 192.168.10.10:4000 -b 192.168.12.30:4002 -c 192.168.14.50:4004 -e 192.168.14.50 -g 192.168.14.50 -f im2.rtp \n");
			printf("where -a, -b, -c - addresses for receiver \n");
			printf("where -e, -g - addresses for sender \n");
			printf(
					"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");

			exit(0);
		default:
			printf("One or more invalid arguments\n");
			printf(
					"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
			printf("To run the program as multipath sender:\n\n");
			printf(
					"MPRTP_imp -a 192.168.10.10:4000 -b 192.168.12.30:4002 -c 192.168.14.50:4004 -e 192.168.14.50 -g 192.168.14.50 -f im2.rtp \n");
			printf("where -a, -b, -c - addresses for receiver \n");
			printf("where -e, -g - addresses for sender \n");
			printf(
					"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<MPRTP_imp HELP>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");

			exit(1);
			break;
		}
	}
}

void signalHandler(int sig) {
	assert(sig == SIGINT && !globalTerminationFlag);
	printf("Got signal %d. Terminating. \n", sig);
	globalTerminationFlag = true;
}

int64_t now()
{
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
	fprintf(log_all, "MPRTP Subflowseq for path %d = %" PRIu16 "\n", x,
			path_status.at(x)->seq_num);
	memcpy(buf + 12, mrheader, sizeof(struct mprtphead));
	path_status.at(x)->seq_num++;
	mrheader->subflowseq = htons(path_status.at(x)->seq_num);
	fprintf(log_all,"Inscrease subflowseq %" PRIu16 "\n", ntohs(mrheader->subflowseq));
//	mrheader->subflowseq = htons(path_status.at(x)->seq_num++);
}

void insert_data_to_list(struct rtppacket * packet)
{
	if (pthread_mutex_lock(&list_mutex) != 0)
	{
		throw std::runtime_error("Mutex lock failed");
		perror("Scheduler : inserting data to list ");
	}

	int64_t t = now() % 10000000000;
	packets_to_send.push_back(packet);
	fprintf(log_all, "%" PRId64 " Push packet to the list %" PRIu64 " \n", t, packet->seq);

	if (pthread_mutex_unlock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");
}

int create_threads(int n, char ipnew[][20], int * txportnew, char ipout[][20]) {
	for (int i = 0; i < n; i++) {
		strncpy(path_status.at(i)->ip, ipnew[i], 20);
		strncpy(path_status.at(i)->ipout, ipout[i], 20);

		printf("interface is %d, IP is %s\n", i, path_status.at(i)->ip);
		path_status.at(i)->allread = 0;
		path_status.at(i)->txport = txportnew[i];
		path_status.at(i)->obytes = 0;
		path_status.at(i)->total_lossrate = -1.0;

		printf("Port: %d \n", txportnew[i]);

		if (pthread_mutex_init(&path_status.at(i)->rtp_mutex, NULL) != 0) // Initialize mutex for RTP thread
				{
			perror("mutex init failed");
			return 0;
		}
		if (pthread_cond_init(&path_status.at(i)->rtp_cond, NULL) != 0) // Initialize conditional variable for RTP thread
				{
			perror("cond init failed");
			return 0;
		}

		if (pthread_mutex_init(&rtcpsr_mutex, NULL) != 0) // Initialize mutex for RTCP SR structure
			throw std::runtime_error("rtcpsr_mutex  initialization failed");

		if (pthread_mutex_init(&rtcprr_mutex, NULL) != 0) // Initialize mutex for RTCP SR structure
			throw std::runtime_error("rtcpsr_mutex  initialization failed");

		if (pthread_cond_init(&path_status.at(i)->rtcp_cond, NULL) != 0) // Initialize conditional variable for RTP thread
				{
			perror("cond init failed");
			return 0;
		}
		if ((pthread_create(&threadrtp[i], NULL, rtp_send, (void *) i)) != 0) {
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		} else
			printf("RTP thread created!!! \n");
		if ((pthread_create(&threadrtcpr[i], NULL, recv_rtcp, (void *) i))
				!= 0) {
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		} else
			printf("RTCPR recv thread created %d!!! \n", i);
		if ((pthread_create(&threadrtcps[i], NULL, send_rtcp, (void *) i))
				!= 0) {
			printf("RTP thread creation failed : %s \n", strerror(errno));
			return 0;
		} else
			printf("RTCPS send thread created %d!!! \n", i);
	}
	return 1;
}

int openfile(char *filename) {
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
	if (read(fd, tmp, 1) != 1) {
		printf("Error while reading file \n");
		exit(0);
	}

	if (tmp[0] == '#') {
		while (1) {
			if (read(fd, tmp, 1) != 1) {
				printf("While reading file (length read: 1): %d, %s\n", errno,
						strerror(errno));
				break;
			}
			if (tmp[0] == 0x0A)
				break;

		}

		if (read(fd, tmp, 16) != 16) {
			printf("While reading file (length read: 1): %d, %s\n", errno,
					strerror(errno));
		}

	} else {
		printf("YEYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY\n");
		if (lseek(fd, 0, SEEK_SET) < 0)
			printf("OH SHIT!!!\n");
	}
	return fd;
}

int readtrace(char *filename) {
	struct trace *frame = NULL;
	int allread = 0;

	std::vector<int> size(LAYERS * FPS, 0);
	std::vector<double> PSNRY(LAYERS * FPS, 0);
	std::vector<double> PSNRU(LAYERS * FPS, 0);
	std::vector<double> PSNRV(LAYERS * FPS, 0);

	FILE *fd = fopen("/home/ekaterina/experiment/tracefile", "r");

	if (fd == NULL) {
		printf("Read called without fd opening file\n");
		printf("While reading fd file : %d, %s\n", errno, strerror(errno));
		fclose(fd);
		return -1;
	}
	else
	{
		fprintf(log_all, "Opened trace file  \n");

		/* read from a trace file information*/
		while (allread == 0) {
			frame = new struct trace;
			memset(frame, 0, sizeof(frame));

			if (fscanf(fd,
					"%d %lf %s %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf"
							"%d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf %d %lf %lf %lf",
					&frame->number, &frame->time, frame->type, &size.at(0),
					&PSNRY.at(0), &PSNRU.at(0), &PSNRV.at(0), &size.at(1),
					&PSNRY.at(1), &PSNRU.at(1), &PSNRV.at(1), &size.at(2),
					&PSNRY.at(2), &PSNRU.at(2), &PSNRV.at(2), &size.at(3),
					&PSNRY.at(3), &PSNRU.at(3), &PSNRV.at(3), &size.at(4),
					&PSNRY.at(4), &PSNRU.at(4), &PSNRV.at(4), &size.at(5),
					&PSNRY.at(5), &PSNRU.at(5), &PSNRV.at(5), &size.at(6),
					&PSNRY.at(6), &PSNRU.at(6), &PSNRV.at(6), &size.at(7),
					&PSNRY.at(7), &PSNRU.at(7), &PSNRV.at(7), &size.at(8),
					&PSNRY.at(8), &PSNRU.at(8), &PSNRV.at(8), &size.at(9),
					&PSNRY.at(9), &PSNRU.at(9), &PSNRV.at(9), &size.at(10),
					&PSNRY.at(10), &PSNRU.at(10), &PSNRV.at(10), &size.at(11),
					&PSNRY.at(11), &PSNRU.at(11), &PSNRV.at(11)) != EOF) {

				//		fprintf(log_all,"frame_number = %d, frame_time = %f, frame_type = %s \n ", frame->number, frame->time, frame->type);

				uint s = 0;
				for (uint i = 0; i < frame->size.at(i).size(); i++)
				{
					for (uint j = 0; j < frame->size.size(); j++)
					{
						for (; s < size.size();)
						{
							/*Since dimension of all three arrays the same we will use the one loop for all*/
							frame->size.at(j).at(i) = size.at(s);
							frame->PSNRY.at(j).at(i) = PSNRY.at(s);
							frame->PSNRU.at(j).at(i) = PSNRU.at(s);
							frame->PSNRV.at(j).at(i) = PSNRV.at(s);

							s++;
							break;
						}
					}
				}

				frame->location = NONALLOC;

				if (pthread_mutex_lock(&frame_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

				if (!frame_list.empty() || frame_list.back()->time != frame->time)
				{
					if (pthread_mutex_unlock(&frame_mutex) != 0)
						throw std::runtime_error("Mutex unlock failed");

					insert_data_to_frame_list(frame);
				}
				else
				{
					if (pthread_mutex_unlock(&frame_mutex) != 0)
						throw std::runtime_error("Mutex unlock failed");
				}
			}
			else
			{
				fprintf(log_all, "The end of trace file. All data is read\n");
				fclose(fd);
				allread = 1;
			}
		}
	}
	return 0;
}

void insert_data_to_frame_list(struct trace * frame) {
	if (pthread_mutex_lock(&frame_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	frame_list.push_back(frame);

	if (pthread_mutex_unlock(&frame_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

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
		printf("While reading file (length read: %d): %d, %s\n",
				packet->packetlen, errno, strerror(errno));
		return 0;
	}
	/*0 the mprtp header of 12 bytes*/
	memset(packet->buf + 12, 0, 12);
	//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	if (read(fd, packet->buf + 24, packet->payloadlen) != packet->payloadlen)
	{
		fprintf(log_all,"While reading file (length read: %d): %d, %s\n", packet->packetlen, errno, strerror(errno));
		return 0;
	}

	static int firstts = 0;
	packet->ts = ntohl(*(uint32_t*) (packet->buf + 4));

	if (firstts == 0) {
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

	int fds = UDP_Init(x, path_status.at(x)->ip, 55000 + (2 * x), path_status.at(x)->ipout);
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
			int64_t t1 = now() % 10000000000;
			fprintf(log_all, "%" PRId64 " before FOR rtp_send \n", t1);

			for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end() && p == NULL;)
			{
				//If the number of path in (struct rtppacket *packet) is equal to the number of interface -> send the packet
				//	if ((*it)->path == x)

				int count_sent = std::count((*it)->path.begin(), (*it)->path.end(), SENT);
				int count_nsent = std::count((*it)->path.begin(), (*it)->path.end(), NSENT);

				if (count_sent == NUM || count_nsent != 0 || (*it)->erase == 0)
				{
					if ((*it)->path.at(x) == SENT)
					{
						byte_sent = sending_rtp(x, fds, *it);
						(*it)->path.at(x) = DELETE;

						for (uint i = 0; i < (*it)->path.size(); i++)
						{
							if ((*it)->path.at(i) == NSENT)
								(*it)->path.at(i) = DELETE;

							fprintf(log_all, "!!!path.at(%d) = enum %d, seq = %" PRIu64 " \n", x, (*it)->path.at(i), (*it)->seq);
						}
						p = *it;
					}
				}

				if ((std::count((*it)->path.begin(), (*it)->path.end(), DELETE)) == NUM)
				{
					fprintf(log_all, "Erase packet %" PRIu64 " from the list by thread %d \n", p->seq, x);
					p->erase = 1;
					packets_to_send.erase(it);
				}
				else
					++it;
			}
			int64_t t2 = now() % 10000000000;
			fprintf(log_all, "%" PRId64 " after FOR rtp_send \n", t2);

			if (p == NULL)
			{
				int64_t t1 = now() % 10000000000;
				fprintf(log_all, "%" PRId64 " before waiting in rtp_send \n", t1);
				pthread_cond_wait(&path_status.at(x)->rtp_cond, &list_mutex);
				int64_t t2 = now() % 10000000000;
				fprintf(log_all, "%" PRId64 " after waiting in rtp_send, Send time in waiting = %" PRId64 " \n", t2, t2 - t1);
			}
		}

		if (p->erase == 1)
		{
			fprintf(log_all, "%d Delete packet p #%" PRIu64 " \n", x, p->seq);
			delete p;
		}

		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		path_status.at(x)->sender_oc += byte_sent; // counts in payload octets, e.t. in bytes
		path_status.at(x)->sender_pc += 1; // counts in quantity of packets (1,2,3,....)

		/*add information to path characteristics about the number of packets sent between two consecutive RR packets*/
		path_status.at(x)->rtp_oc_count += byte_sent;

		fprintf(log_all, "%d RTP payload count between two consecutive RTCP RR = %" PRId16 " \n",x, path_status.at(x)->rtp_oc_count);

		fprintf(log_all, "%d  %10" PRIu32 " = sender_oc, %5" PRIu32 " = sender_pc \n", x,
				path_status.at(x)->sender_oc, path_status.at(x)->sender_pc);

		if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		// Call the rtcp send thread

		// How to initialize it????? what is the value????
		int avg_rtcp_size;
		fprintf(log_all, "avg_rtcp_size = %d \n", avg_rtcp_size);

		if (time1 == -1) {
			fprintf(log_all,"st0 later = %" PRId64" \n", st0);
			time1 = st0;
			fprintf(log_all,"time1 = %" PRId64" \n", time1);
		}

		fb_interval = rtcp_interval(i_senders * 2, i_senders, RTCP_BW, 1, 56/*RTCPSR+UDP+IP*/, &avg_rtcp_size, 1, 0.1);
		fprintf(log_all,"fb_interval = %" PRId64" \n", fb_interval);

		int64_t time2 = now();

/*		printf("time2 = %" PRId64"\n", time2);

		printf("time2_int = %" PRId64"\n", time2 / 1000000);
		printf("time2_frac = %" PRId64"\n", time2 % 1000000);
*/
		fprintf(log_all,"t2-t1 = %" PRId64" thread %d \n", time2 - time1, x);

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

void MPRTP_Init_subflow(struct mprtphead * mrheader, uint16_t x) {
	mrheader->prospec = htons(RTP_H_EXTID);
	mrheader->wordlen = htons(2);
	mrheader->hextid = htons(MPR_H_EXTID);
	mrheader->len = htons(6);
//	mrheader->mprtype = htons(MPR_TYPE_SUBFLOW);
	mrheader->subflowid = htons(x);
	mrheader->subflowseq = htons(1);
	mrheader->mprtype = htons(MPR_TYPE_SUBFLOW);
}

void waiting(const int64_t wake)
{
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

void *send_rtcp(void *arg) {
	int x = (long) ((int *) arg);

	while (globalTerminationFlag == false) {
		pthread_cond_wait(&path_status.at(x)->rtcp_cond,
				&path_status.at(x)->rtcp_thread_mutex);
		struct sockaddr_in rtcpsender;

		memset((char *) &rtcpsender, 0, sizeof(rtcpsender));
		rtcpsender.sin_family = PF_INET;
		rtcpsender.sin_port = htons(path_status.at(x)->txport + 1);
		if (inet_aton((char *) path_status.at(x)->ip, &rtcpsender.sin_addr)
				== 0) {
			fprintf(stderr, "inet_aton() failed\n");
			pthread_exit(NULL);
		}
		int64_t t = now() % 10000000000;
		if (sendto(path_status.at(x)->rtcp_sock, &rtcpsr, sizeof rtcpsr, 0,
				(struct sockaddr *) &rtcpsender, sizeof(rtcpsender)) < 0) {
			perror("rtcpsr send");
			pthread_exit(NULL);
		} else {
			fprintf(log_all,
					"%" PRId64 " Send RTCP SR on path %d, rtp count = %" PRIu32 ", ts = %" PRIu32 " \n",
					t, x, rtcpsr.sender_pc, rtcpsr.rtpts);

			if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			//	fprintf(log_rtp, "%d %10" PRIu64 " %10" PRIu32 " = sender_oc, %5" PRIu32 " = sender_pc \n", x, t, path_status.at(x)->sender_oc, path_status.at(x)->sender_pc);

			if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");
		}
	}

	return NULL;
}

int getrtcpsr(struct rtcpsrbuf * rtcpsr, int64_t time2, int x) {
	if (pthread_mutex_lock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	rtcpsr->b1 = RTCP_SR_B1;
	rtcpsr->b2 = RTCP_SR_B2;
	rtcpsr->len = htons(28);
	rtcpsr->ssrc = 0xaaaabbbb;

	uint32_t ntpts = time2 / 1000000;
	rtcpsr->ntpts = htonl(ntpts);

	int32_t tmp = time2 % 1000000 / 1e6 * (65536);

	int32_t tmp1 = tmp << 16; // !!! It is equal to multiplication by 65536

	rtcpsr->ntpts_frac = htonl(tmp1);

//	printf("ntps_frac = %" PRIu32 " \n", rtcpsr->ntpts_frac);

	rtcpsr->rtpts = 0;

	if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	rtcpsr->sender_oc = path_status.at(x)->sender_oc;
	rtcpsr->sender_pc = path_status.at(x)->sender_pc;
	fprintf(log_all,"sender_oc in rtcpsr = %" PRIu32 " \n", rtcpsr->sender_oc);

	if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	if (pthread_mutex_unlock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	if (pthread_mutex_lock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	fprintf(log_rtcp_s, "%d  %" PRId64 "   %10" PRIu32 " %10" PRIu32 " \n", x,
			time2, rtcpsr->sender_oc, rtcpsr->sender_pc);

	if (pthread_mutex_unlock(&rtcpsr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	fprintf(log_all," Time of sending RTCP Report %" PRId64 " : %" PRId64 " \n",
			(int64_t) rtcpsr->ntpts, (int64_t) rtcpsr->ntpts_frac);
	return 28;
}

void* recv_rtcp(void *arg) {
	int x = (long) ((int *) arg);

	struct sockaddr_in reciever;
	socklen_t reciever_size;
	struct rtcprrbuf rtcpbuf;

	struct rtcprrbuf *rtcprr = NULL;
	struct rtcprrbuf *p = NULL;

	printf("path_status.at(x)->rtcp_sock = %d \n",
			path_status.at(x)->rtcp_sock);

	while (path_status.at(x)->rtcp_sock == 0)
		usleep(100000);

	int sock = path_status.at(x)->rtcp_sock;

	/*check for rtcp rr*/

	while (globalTerminationFlag == false) {
		int64_t time1 = now() % 10000000000;
//		fprintf(log_all, "%" PRId64 " Time of receiving RR before select \n", time1);

		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(sock, &rset);

		int sel = select(sock + 1, &rset, NULL, NULL, NULL);

		if (sel == -1) {
			perror("error in select!");
			throw std::runtime_error("Bad");
		} else {
			//	rtcpbuf = new char[BUFLEN];
			if (FD_ISSET(sock, &rset)) {
				reciever_size = sizeof reciever;
				fprintf(log_all,"Waiting for RCTP RR packet \n");

				int datalen = recvfrom(sock, &rtcpbuf, 100, 0,
						(struct sockaddr *) &reciever, &reciever_size);
				if (datalen <= 0) {
					perror("rtcp recv");
					throw std::runtime_error("Bad");
				} else {
					if (!(unsigned char) rtcpbuf.b1 ^ RTCP_RR_B2) {
						fprintf(log_all, "RTCP RR received...\n");

						int64_t time2 = now() % 10000000000;

						fprintf(log_all, "%d Time of receiving RR after select %" PRId64 "\n", x, time2);
						fprintf(log_all, " Difference %" PRId64 "\n", time2 - time1);

						rtcprr = new struct rtcprrbuf;
						memset(rtcprr, 0, sizeof(rtcprr));

						rtcprr_list(rtcprr, &rtcpbuf, x);
						path_status.at(x)->rrcount++;

						fprintf(SB_log, "RRCOUNT = %d\n", path_status.at(x)->rrcount);

						/*Set RTCP_RR_flag to true after receiving the first RTCP packet*/
						if (path_status.at(x)->rrcount == 0)
							RTCP_RR_flag = 0;
						else {
							if (path_status.at(x)->rrcount == 1)
								RTCP_RR_flag = 1;
							else
								RTCP_RR_flag = 2;
						}

						if (pthread_mutex_lock(&rtcprr_mutex) != 0)
							throw std::runtime_error("Mutex lock failed");

						/*If the size of a RTCP RR list is 5, delete the first element from a list*/
						if (rtcp_rr_packets.at(x).size() == 5) {
							std::list<struct rtcprrbuf *>::iterator itb =
									rtcp_rr_packets.at(x).begin();
							p = *itb;
							rtcp_rr_packets.at(x).erase(itb);
							fprintf(log_all,
									"%d Delete the first RTCP RR packet from a list \n",
									x);
							fprintf(log_all, "%d_list_size = %zu \n", x, rtcp_rr_packets.at(x).size());
							delete p;
						}

						rtcp_rr_packets.at(x).push_back(rtcprr);
						fprintf(log_all, "%d Size of a rtcprr list = %zu \n", x,	rtcp_rr_packets.at(x).size());

						fprintf(log_all,
								"************************************************************\n");
						fprintf(log_all,
								"%d bnd just after filling a list %f \n", x,
								rtcp_rr_packets.at(x).back()->Bnd);

						if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
							throw std::runtime_error("Mutex unlock failed");

						path_status.at(x)->rtp_oc_count = 0;
						fprintf(log_all, "%d rrcount in recv_rtcp %d \n", x,
								path_status.at(x)->rrcount);
					}
				}
			} else
				printf("Don't see handle of a socket!!!");
		}
	}
	return NULL;
}

void rtcprr_list(struct rtcprrbuf * rtcprr, struct rtcprrbuf * buf, int x) {
	int64_t rrtime = 0;
	double delta_t = 0;
	rrtime = now();
	fprintf(log_all, "rrtime before = %" PRId64" \n", rrtime);
	p_rrtime = rrtime;
	rtcprr->rrtime = rrtime;
	uint16_t rrtime_int = (uint16_t) (rrtime / 1000000);

	rrtime = (int64_t) rrtime_int * 1000000 + rrtime % 1000000;

	fprintf(log_all, "rrtime = %" PRId64" \n", rrtime);
	fprintf(log_all, "rrtime_int = %" PRIu16" \n", rrtime_int);

	rtcprr->path = x;
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

	int64_t lsr = (int64_t) rtcprr->lsr_frac * 1000000 / 65536
			+ (int64_t) rtcprr->lsr * 1000000;
	rtcprr->rtt = rrtime - (int64_t) rtcprr->dlsr * 1000000
			- (int64_t) rtcprr->dlsr_frac * 1000000 / 65536 - lsr;
	path_status.at(x)->total_lossrate = rtcprr->totallost;

	printf("rtt = %" PRId64 " \n", rtcprr->rtt);

	//calculate PLR
	rtcprr->lossrate = ((double) (rtcprr->fraclost)) / 256;

	double payload = ((double) path_status.at(x)->sender_oc
			- (double) path_status.at(x)->lastsender_oc) * 8;

	/*	double payload = ((double) path_status.at(x)->sender_oc - (double) path_status.at(x)->lastsender_oc) * 8 /
	 ((double) path_status.at(x)->sender_pc - (double) path_status.at(x)->lastsender_pc);*/

	printf("fracloss = %f \n", (double) rtcprr->fraclost);
	printf("lossrate = %f \n", rtcprr->lossrate);

	fprintf(log_rtcp_r,
			"% d  %" PRId64 " %10" PRIu32 " %10f %" PRId32 "   %10" PRIu32 " %20" PRIu16 ":%" PRIu16 " %10" PRIu16 ":%" PRIu16 " %10" PRIu32 "\n",
			x, rrtime, rtcprr->ehsn, rtcprr->lossrate,
			(int32_t) rtcprr->totallost, rtcprr->jitter, rtcprr->lsr,
			rtcprr->lsr_frac, rtcprr->dlsr, rtcprr->dlsr_frac,
			path_status.at(x)->sender_pc);

	if (path_status.at(x)->rrcount == 0) {
		delta_t = ((double) p_rrtime - (double) st0) / 1000000;
		rtcprr->Bnd = payload * (1 - rtcprr->lossrate) / delta_t;
		fprintf(path_par,
				"%5d %10" PRId64 " %9.6f %17f %14f %14f %14f %18" PRId64 " %10f %7" PRIu32 " %7" PRIu32 " %7s %7" PRId32 " %10f \n",
				x, rtcprr->rtt, rtcprr->lossrate, rtcprr->Bnd, payload,
				(double) path_status.at(x)->sender_oc * 8,
				(double) path_status.at(x)->lastsender_oc * 8, p_rrtime,
				delta_t, rtcprr->ehsn, path_status.at(x)->last_ehsn, "0",
				(int32_t) rtcprr->totallost, 1 - rtcprr->lossrate);
	}

	if (path_status.at(x)->rrcount > 0) {

		// Calculate Bandwidth on a path
		delta_t = ((double) p_rrtime - (double) path_status.at(x)->last_rrtime)
				/ 1000000;
		printf("p_rrtime = %" PRId64 " \n", p_rrtime);
		printf("last_rrtime = %" PRId64 " \n", path_status.at(x)->last_rrtime);
		printf("delta_t = %f \n", delta_t);

		//	double Bnd = (payload * ((double) rtcprr->ehsn - (double) path_status.at(x)->last_ehsn)) / delta_t;
		rtcprr->Bnd = payload * (1 - rtcprr->lossrate) / delta_t;
		printf("EHSN = %f \n",
				(double) rtcprr->ehsn - (double) path_status.at(x)->last_ehsn);

		uint32_t dif_pc = path_status.at(x)->sender_pc
				- path_status.at(x)->lastsender_pc;

		printf(
				"%d Bnd = %f, sender_oc (%f), lastsender_oc (%f), sender_pc (%f), last_sender_pc (%f), ehsn(%" PRIu32 ") \n",
				x, rtcprr->Bnd, (double) path_status.at(x)->sender_oc,
				(double) path_status.at(x)->lastsender_oc,
				(double) path_status.at(x)->sender_pc,
				(double) path_status.at(x)->lastsender_pc, rtcprr->ehsn);

		fprintf(log_all,
				"%d Bnd = %f, sender_oc (%f), lastsender_oc (%f), sender_pc (%f), last_sender_pc (%f), ehsn(%" PRIu32 ") \n",
				x, rtcprr->Bnd, (double) path_status.at(x)->sender_oc,
				(double) path_status.at(x)->lastsender_oc,
				(double) path_status.at(x)->sender_pc,
				(double) path_status.at(x)->lastsender_pc, rtcprr->ehsn);

		fprintf(log_all, "Totallost = %" PRId32 ", fraclost = %f  \n",
				(int32_t) rtcprr->totallost, (double) rtcprr->fraclost);

		fprintf(path_par,
				"%5d %10" PRId64 " %9.6f %17f %14f %14f %14f %18" PRId64 " %10f %7" PRIu32 " %7" PRIu32 " %7" PRId32 " %7" PRId32 " %10f\n",
				x, rtcprr->rtt, rtcprr->lossrate, rtcprr->Bnd, payload,
				(double) path_status.at(x)->sender_oc * 8,
				(double) path_status.at(x)->lastsender_oc * 8, p_rrtime,
				delta_t, rtcprr->ehsn, path_status.at(x)->last_ehsn, dif_pc,
				rtcprr->totallost, 1 - rtcprr->lossrate);
	}

	/* save last received characteristics */
	path_status.at(x)->last_rrtime = p_rrtime;
	path_status.at(x)->lastsender_oc = path_status.at(x)->sender_oc;
	path_status.at(x)->lastsender_pc = path_status.at(x)->sender_pc;
	path_status.at(x)->last_ehsn = rtcprr->ehsn;
}

int UDP_Init(int x, char * ip, int localport, char * ipout) {
	int s;
	struct sockaddr_in mpaddr1;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		return -1;
	}
	if (s > 0) {
		printf("UDP socket has been initialized %s:%d\n", ip, localport);
	}
	//BIND
	mpaddr1.sin_family = AF_INET;
	mpaddr1.sin_port = htons(localport);
	if (inet_aton(ipout, &mpaddr1.sin_addr) == 0)
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
	fprintf(log_all, "ID flow = %" PRIu16 " \n", ntohs(mrheader.at(x)->subflowid));
	MPRTP_update_subflow(mrheader.at(x), packet->buf, x);

//	int64_t sent_time = now();
	sent_time = now()%10000000000;
	/* We can use sendto in non-blocking way for rescheduling information for another path (using MSG_DONTWAIT flag)
	 * (Don't forget to check the return value after that!)*/

	int bytes_sent = sendto(fds, packet->buf, packet->packetlen, MSG_DONTWAIT, (struct sockaddr *) &udpserver, sizeof(udpserver));
//	int64_t spent_time = now();

//	printf("bytes_sent = %d, seq = %" PRIu64 " \n", bytes_sent, packet->seq);
	fprintf(log_all, "bytes_sent = %d, seq = %" PRIu64 " \n", bytes_sent, packet->seq);
	if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EMSGSIZE))
	{
		printf("Buffer is overloaded...\n");
		fprintf(log_all, "Buffer is overloaded...\n");
		bytes_sent = packet->packetlen;
		printf("bytes_sent = packetlen = %d \n", bytes_sent);
		//	return 0;
	}

	fprintf(log_rtp, "%4d %6d %10" PRIu64 " %10d %10" PRIu32 " %16" PRIu64 " %16" PRIu16 "\n", x, packet->packetlen, packet->seq,
			packet->frame_number, packet->ts, sent_time, path_status.at(x)->seq_num - 1);

	return bytes_sent;
}

void SetnonBlocking(int s) {
	int opts;
	opts = fcntl(s, F_GETFL); //!!! Get the file access mode and the file status flags
	if (opts < 0) {
		perror("fcntl(F_GETFL)");
		exit(1);
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(s, F_SETFL, opts) < 0) {
		perror("fcntl(F_SETFL)");
		exit(1);
	}
	return;
}

vector<path_t> path_select(const vector<path_t>& lastpath) {
	if (pthread_mutex_lock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	thread_path = lastpath;
	int ts = -1;

	for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin();
			it != packets_to_send.end(); ++it) {
		int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

		if (count != NUM) {
			//	fprintf(log_all, "packet #%d already scheduled \n", (*it)->seq);
			continue;
		} else {
			//check if it is the first packet of Frame
			if (ts == -1)
				ts = (*it)->ts;
		}

		if (ts == (*it)->ts) {

			if (lastpath.at(0) == SENT) {
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(1) = SENT;
				(*it)->path.at(0) = NSENT;
			} else {
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(0) = SENT;
				(*it)->path.at(1) = NSENT;
			}

			thread_path = (*it)->path;

			for (uint i = 0; i < thread_path.size(); i++) {
				if (thread_path.at(i) == SENT) {
					fprintf(log_path, "%d 	%" PRIu64 "		%" PRIu32 " \n", i, (*it)->seq, (*it)->ts);
					fprintf(log_all, "%d 	%" PRIu64 "		was scheduled ROUND ROBIN \n", i, (*it)->seq);
				}
			}
		} else
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
		double rtcp_min_time) {
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
	if (initial) {
		rtcp_min_time /= 2;
		*avg_rtcp_size = 128;
	}
	/*
	 * If there were active senders, give them at least a minimum
	 * share of the RTCP bandwidth.  Otherwise all participants share
	 * the RTCP bandwidth equally.
	 */
	n = members;
	if (senders > 0 && senders < members * RTCP_SENDER_BW_FRACTION) {
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
	fprintf(log_all,"probe fb_interval = %f \n", probe);

	return probe * 1000000;
}

/*Functions for path scheduling */

vector<path_t> path_scheduling(const vector<path_t>& lastpath) {
	thread_path = lastpath;
	int32_t ts = -1;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0)
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(),
					INIT);

			if (count != NUM)
				continue;
			else {
				//if it is the first packet of Frame
				if (ts == -1)
					ts = (*it)->ts;
			}

			fprintf(log_all, "ts of the frame in scheduling %d \n", ts);
			if (ts == (*it)->ts)
			{
				for (uint i = 0; i < (*it)->path.size(); i++)
				{
					(*it)->path.at(i) = SENT;
					thread_path.at(i) = SENT;
					fprintf(log_path, "!!!%d 	%" PRIu64 "		%" PRIu32 " \n", i, (*it)->seq, (*it)->ts);
					int64_t t = now() % 10000000000;
					fprintf(log_all,
							"%" PRId64 " Scheduled %7d %7" PRIu64 " %16d \n", t, i, (*it)->seq, (*it)->frame_number);
				}
			} else
				break;

		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	} else {
//		path_smart_select(lastpath);
		thread_path = path_select(lastpath);
	}
	return thread_path;
}

vector<path_t> packets_path_scheduling(const vector<path_t>& lastpath) {
	thread_path = lastpath;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0) {
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		for (std::list<struct rtppacket *>::iterator it =
				packets_to_send.begin(); it != packets_to_send.end(); ++it) {
			int count = std::count((*it)->path.begin(), (*it)->path.end(),
					INIT);

			if (count != NUM)
				continue;
			else {
				for (uint i = 0; i < (*it)->path.size(); i++) {
					(*it)->path.at(i) = SENT;
					thread_path.at(i) = SENT;
					fprintf(log_path, "!!!%d 	%" PRIu64 "		%" PRIu32 " \n", i, (*it)->seq, (*it)->ts);
					int64_t t = now() % 10000000000;
					fprintf(log_all, "%" PRId64 " Scheduled %7d %7" PRIu64 " %16d \n", t, i, (*it)->seq, (*it)->frame_number);
				}
				break;
			}
		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	} else {
		//		path_smart_select(lastpath);
		thread_path = packets_path_select(lastpath);
	}
	return thread_path;
}

vector<path_t> packets_path_select(const vector<path_t>& lastpath) {
	if (pthread_mutex_lock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	thread_path = lastpath;

	for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin();
			it != packets_to_send.end(); ++it) {
		int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

		if (count != NUM) {
			//	fprintf(log_all, "packet #%d already scheduled \n", (*it)->seq);
			continue;
		} else {
			if (lastpath.at(0) == SENT) {
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(1) = SENT;
				(*it)->path.at(0) = NSENT;
			} else {
				//	printf("lastpath = %d \n", lastpath);
				(*it)->path.at(0) = SENT;
				(*it)->path.at(1) = NSENT;
			}

			thread_path = (*it)->path;

			for (uint i = 0; i < thread_path.size(); i++) {
				if (thread_path.at(i) == SENT) {
					fprintf(log_path, "%d 	%" PRIu64 "		%" PRIu32 " \n", i, (*it)->seq, (*it)->ts);
					fprintf(log_all, "%d 	%" PRIu64 "		was scheduled ROUND ROBIN \n", i, (*it)->seq);
				}
			}
			break;
		}
	}

	if (pthread_mutex_unlock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	return thread_path;
}

vector<std::pair<double, int> > SB_calculation()
{
	double Media_Rate = 500000;
	double AgB = 0;
	double AvB_total = 0;
	double sum_of_SB = 0;
	double Add_SB = 0;

	std::vector<double> average_Bnd(NUM, 0);
	std::vector<std::pair<double, int> > SAvB(NUM, std::make_pair(0.0, 0));
	std::vector<std::pair<double, int> > SB_total(NUM, std::make_pair(0.0, 0));

	if (pthread_mutex_lock(&rtcprr_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	for (uint i = 0; i < rtcp_rr_packets.size(); i++)
	{
		if (rtcp_rr_packets.at(i).empty() == 1)
		{
			fprintf(log_all, "The list %d is empty \n", i);
			fprintf(log_all, "%d AvB = %f if the list was empty \n",
					AvB.at(i).second, AvB.at(i).first);

			if (i == NUM - 1) {
				SB_total.at(i).second = i;
				SB_total.at(i).first = simple;
				fprintf(log_all, "SB %f for a path %d if a list is empty \n",
						SB_total.at(i).first, i);

				if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
					throw std::runtime_error("Mutex unlock failed");

				Media_index.first = LAYERS;
				Media_index.second = FPS;

				return SB_total;
			} else
				continue;
		}
		else {
			/*Calculate aggregated bandwidth for all paths*/
			AgB += rtcp_rr_packets.at(i).back()->Bnd;

			//		RTCP_RR_flag = 1;

			if ((rtcp_rr_packets.at(i).back()->lossrate == 0
					&& rtcp_rr_packets.at(i).back()->Bnd >= AvB.at(i).first)
					|| (rtcp_rr_packets.at(i).back()->lossrate > 0)) {
				//	average_Bnd = 0;
				/*define iterator for list which is an element of vector "rtcp_rr_packets"*/
				for (std::list<struct rtcprrbuf *>::iterator it =
						rtcp_rr_packets.at(i).begin();
						it != rtcp_rr_packets.at(i).end(); ++it)
				{
					/* find the sum of bandwidth values for path i in sliding  window and write this sum to element of pair(path, Average_Bandwidth) */
					average_Bnd.at(i) += (*it)->Bnd;
					fprintf(log_all, "%d average bandwidth = %f \n", i,
							average_Bnd.at(i));
				}

				AvB.at(i).second = i;
				AvB.at(i).first = average_Bnd.at(i)
						/ rtcp_rr_packets.at(i).size();

				if (rtcp_rr_packets.at(i).back()->lossrate == 0)
					simple = AvB.at(i).first;
				else
					simple = AvB.at(i).first / 2;
//**************************************************************************************
				for (uint k = 0; k < rtcp_rr_packets.size(); k++) {
					if (rtcp_rr_packets.at(k).empty() == 1) {
						AvB.at(k).first = simple;
						AvB.at(k).second = k;

						if (k == NUM - 1) {
							SB_total.at(k).second = i;
							SB_total.at(k).first = simple;
							fprintf(log_all,
									"SB %f for a path %d if a list is empty ",
									SB_total.at(k).first, k);

							if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
								throw std::runtime_error("Mutex unlock failed");

							return SB_total;
						} else
							continue;
					}
				}
//**************************************************************************************
				fprintf(log_all, "AvB before sorting \n");
				fprintf(log_all, "SIMPLE = %f \n", simple);
				for (uint i = 0; i < AvB.size(); i++) {
					fprintf(log_all, "%d AvB = %f \n", AvB.at(i).second,
							AvB.at(i).first);
				}
			}
		}
	}

	if (pthread_mutex_unlock(&rtcprr_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	fprintf(log_all, "Aggregated Bandwidth = %f \n", AgB);

	SAvB.assign(NUM, std::make_pair(0.0, 0));
	SAvB = AvB;

	/*Sort AverageBandwidth values from min to max*/
	std::sort(SAvB.begin(), SAvB.end());

	fprintf(log_all, "AvB after sorting \n");
	for (uint i = 0; i < SAvB.size(); i++) {
		fprintf(log_all, "%d SAvB = %f \n", SAvB.at(i).second,
				SAvB.at(i).first);
	}

	/*Sum all values of Average Bandwidth for all paths*/
	for (uint j = 0; j < AvB.size(); j++) {
		AvB_total += AvB.at(j).first;
	}
	fprintf(log_all, "AvB_total = %f \n", AvB_total);

	for (uint j = 0; j < AvB.size(); j++) {
		AvB_total += AvB.at(j).first;
	}
	fprintf(log_all, "AvB_total = %f \n", AvB_total);

	//////////////////////////////////////////////////////////////////////////////////////////

	for (uint j = 0; j < Media_bitrate.size(); j++) {
		for (uint i = 0; i < Media_bitrate.at(i).size(); i++)
		{
			if (AgB < Media_bitrate.at(j).at(i)) {
				double MRate1 = Media_bitrate.at(j - 1).at(i);
				double MRate2 = Media_bitrate.at(j).at(i - 1);

				if (AgB < MRate1) {
					Media_Rate = MRate1;

					if (pthread_mutex_lock(&media_mutex) != 0)
						throw std::runtime_error("Mutex lock failed");

					Media_index.first = j - 1;
					Media_index.second = i;

					if (pthread_mutex_unlock(&media_mutex) != 0)
						throw std::runtime_error("Mutex unlock failed");
				}
				if (AgB < MRate1) {
					Media_Rate = MRate2;

					if (pthread_mutex_lock(&media_mutex) != 0)
						throw std::runtime_error("Mutex lock failed");

					Media_index.first = j;
					Media_index.second = i - 1;

					if (pthread_mutex_unlock(&media_mutex) != 0)
						throw std::runtime_error("Mutex unlock failed");

				}
				fprintf(log_all,"AgB = %f MRate1 = %f MRate2 = %f \n", AgB, MRate1,
						MRate2);
			}
		}
	}

	for (uint j = 0; j < SAvB.size(); j++) {
		fprintf(log_all,"Number of element SB_total %d \n", SAvB.at(j).second);
		fprintf(log_all,"AvB.at(j).first (%f) / AvB_total (%f)  = %f\n",
				SAvB.at(j).first, AvB_total, SAvB.at(j).first / AvB_total);
		fprintf(log_all,"Difference between MediaRate and sum_of_SB %f \n",
				(double) Media_Rate - sum_of_SB);

		SB_total.at(j).first = (SAvB.at(j).first / AvB_total)
				* ((double) Media_Rate - sum_of_SB) * alpha;
		SB_total.at(j).second = SAvB.at(j).second;

		fprintf(log_all, "For path %d SB = %f without extra \n",
				SB_total.at(j).second, SB_total.at(j).first);
	}

	for (uint j = 0; j < SAvB.size(); j++)
	{
		fprintf(log_all,"Number of element SB_total %d \n", SAvB.at(j).second);
		fprintf(log_all,"AvB.at(j).first (%f) / AvB_total (%f)  = %f\n",
				SAvB.at(j).first, AvB_total, SAvB.at(j).first / AvB_total);
		fprintf(log_all,"Difference between MediaRate and sum_of_SB %f \n",
				(double) Media_Rate - sum_of_SB);

		SB_total.at(j).first = (SAvB.at(j).first / AvB_total)
				* ((double) Media_Rate - sum_of_SB) * alpha;
		SB_total.at(j).second = SAvB.at(j).second;

		//SB_total.at(SAvB.at(j).second) = (SAvB.at(j).first / AvB_total) * ((double)Media_Rate - sum_of_SB) * alpha;

		//		sum_of_SB = std::accumulate(SB_total.begin(),SB_total.end(), SB_total.at(AvB.at(j).second));

		fprintf(log_all, "For path %d SB = %f without extra \n",
				SB_total.at(j).second, SB_total.at(j).first);

		//	sum_of_SB = std::accumulate(SB_total.begin(),SB_total.end(),0);
		for (uint i = 0; i < SB_total.size(); i++)
			sum_of_SB += SB_total.at(i).first;

		int path_num = 0;
		/* If Sending Bit rate is smaller that Video Rate */
		while (1) {
			fprintf(log_all, "WHILE!!!!!!\n");
			for (uint i = 0; i < rtcp_rr_packets.size(); i++) {
				fprintf(log_all, "ExTRA begin FOR loop %d\n", i);

				if (rtcp_rr_packets.at(i).back()->lossrate == 0) {
					path_num++;
					sum_of_SB += Add_SB;
					Add_SB = (SAvB.at(i).first / Media_Rate)
							* (Media_Rate - sum_of_SB);
					SB_total.at(i).first += Add_SB;

					fprintf(log_all, "sum_of_SB = %f \n", sum_of_SB);
					fprintf(log_all, "For path %d SB = %f with extra \n",
							SAvB.at(i).second, SB_total.at(i).first);
				}
			}
			/*If we have congestion on all paths */

			fprintf(log_all, "sum_of_SB = %f \n", sum_of_SB);

			/*Since I don't know why it is impossible escape from a loop even if values of sum_of_SB and Media_Rate are equal, I decided to add +0.00001 to sum_of_SB*/
			if (sum_of_SB >= Media_Rate - 0.00001)
				break;
			else {
				if (path_num == 0) {
					for (uint i = 0; i < rtcp_rr_packets.size(); i++) {
						if (rtcp_rr_packets.at(i).back()->lossrate > 0) {
							sum_of_SB += Add_SB;
							Add_SB = (SAvB.at(i).first / Media_Rate)
									* (Media_Rate - sum_of_SB);
							SB_total.at(i).first += Add_SB;

							fprintf(log_all, "sum_of_SB in congestion = %f \n",
									sum_of_SB);
							fprintf(log_all,
									"For path %d SB = %f with extra in congestion \n",
									SAvB.at(i).second, SB_total.at(i).first);
						}
					}
				}
			}
		}
	}
	return SB_total;
}

int create_packet()
{
	uint16_t seq_fr = 0;

	if (pthread_mutex_lock(&frame_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	for (std::list<struct trace *>::iterator it = frame_list.begin(); it != frame_list.end(); ++it)
	{
		if ((*it)->location == NONALLOC)
		{
			int payload = 0;

			for (int i = 0; i < Media_index.first; i++)
				payload += (*it)->size.at(i).at(Media_index.second - 1);

			double q = payload / (double) (MTU-MPRTP_HEADER);
			int quantity = 0;
			quantity = (int) ceil(q);

			for (int i = 0; i != quantity; i++)
			{
				/*Don't forget to consider MPRTP header in 12 bytes!*/

				if (payload <= (MTU - MPRTP_HEADER))
				{
					struct rtppacket *packet = NULL;

					packet = new struct rtppacket;
					memset(packet, 0, sizeof(packet));

					packet->payloadlen = payload;
					packet->packetlen = packet->payloadlen + 12;
					packet->ts = (*it)->time*1000000;
					packet->seq_fr = i;
					packet->seq = seq;
					packet->frame_number = (*it)->number;
					seq_fr++;
					seq++;

					fprintf(log_all, "1 Frame #%d packet_fr#%d packet#%" PRIu64 " payload %d \n", (*it)->number, packet->seq_fr, packet->seq,
							packet->payloadlen);

					insert_data_to_list(packet);
				}
				else
				{
					struct rtppacket *packet = NULL;

					packet = new struct rtppacket;
					memset(packet, 0, sizeof(packet));

					packet->payloadlen = MTU;
					packet->packetlen = packet->payloadlen + 12;
					packet->ts = (*it)->time*1000000;
					packet->seq_fr = i;
					packet->seq = seq;
					packet->frame_number = (*it)->number;
					seq++;
					seq_fr++;

					fprintf(log_all, "2 Frame #%d packet_fr#%d packet#%" PRIu64 " payload %d \n",
							(*it)->number, packet->seq_fr, packet->seq,	packet->payloadlen);

					insert_data_to_list(packet);

					payload = payload - MTU;
				}

				if (seq_fr == quantity)
				{
					(*it)->location = ALLOC;
					int64_t t_finish = now();

					fprintf(log_quantity, "%d    %8" PRIu16 " %10" PRId64 " %10" PRId64 "\n", (*it)->number, seq_fr, t_finish-st0, (int64_t)((*it)->time*1000000));

					if (pthread_mutex_unlock(&frame_mutex) != 0)
						throw std::runtime_error("Mutex unlock failed");

					return 0;
				}
			}
		}
		else
			continue;
	}
	if (pthread_mutex_unlock(&frame_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	return 0;
}

int open_bitrate_file() {
	FILE *fr = fopen("/home/ekaterina/experiment/bitrate", "r");
	if (fr == NULL) {
		printf("Read called without fr opening file\n");
		printf("While reading fr file : %d, %s\n", errno, strerror(errno));
		return 0;
	}
	/* read from a bitrate trace file information
	 * put these values to vector Media_bitrate where strings reflects quality scalability
	 * and colomns reflects temporal scalability */

	for (uint j = 0; j < Media_bitrate.size(); j++) {
		for (uint i = 0; i < Media_bitrate.at(i).size(); i++) {
			if (fscanf(fr, "%lf", &Media_bitrate.at(j).at(i)) == EOF)
				break;
		}
	}
	if (fclose(fr) == 0)
		printf("Bitrate file was successfully closed \n");
	else
		printf("Error during the procedure of closing bitrate file: %d \n",
				errno);
	return 1;
}

/*FILE open_tracefile (char *filename)
 {
 FILE *tr = fopen("/home/ekaterina/experiment/tracefile", "r");
 if (tr == NULL)
 {
 printf("Read called without fd opening file\n");
 printf("While reading fd file : %d, %s\n", errno, strerror(errno));
 fclose(tr);
 //	return NULL;
 }
 else
 {
 printf("tr = %d \n", tr);
 return *tr;
 }
 }*/

vector<path_t> path_SB_scheduling(std::vector<std::pair<double, int> > SB_total)
{
	int64_t tc = 0;
	double delta_t = 0;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0)
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		fprintf(log_all, "size of packets_to_send in path scheduling = %" PRId64 " \n", packets_to_send.size());

		for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

			if (count != NUM)
				continue;
			else
			{
				std::list<struct rtppacket *>::iterator itor = packets_to_send.begin();

				if((*it)->ts == 0 || (*it)->ts == (*itor)->ts)
				{
					for (uint i = 0; i < (*it)->path.size(); i++)
					{
						(*it)->path.at(i) = SENT;
						thread_path.at(i) = SENT;
						fprintf(log_path, "!!!%d 	%" PRIu64 "	 %d	%d \n", i, (*it)->seq, (*it)->frame_number, (*it)->ts);
						int64_t t = now() % 10000000000;

						fprintf(log_all, "%" PRId64 " Scheduled %7d %7" PRIu64 " %7d \n", t, i, (*it)->seq, (*it)->frame_number);
					}
				}
				else
					break;
			}
		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
	else
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		for (std::list<struct rtppacket *>::iterator it =
				packets_to_send.begin(); it != packets_to_send.end(); ++it) {
			int count = std::count((*it)->path.begin(), (*it)->path.end(),
					INIT);

			/*if packet in a list is already scheduled*/
			if (count != NUM)
				continue;
			/*if packet in a list isn't scheduled yet*/
			else {
				tc = now(); // take a current time

				for (uint i = 0; i < SB_total.size(); i++) {
					delta_t = (*it)->packetlen / SB_total.at(i).first * 1000000;
					fprintf(log_all, "packetlen = %d #%" PRIu64 " \n", (*it)->packetlen, (*it)->seq);
					fprintf(log_all, "%d Sending Bit rate = %f \n", i,
							SB_total.at(i).first);
					fprintf(log_all, "%d delta_t in Token Bucket = %f \n", i,
							delta_t);
					fprintf(log_all, "%d tc - tp = %" PRId64 "\n", i,
							tc - tp.at(i));

					if (tc - tp.at(i) > delta_t) {
						for (uint j = 0; j < (*it)->path.size(); j++)
							(*it)->path.at(j) = NSENT;

						(*it)->path.at(SB_total.at(i).second) = SENT;

						fprintf(log_all, "Transmit packet #%" PRIu64 " to path %d \n", (*it)->seq, i);
						tp.at(i) = tc;
						thread_path = (*it)->path;
						fprintf(log_path, "%d 	%" PRIu64 "	%d	%d \n", i, (*it)->seq, (*it)->frame_number, (*it)->ts);

						if (pthread_mutex_unlock(&list_mutex) != 0)
							throw std::runtime_error("Mutex unlock failed");

						return thread_path;
					} else
						continue;
				}
			}
		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
	return thread_path;
}
