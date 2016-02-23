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
#include <cmath>
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
#define Sliding_window 5000000
#define alpha 0.5
#define MTU 1460				//define MTU size of a network
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

pthread_mutex_t list_mutex; // = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutex_t loss_mutex;
//pthread_mutex_t rtcprr_mutex; // = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutex_t frame_mutex; // = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutex_t media_mutex; // = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
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
std::vector<std::vector<double> > common_loss(NUM, std::vector<double>());
std::vector<double> common(NUM, 0);
std::vector <double> old_common (NUM, 0);

std::pair<int, int> Media_index(0, 0);

list<struct trace *> frame_list; //Create a list of frames (with different layers information) based of trace file
list<struct rtppacket *> packets_to_send;   //Create list of packets!!!!

FILE *log_path = NULL;
FILE *log_rtcp_s = NULL;
FILE *log_rtcp_r = NULL;
FILE *SB_log = NULL;
FILE *path_par = NULL;
FILE *log_rtp = NULL;
FILE *log_all = NULL;
FILE *log_quantity = NULL;
FILE *time_log = NULL;
FILE *sched = NULL;

FILE *temple = NULL;
FILE *log_for = NULL;
FILE *log_while = NULL;

static int64_t st0;

uint16_t seq = 0;
int64_t ts0 = -1;
int64_t fb_interval = 0;
int64_t sent_time = 0;
int64_t initial_sent_time = 0;
int64_t buffer_time = 0;

double simple = 0;
double Media_rate_prev = 0;

int i_senders;
int RTCP_RR_flag = 0;
int fdrtcp[NUM]; 								//descriptor for rtcp socket
int counter = 0;							//counter for media_rate changing
int last_path = -1;

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
	std::vector<std::pair<double, int> > SB_total_sum(NUM,
			std::make_pair(0.0, 0));

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
		path_status.back()->seq_num = -1;

		mrheader.push_back(new struct mprtphead);
		memset(mrheader.back(), 0, sizeof(struct mprtphead));
		MPRTP_Init_subflow(mrheader.at(i), i);
	}

	SB_log = fopen("/home/ekaterina/experiment/SB_file", "w");

	log_all = fopen("/home/ekaterina/experiment/log_all", "w");
	if (log_all == NULL)
		printf("Impossible to open or create txt log file for RTP!!!\n");

	sched = fopen("/home/ekaterina/experiment/sched", "w");
	if (sched == NULL)
		printf("Impossible to open or create txt log file for sched!!!\n");
	else
		fprintf(sched, "%s %10s \n", "Media_rate", "Time_of_changing");

	temple = fopen("/home/ekaterina/experiment/temple", "w");
	log_for = fopen("/home/ekaterina/experiment/octave/log_for", "w");

	// Create log file for monitor RTP packets
	log_rtp = fopen("/home/ekaterina/experiment/log_rtp_file", "w");
	if (log_rtp == NULL)
		printf("Impossible to open or create txt log file for RTP!!! \n");
	else
		fprintf(log_rtp, "%4s %6s %10s %10s %10s %10s %6s %16s %16s %16s %16s \n",
				"Path", "Length", "Payload", "Seq_No","SeqFlow_No" "Frame_No", "ts",
				"send_time", "ideal_send_time", "create_time", "alloc_time");

	log_while = fopen("/home/ekaterina/experiment/log_while", "w");

	log_quantity = fopen("/home/ekaterina/experiment/log_qu", "w");
	if (log_quantity == NULL)
		fprintf(log_all, "Impossible to open or create txt quantity!!!\n");
	else
		fprintf(log_quantity, "%s %8s %10s %10s \n", "Frame", "packets",
				"Real time", "timestamp");

	log_rtcp_r = fopen("/home/ekaterina/experiment/log_rtcp_r", "w");
	if (log_rtcp_r == NULL)
		printf("Impossible to open or create txt rtcp_r!!!\n");
	else
		fprintf(log_rtcp_r, "%s %16s %16s %16s %16s %16s %16s %16s %16s\n",
				"path", "ts", "ehsn", "fraclost", "totallost", "jitter",
				"LSR : LSR_FRAC", "DLSR:DLSR_FRAC", "Packet_sent");

	time_log = fopen("/home/ekaterina/experiment/time_log", "w");
	if (time_log == NULL)
		printf("Impossible to open or create txt rtcp_r!!!\n");
	else
		fprintf(time_log, "%s %25s %20s %20s %20s %20s %20s \n", "st0",
				"before_wait", "after_wait", "before_SB", "after_SB",
				"after_create", "after_path_sched");

	path_par = fopen("/home/ekaterina/experiment/path_parameters", "w");
	if (path_par == NULL)
		printf("Impossible to open or create txt path_parameters!!!\n");
	else
		fprintf(path_par,
				"%5s %10s %9.6s %17s %14s %14s %14s %18s %10s %7s %7s %7s %7s\n",
				"path", "rtt", "PLR", "Bandwidth", "payload", "sender_oc",
				"last_sender_oc", "rrtime", "delta_t", "EHSN", "LAST_EHSN",
				"dif_pc", "total loss");

	log_rtcp_s = fopen("/home/ekaterina/experiment/log_rtcp_s", "w");

	if (log_rtcp_s == NULL)
		throw std::runtime_error("File log_rtcp_s creation failed");
	else
		fprintf(log_rtcp_s, "path	ts 		sender_oc 	sender_pc \n");

	log_path = fopen("/home/ekaterina/experiment/log_path_file", "w");
	if (log_path == NULL)
		throw std::runtime_error("File log_path creation failed");
	else
		fprintf(log_path, "%s %5s %5s %5s \n", "path", "seq_No", "frame_No", "ts");

	//Create 3 threads for each interface: 1) for RTP; 2) RTCP send; 3)RTCP receive
	create_threads(n, ip, port, ipout);

	//Open trace file and put frames into frame list

#if TRACING

	// Open RTP file

	int fd = openfile(filename);
#else
//	 FILE fd = open_tracefile(filename);

#endif

	/*Read bitrate file for particular trace file*/

	open_bitrate_file();

	/* Read frames from trace file and push them to the frame_list*/
	readtrace(filename);

	char frame_type[10];

	struct time time;

	if (ts0 == -1)
	{
		ts0 = 0;
		st0 = now();
		time1 = st0;
		buffer_time = st0;
	}

	while (term_flag == 0)
	{
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


		for (std::list<struct trace *>::iterator it = frame_list.begin(); it != frame_list.end(); ++it)
		{
			if ((*it)->number == 0 || (*it)->location == NONALLOC)
			{
				strcpy(frame_type, (*it)->type);

				//Sleep until wake (in microsec)
				int64_t wake = (int64_t) ((*it)->time * 1000000) + st0;

				int64_t wait = waiting(wake);

				fprintf(temple, " %"PRId64 " %"PRId64 "\n", wait, wake);

	//			assert(it == frame_list.begin());
				break;
			}
		}

		/*Sending Bit rate calculation!!!!*/
		char frame_I[] = "I";

		int64_t time2 = now();
		int64_t T_scheduler = fb_interval * (drand48() + 0.5) /** 1000000*/;

	//	fprintf(log_all, "Frame_type = %s frame_I = %s \n", frame_type, frame_I);

		/*Renew Sending Bit Rate values if recalculation interval is expired or we received the first RTCP RR packet*/
		if ((time2 - time1 > T_scheduler || RTCP_RR_flag == 1) && (strcmp(frame_type, frame_I) == 0))
		{
			/*Calculate Sending Bit rate for each path*/

			fprintf(log_all, "Time for recalculation of scheduler is expired \n");

			time.before_SB = now();

	//		SB_total = SB_calculation();

			for(int y = 0; y < NUM; y++)
			{
				Media_index.first = LAYERS - 1;
				Media_index.second = FPS - 1;

				SB_total.at(y).second = y;

			//	SB_total.at(y).first = Media_bitrate.at(Media_index.first).at(Media_index.second) / 8;
				SB_total.at(y).first = 1010554320.0 / 8;
			}

			/*For calculation a total average bandwidth for a path*/

			for (uint i = 0; i < SB_total.size(); i++)
			{
				if (pthread_mutex_lock(&path_status.at(i)->rtp_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

/*				struct timespec timeout;
				struct timeval timenow;

				gettimeofday(&timenow, NULL);
				timeout.tv_sec = timenow.tv_sec;
				timeout.tv_nsec = 100000;

				if(pthread_mutex_timedlock(&path_status.at(i)->rtp_mutex, &timeout) != 0)
				{
					printf( "Error during rtp mutex %s \n",  strerror(errno));
					throw std::runtime_error("Mutex lock failed");
				}
*/
				if (path_status.at(i)->rrcount == 0	&& path_status.at(i)->rrcount_prev == 0)
				{
					path_status.at(i)->rrcount_prev = -1;
					counter.at(i)++;
					SB_total_sum.at(SB_total_sum.at(i).second).first +=	SB_total.at(i).first;

			/*		fprintf(log_all, "!!!SB %d %10f %d %d \n",
							SB_total_sum.at(i).second, SB_total_sum.at(i).first,
							path_status.at(i)->rrcount,
							path_status.at(i)->rrcount_prev);
			*/	}

				if (path_status.at(i)->rrcount != path_status.at(i)->rrcount_prev)
				{
					if (path_status.at(i)->rrcount != 0)
					{
						path_status.at(i)->rrcount_prev = path_status.at(i)->rrcount;

						SB_total_sum.at(SB_total.at(i).second).first +=	SB_total.at(i).first;
						SB_total_sum.at(SB_total.at(i).second).second = SB_total.at(i).second;
						counter.at(i)++;
						//	SB_total_sum.at(i).second = SB_total.at(i).second;
						fprintf(SB_log, " %d %20f\n", SB_total.at(i).second, SB_total.at(i).first);

				/*		fprintf(log_all, "SB_total_sum %d %10f %d \n",
								SB_total_sum.at(SB_total.at(i).second).second,
								SB_total_sum.at(SB_total.at(i).second).first,
								path_status.at(i)->rrcount);
*/
					}
				}
				if (pthread_mutex_unlock(&path_status.at(i)->rtp_mutex) != 0)
					throw std::runtime_error("Mutex unlock failed");
			}
			time.after_SB = now();
			time1 = time2;
		}

		/* It is neccesary to create packets for sending according to Media_index */

		create_packet();

/*		struct timespec timeout;
		struct timeval timenow;

		gettimeofday(&timenow, NULL);
		timeout.tv_sec = timenow.tv_sec;
		timeout.tv_nsec = 100000;

		for (uint i = 0; i < NUM; i++)
			if (pthread_mutex_timedlock(&path_status.at(i)->rtp_thread_mutex, &timeout) != 0)
			{
				printf( "Error during rtp mutex %d \n", errno);
				throw std::runtime_error("Mutex lock failed");
			}
*/
		for (uint i = 0; i < NUM; i++)
			if (pthread_mutex_lock(&path_status.at(i)->rtp_thread_mutex) != 0)
			{
				printf( "Error during rtp mutex %d \n", errno);
				throw std::runtime_error("Mutex lock failed");
			}
		/*Allocate packet to an appropriate path*/
		thread_path = path_SB_scheduling(SB_total);

#endif
		for (uint i = 0; i < NUM; i++)
		{
			if (path_status.at(i)->packet.empty() == 0)
			{
				if (pthread_cond_signal(&path_status.at(i)->rtp_cond) == 0)
				{
				//	fprintf(log_all,"Signal to rtp send thread %d successfull \n", i);
				}
				else
					fprintf(log_all, "Error during the signal to rtp send thread %d: %d \n", errno, i);
			}
		}

		for (uint i = 0; i < NUM; i++)
		{
			if (pthread_mutex_unlock(&path_status.at(i)->rtp_thread_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

		}


//		continue;

		int counter = 0;

		for (uint i = 0; i < NUM; i++)
		{
			struct timespec timeout;
			struct timeval timenow;

/*			gettimeofday(&timenow, NULL);
			timeout.tv_sec = timenow.tv_sec;
			timeout.tv_nsec = 100000;

			int ret = pthread_mutex_timedlock(&path_status.at(i)->rtp_mutex_list, &timeout);
			if(ret == ETIMEDOUT)
			{
				printf( "Error during rtp mutex %s \n",  strerror(errno));
			}
			if(ret != 0)
				throw std::runtime_error("Mutex lock failed");
*/
			if (pthread_mutex_lock(&path_status.at(i)->rtp_mutex_list) != 0)
				throw std::runtime_error("Mutex lock failed");

			if (path_status.at(i)->packet.empty() == 1)
				counter++;

			if (pthread_mutex_unlock(&path_status.at(i)->rtp_mutex_list) != 0)
				throw std::runtime_error("Mutex lock failed");
		}

//		printf("packets_to_send = %zu  frame_list = %zu counter = %d \n", packets_to_send.size(), frame_list.size(), counter);
		if (packets_to_send.empty()	== 1 && frame_list.empty() == 1 && counter == NUM)
		{
			term_flag = 1;
//			fprintf(log_all, "term_flag = 1 \n");
		}
	}

	if (term_flag == 1)
	{
		for (int i = 0; i < n; i++)
		{
			if (pthread_join(threadrtp[i], NULL) != 0)
				printf("Error during the termination rtp thread: %d \n", errno);
			if (pthread_join(threadrtcpr[i], NULL) != 0)
				printf("Error during the termination rtcpr thread: %d \n", errno);
			if (pthread_join(threadrtcps[i], NULL) != 0)
				printf("Error during the termination rtcpsr thread: %d \n", errno);

			printf("Total number of packet sent to path %d = %d \n", i, path_status.at(i)->sender_pc);
			printf("Total lost for path %d = %" PRId32 " \n", i, path_status.at(i)->total_lossrate);
			fprintf(log_all, "Total number of packet sent to path %d = %d \n", i, path_status.at(i)->sender_pc);
			fprintf(log_all, "Total lost for path %d = %" PRId32 " \n", i, path_status.at(i)->total_lossrate);
		}

		int64_t t_finish = now();
		printf("Total_time = %" PRId64 " \n", t_finish - st0);
		fprintf(log_all, "Total_time = %" PRId64 " \n", t_finish - st0);

		if (fclose(log_rtp) == 0)
			printf("Log_rtp file was successfully closed \n");
		else
			printf("Error during the procedure of closing log file: %d \n",
					errno);

		if (fclose(log_rtcp_r) == 0)
			printf("Log_rtcp_r file was successfully closed \n");
		else
			printf(
					"Error during the procedure of closing log_rtcp_r file: %d \n",
					errno);

		if (fclose(log_while) == 0)
			printf("Log_while file was successfully closed \n");

		if (fclose(log_rtcp_s) == 0)
			printf("Log_rtcp_s file was successfully closed \n");
		else
			printf(
					"Error during the procedure of closing Log_rtcp_s file: %d \n",
					errno);

		if (fclose(sched) == 0)
			printf("Sched file was successfully closed \n");
		else
			printf("Error during the procedure of closing Sched file: %d \n",
					errno);

		if (fclose(log_quantity) == 0)
			printf("Log_quantity file was successfully closed \n");
		else
			printf(
					"Error during the procedure of closing Log_quantity file: %d \n",
					errno);

		if (fclose(time_log) == 0)
			printf("Time_log file was successfully closed \n");
		else
			printf("Error during the procedure of closing time_log file: %d \n",
					errno);

		if (fclose(temple) == 0)
			printf("temple file was successfully closed \n");
		else
			printf("Error during the procedure of closing temple file: %d \n",
					errno);

		if (fclose(log_for) == 0)
			printf("Log_for file was successfully closed \n");

		if (fclose(log_all) == 0)
			printf("Log_all file was successfully closed \n");
		else
			printf("Error during the procedure of closing log_all file: %d \n",
			errno);

#ifdef SMART

		for (uint i = 0; i < SB_total_sum.size(); i++) {
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
			break;
		case 'g':
			strcpy(ipout[1], optarg);
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

/*to make crtl+c works*/
void signalHandler(int sig) {
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

	/*set x bit*/
	buf[0] = buf[0] | EXT_MASK;
	path_status.at(x)->seq_num++;
	fprintf(log_all, "MPRTP Subflowseq for path %d = %" PRIu16 "\n", x,	path_status.at(x)->seq_num);
	mrheader->subflowseq = htons(path_status.at(x)->seq_num);
	mrheader->subflowid = htons(x);
	memcpy(buf+12, mrheader, sizeof(struct mprtphead));
}

void insert_data_to_list(struct rtppacket * packet)
{
	packets_to_send.push_back(packet);
}

int create_threads(int n, char ipnew[][20], int * txportnew, char ipout[][20])
{
	for (int i = 0; i < n; i++) {
		strncpy(path_status.at(i)->ip, ipnew[i], 20);
		strncpy(path_status.at(i)->ipout, ipout[i], 20);

		printf("interface is %d, IP is %s\n", i, path_status.at(i)->ip);
		path_status.at(i)->allread = 0;
		path_status.at(i)->txport = txportnew[i];
		path_status.at(i)->obytes = 0;
		path_status.at(i)->total_lossrate = -1.0;

		printf("Port: %d \n", txportnew[i]);

		// Initialize mutex for path_status structure
		if (pthread_mutex_init(&path_status.at(i)->rtcpsr_mutex_list, NULL) != 0)
			throw std::runtime_error("RTP_mutex_list  initialization failed");
		else
			printf("rtp_mutex_list for path %d is initialized \n", i);

		if (pthread_mutex_init(&path_status.at(i)->rtp_mutex_list, NULL) != 0)
			throw std::runtime_error("RTP_mutex_list  initialization failed");
		else
			printf("rtp_mutex_list for path %d is initialized \n", i);

		// Initialize mutex for path_status structure
		if (pthread_mutex_init(&path_status.at(i)->rtp_mutex, NULL) != 0)
			throw std::runtime_error("RTP_mutex  initialization failed");
		else
			printf("rtp_mutex for path %d is initialized \n", i);
		// Initialize mutex for rtp thread
		if (pthread_mutex_init(&path_status.at(i)->rtp_thread_mutex, NULL) != 0)
			throw std::runtime_error("rtcpsr_mutex  initialization failed");

		// Initialize mutex for rtcpsr thread
		if (pthread_mutex_init(&path_status.at(i)->rtcpsr_thread_mutex, NULL)
				!= 0)
			throw std::runtime_error("rtcpsr_mutex  initialization failed");

		// Initialize mutex for rtcprr thread
		if (pthread_mutex_init(&path_status.at(i)->rtcprr_thread_mutex, NULL)
				!= 0)
			throw std::runtime_error("rtcpsr_mutex  initialization failed");

		// Initialize conditional variable for RTCP thread
		if (pthread_cond_init(&path_status.at(i)->rtcp_cond, NULL) != 0) {
			perror("cond init failed");
			return 0;
		}
		// Initialize conditional variable for RTP thread
		if (pthread_cond_init(&path_status.at(i)->rtp_cond, NULL) != 0) {
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
	if (fd < 0) {
		printf("Error opening file : %s\n", strerror(errno));
	} else {
		if (fstat(fd, &statbuf) != 0) {
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
	} else {
		/* read from a trace file information*/
		while (allread == 0) {
			frame = new struct trace;
			//		memset(frame, 0, sizeof(frame));

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

				uint s = 0;
				for (uint i = 0; i < frame->size.at(i).size(); i++) {
					for (uint j = 0; j < frame->size.size(); j++) {
						for (; s < size.size();) {
							/*Since dimension of all three arrays the same we will use the one loop for all*/
							frame->size.at(j).at(i) = size.at(s);
							frame->PSNRY.at(j).at(i) = PSNRY.at(s);
							frame->PSNRU.at(j).at(i) = PSNRU.at(s);
							frame->PSNRV.at(j).at(i) = PSNRV.at(s);

							/*						fprintf(log_all, "FRAME #%d \n", frame->number);
							 fprintf(log_all, "i= %d, j = %d size: %d \n", i, j, frame->size.at(j).at(i) );
							 fprintf(log_all, "i= %d, j = %d PSNRY: %f \n", i, j, frame->PSNRY.at(j).at(i) );
							 fprintf(log_all, "i= %d, j = %d PSNRU: %f \n", i, j, frame->PSNRU.at(j).at(i) );
							 fprintf(log_all, "i= %d, j = %d PSNRV: %f \n", i, j, frame->PSNRV.at(j).at(i) );
							 */
							s++;
							break;
						}
					}
				}

				frame->location = NONALLOC;

				if (!frame_list.empty()	|| frame_list.back()->time != frame->time)
				{
					frame_list.push_back(frame);
				}
			}
			else
			{
				fclose(fd);
				allread = 1;
			}
		}
	}
	return 0;
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
	if (read(fd, packet->buf + 24, packet->payloadlen) != packet->payloadlen) {
		fprintf(log_all, "While reading file (length read: %d): %d, %s\n",
				packet->packetlen, errno, strerror(errno));
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

	struct timespec timeoutsr;

	//Initialize socket for rtp connection

	if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex lock failed");

	int fds = UDP_Init(x, 55000 + (2 * x), path_status.at(x)->ipout);
	if (fds < 0)
	{
		printf("UDP socket initialize for RTP failed");
		pthread_exit(NULL);
	}

	//Initialize socket for rtcp connection
	path_status.at(x)->rtcp_sock = UDP_Init(x, 55000 + (2 * x) + 1, path_status.at(x)->ipout);

	//	SetnonBlocking(path_status.at(x)->rtcp_sock);

	if (path_status.at(x)->rtcp_sock < 0)
	{
		if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
		pthread_exit(NULL);
	}

	if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	while (term_flag == 0)
	{
		if (pthread_mutex_lock(&path_status.at(x)->rtp_thread_mutex) != 0)
			pthread_exit(NULL);

		struct timeval timenowsr;

		while (path_status.at(x)->packet.empty() == 1 && term_flag == 0)
		{
			gettimeofday(&timenowsr, NULL);
			timeoutsr.tv_sec = 30 + timenowsr.tv_sec;
			timeoutsr.tv_nsec = 0;

			int ret = pthread_cond_timedwait(&path_status.at(x)->rtp_cond, &path_status.at(x)->rtp_thread_mutex, &timeoutsr);

			if (ret == ETIMEDOUT)
				printf("While waiting rtp, %d %d : %s \n", x, errno, strerror(errno));
		}

		if (pthread_mutex_unlock(&path_status.at(x)->rtp_thread_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		fprintf(log_all, "%d size of path list %zu \n", x, path_status.at(x)->packet.size());

//		fprintf(log_all, "%d Send packet #%" PRIu16 " fr# %d \n", x, path_status.at(x)->packet.front()->seq,
//				path_status.at(x)->packet.front()->frame_number);

		printf("%d Send packet #%" PRIu16 " fr# %d \n", x, path_status.at(x)->packet.front()->seq,
						path_status.at(x)->packet.front()->frame_number);

		int64_t time_mute = now();

		byte_sent = sending_rtp(x, fds, path_status.at(x)->packet.front());

		int64_t time_mute2 = now();
		fprintf(log_for, " %" PRId64 " %" PRId64 " \n", time_mute, time_mute2);

		int64_t t1 = now();

		if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		int64_t t2 = now();
		fprintf(time_log, "%" PRId64 " %" PRId64 "\n", t1, t2);

		path_status.at(x)->sender_oc += byte_sent; // counts in payload octets, e.t. in bytes
		path_status.at(x)->sender_pc += 1; // counts in quantity of packets (1,2,3,....)

		/*add information to path characteristics about the number of packets sent between two consecutive RR packets*/
		path_status.at(x)->rtp_oc_count += byte_sent;

		if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		// Call the rtcp send thread

		// How to initialize it????? what is the value????
		int avg_rtcp_size;

		if (time1 == -1)
		{
			time1 = st0;
		}

		int64_t time_rtp_mut = now();

		fb_interval = rtcp_interval(i_senders * 2, i_senders, RTCP_BW, 1, 56/*RTCPSR+UDP+IP*/, &avg_rtcp_size, 1, 0.1);

		int64_t time2 = now();

//		fprintf(log_all, "(time2 (%" PRId64 ") - time1 (%" PRId64 ")) - fb_interval (%" PRId64 ") = (%" PRId64 ") \n",
//				time2, time1, fb_interval, time2 - time1 - fb_interval);

//		struct timespec timeout1;
//		timeout1.tv_sec = 60;
//		timeout1.tv_nsec = 0;

		if (time2 - time1 > fb_interval)
		{
			int ret1 = pthread_mutex_lock(&path_status.at(x)->rtcpsr_thread_mutex);
	/*		if(ret1 == ETIMEDOUT)
			{
				printf( "Error during rtcpsr thread mutex %s \n",  strerror(errno));
			}
*/			if(ret1 != 0)
				throw std::runtime_error("Mutex lock failed");

			getrtcpsr(time2, x);
			fprintf(log_all, "%d Sending RTCP report \n", x);

			if (pthread_cond_signal(&path_status.at(x)->rtcp_cond) != 0)
				throw std::runtime_error("Pthread_cond_signal failed");

			if (pthread_mutex_unlock(&path_status.at(x)->rtcpsr_thread_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

			time1 = now();
		}
		int64_t time_rtp_mut2 = now();
		fprintf(log_while, "%" PRId64 " %" PRId64 " \n",time_rtp_mut, time_rtp_mut2);

		if (pthread_mutex_lock(&path_status.at(x)->rtp_mutex_list) != 0)
			throw std::runtime_error("Mutex lock failed");

		if(path_status.at(x)->packet.begin() != path_status.at(x)->packet.end())
		{
			//		printf("%d It is necessary to remove packet %" PRIu16 " from vector \n", x,	path_status.at(x)->packet.front()->seq);
			path_status.at(x)->packet.erase(path_status.at(x)->packet.begin());
					fprintf(log_all,"%d After remove packet %" PRIu16 " from vector \n", x,	path_status.at(x)->packet.front()->seq);
			//		printf("path_status.at(x)->packet. size = %zu \n", path_status.at(x)->packet.size());
		}
		else
		{

			if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex_list) != 0)
				throw std::runtime_error("Mutex unlock failed");

			continue;
		}

		if (pthread_mutex_unlock(&path_status.at(x)->rtp_mutex_list) != 0)
			throw std::runtime_error("Mutex unlock failed");

	}
	return NULL;
}

void MPRTP_Init_subflow(struct mprtphead * mrheader, uint16_t x)
{
	mrheader->prospec = htons(RTP_H_EXTID);
	mrheader->wordlen = htons(2);
	mrheader->hextid = htons(MPR_H_EXTID);
	mrheader->len = htons(6);
	mrheader->subflowid = htons(x);
	mrheader->subflowseq = htons(0);
	mrheader->mprtype = htons(MPR_TYPE_SUBFLOW);
}

int64_t waiting(int64_t wake)
{
	int64_t time_now = now();
//	fprintf(log_all, "wake - time_now = %" PRId64 "\n", wake - time_now);

	if (wake > time_now)
	{
		if(usleep(wake - time_now) == -1)
			printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> \n");
	}
	else
		printf("/////////////////////////////////////////// \n");

	return wake - time_now;

}

void *send_rtcp(void *arg)
{
	int x = (long) ((int *) arg);

	struct sockaddr_in rtcpsender;
	memset((char *) &rtcpsender, 0, sizeof(rtcpsender));
	rtcpsender.sin_family = PF_INET;
	rtcpsender.sin_port = htons(path_status.at(x)->txport + 1);

	if (inet_aton((char *) path_status.at(x)->ip, &rtcpsender.sin_addr) == 0)
	{
		fprintf(stderr, "inet_aton() failed\n");
		pthread_exit(NULL);
	}

	struct timeval timenow;

	struct timespec timeout;

	while (term_flag == 0)
	{
		if (pthread_mutex_lock(&path_status.at(x)->rtcpsr_thread_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		while (path_status.at(x)->rtcpsr.empty() == 1 && term_flag == 0)
		{
			//	printf("waiting for a signal rtcp_thread\n");
			//		pthread_cond_wait(&path_status.at(x)->rtcp_cond,&path_status.at(x)->rtcpsr_thread_mutex);
			gettimeofday(&timenow, NULL);
			timeout.tv_sec = 30 + timenow.tv_sec;
			timeout.tv_nsec = 0;

			int ret = 0;
			ret = pthread_cond_timedwait(&path_status.at(x)->rtcp_cond, &path_status.at(x)->rtcpsr_thread_mutex, &timeout);

			if (ret == ETIMEDOUT)
				printf("While waiting rtcp, %d %d : %s \n", x, errno, strerror(errno));
		}

		if (pthread_mutex_unlock(&path_status.at(x)->rtcpsr_thread_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		int send_byte = sendto(path_status.at(x)->rtcp_sock, path_status.at(x)->rtcpsr.front(), sizeof(path_status.at(x)->rtcpsr.front()),
				0, (struct sockaddr *) &rtcpsender, sizeof(rtcpsender));

/*		fprintf(log_all, "Send RTCP SR on path %d sent_bytes %d \n", x, send_byte);

		fprintf(log_all, "Send RTCP SR on path %d, rtp count = %" PRIu32 ", ts = %" PRIu32 " \n",
				 x, path_status.at(x)->rtcpsr.front()->sender_oc, path_status.at(x)->rtcpsr.front()->rtpts);
		fprintf(log_all, "RTCP send erase it %" PRIu32 " \n",
				path_status.at(x)->rtcpsr.front()->sender_oc);
		fprintf(log_all, "RTCP size %zu \n", path_status.at(x)->rtcpsr.size());
*/
		if (pthread_mutex_lock(&path_status.at(x)->rtcpsr_mutex_list) != 0)
			throw std::runtime_error("Mutex lock failed");

		if(path_status.at(x)->packet.begin() != path_status.at(x)->packet.end())
		{
			path_status.at(x)->rtcpsr.erase(path_status.at(x)->rtcpsr.begin());
		}
		else
		{
			if (pthread_mutex_unlock(&path_status.at(x)->rtcpsr_mutex_list) != 0)
						throw std::runtime_error("Mutex lock failed");
			continue;
		}

		if (pthread_mutex_unlock(&path_status.at(x)->rtcpsr_mutex_list) != 0)
			throw std::runtime_error("Mutex lock failed");

		/*				fprintf(log_all, "RTCP size after %zu \n",
						path_status.at(x)->rtcpsr.size());
				fprintf(log_all, "RTCP send after erase it %" PRIu32 " \n",
						(*it)->sender_oc);
		 */
	}

	return NULL;
}

int getrtcpsr(int64_t time2, int x)
{
	struct rtcpsrbuf * rtcpsr = NULL;

	rtcpsr = new struct rtcpsrbuf;

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

	rtcpsr->rtpts = htonl(0);

	rtcpsr->sender_oc = path_status.at(x)->sender_oc;
	rtcpsr->sender_pc = path_status.at(x)->sender_pc;

	if (pthread_mutex_lock(&path_status.at(x)->rtcpsr_mutex_list) != 0)
		throw std::runtime_error("Mutex lock failed");

	path_status.at(x)->rtcpsr.push_back(rtcpsr);

	if (pthread_mutex_unlock(&path_status.at(x)->rtcpsr_mutex_list) != 0)
		throw std::runtime_error("Mutex lock failed");

/*	fprintf(log_all, "ntpts = %" PRIu32 " \n"
	"sender_pc  = %" PRIu32 " \n"
	"sender_oc  = %" PRIu32 " \n"
	"ntps = %" PRIu32 " \n"
	"ntps_frac%" PRIu32 " \n", ntpts, rtcpsr->sender_pc, rtcpsr->sender_oc,
			ntpts, tmp1);
*/

	fprintf(log_rtcp_s, "%d  %" PRId64 "   %10" PRIu32 " %10" PRIu32 " \n", x, time2, rtcpsr->sender_oc, rtcpsr->sender_pc);

	fprintf(log_all, " Time of sending RTCP Report %" PRId64 " : %" PRId64 " \n", (int64_t) rtcpsr->ntpts, (int64_t) rtcpsr->ntpts_frac);
	return 28;
}

void* recv_rtcp(void *arg)
{
	int x = (long) ((int *) arg);

	struct sockaddr_in reciever;
	socklen_t reciever_size;
	struct rtcprrbuf rtcpbuf;

	struct rtcprrbuf *rtcprr = NULL;
	struct rtcprrbuf *p = NULL;

	struct timeval timeout;

	printf("path_status.at(x)->rtcp_sock = %d \n", path_status.at(x)->rtcp_sock);

	while (path_status.at(x)->rtcp_sock == 0)
		usleep(100000);

//	int sock = path_status.at(x)->rtcp_sock;

	/*check for rtcp rr*/

	while (term_flag == 0)
	{
		timeout.tv_sec = 0.5*60;
		timeout.tv_usec = 0;

		int sock = path_status.at(x)->rtcp_sock;

		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(sock, &rset);

		int sel = select(sock + 1, &rset, NULL, NULL, &timeout);

		if (sel < 0)
		{
			perror("error in select!");
			throw std::runtime_error("Bad");
		}
		else
		{
			if (FD_ISSET(sock, &rset) == 1)
			{
				reciever_size = sizeof reciever;

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
				//		fprintf(log_all, "RTCP RR received...\n");

						rtcprr = new struct rtcprrbuf;
						memset(rtcprr, 0, sizeof(rtcprr));

						rtcprr_list(rtcprr, &rtcpbuf, x);
						path_status.at(x)->rrcount++;

						//				fprintf(SB_log, "RRCOUNT = %d\n", path_status.at(x)->rrcount);

						/*Set RTCP_RR_flag to true after receiving the first RTCP packet*/
						if (path_status.at(x)->rrcount == 0)
							RTCP_RR_flag = 0;
						else
						{
							if (path_status.at(x)->rrcount == 1)
							{
								RTCP_RR_flag = 1;
								fprintf(log_all, "RTCP_RR_flag = 1\n");
							} else {
								RTCP_RR_flag = 2;
								fprintf(log_all, "RTCP_RR_flag = 1\n");
							}
						}

						if (pthread_mutex_lock(&path_status.at(x)->rtcprr_thread_mutex) != 0)
							throw std::runtime_error("Mutex lock failed");

						int64_t get_time = now();
						/*If the size of a RTCP RR list is the size of sliding window, delete the first element from a list*/

						if(get_time > Sliding_window + buffer_time)
			//			if(rtcp_rr_packets.at(x).size() == Sliding_window)
						{
							std::list<struct rtcprrbuf *>::iterator itb = rtcp_rr_packets.at(x).begin();
							p = *itb;
							itb = rtcp_rr_packets.at(x).erase(itb);
							delete p;
							buffer_time = now();
							fprintf(log_all, "Delete the rtcp packet from list \n");
						}

						rtcp_rr_packets.at(x).push_back(rtcprr);
						fprintf(log_all, "%d Size of a rtcprr list = %zu \n", x,	rtcp_rr_packets.at(x).size());

						 fprintf(log_all,
						 "************************************************************\n");
				//		 fprintf(log_all, "%d bnd just after filling a list %f \n", x, rtcp_rr_packets.at(x).back()->Bnd);


						path_status.at(x)->rtp_oc_count = 0;

						if (pthread_mutex_unlock(&path_status.at(x)->rtcprr_thread_mutex) != 0)
							throw std::runtime_error("Mutex unlock failed");

				//		fprintf(log_all, "%d RTCP RR term_flag =%d \n", x, term_flag);
						fprintf(log_all, "%d rrcount in recv_rtcp %d \n", x, path_status.at(x)->rrcount);
					}
				}
			}
			//		else
			//			printf("Don't see handle of a socket!!!\n");

		}
	}
	return NULL;
}

void rtcprr_list(struct rtcprrbuf * rtcprr, struct rtcprrbuf * buf, int x)
{
	int64_t rrtime = 0;
	int64_t delta_t = 0;
	rrtime = now();
	p_rrtime = rrtime;
	rtcprr->rrtime = rrtime;

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

	// calculate RTT in microsec

	int64_t lsr = (int64_t) rtcprr->lsr_frac * 1000000 / 65536 + (int64_t) rtcprr->lsr * 1000000;
	rtcprr->rtt = rrtime - (int64_t) rtcprr->dlsr * 1000000 - (int64_t) rtcprr->dlsr_frac * 1000000 / 65536 - lsr;

	path_status.at(x)->total_lossrate = rtcprr->totallost - 1;

	//calculate PLR
	rtcprr->lossrate = ((double) (rtcprr->fraclost)) / 256;

	uint32_t payload = (path_status.at(x)->sender_oc - path_status.at(x)->lastsender_oc) * 8;
	fprintf(log_rtcp_r,
			"% d  %" PRId64 " %16" PRIu32 " %16f %16" PRId32 "   %16" PRIu32 " %16" PRIu16 ":%" PRIu16 " %16" PRIu16 ":%" PRIu16 " %16" PRIu32 "\n",
			x, rrtime, rtcprr->ehsn, rtcprr->lossrate,
			(int32_t) rtcprr->totallost, rtcprr->jitter, rtcprr->lsr,
			rtcprr->lsr_frac, rtcprr->dlsr, rtcprr->dlsr_frac,
			path_status.at(x)->sender_pc);

	if (path_status.at(x)->rrcount == 0)
	{
		fprintf(log_all, "p_rrtime - st0 = %" PRId64 " \n", p_rrtime - st0);
		delta_t = p_rrtime - st0;
		fprintf(log_all,"1 delta_t = %" PRId64 "\n", delta_t);

		rtcprr->Bnd = ((payload * (1 - rtcprr->lossrate)) * 1000000 / delta_t);

		fprintf(log_all, "Bnd  = %" PRId64 "'n", rtcprr->Bnd);
		fprintf(path_par,
				"%d %10" PRId64 " %9.6f %17" PRId64 " %14" PRIu32 " %14" PRIu32 " %14" PRIu32 " %18" PRId64 " %10" PRId64 " %7" PRIu32 ""
						" %7" PRIu32 " %7s %7" PRId32 " \n",
				x, rtcprr->rtt, rtcprr->lossrate, rtcprr->Bnd, payload,
				path_status.at(x)->sender_oc * 8,
				path_status.at(x)->lastsender_oc * 8, p_rrtime,
				delta_t, rtcprr->ehsn, path_status.at(x)->last_ehsn, "0",
				(int32_t) rtcprr->totallost);
	}

	if (path_status.at(x)->rrcount > 0  && path_status.at(x)->lastsender_oc != path_status.at(x)->sender_oc)
	{
		// Calculate Bandwidth on a path
		fprintf(log_all, "p_rrtime = %" PRId64 " path_status.at(x)->last_rrtime = %" PRId64 "\n", p_rrtime, path_status.at(x)->last_rrtime);

		delta_t = p_rrtime - path_status.at(x)->last_rrtime;
		fprintf(log_all,"2 delta_t = %" PRId64 "\n", delta_t);

		rtcprr->Bnd = (payload * (1 - rtcprr->lossrate)) * 1000000 / delta_t;
		uint32_t dif_pc = path_status.at(x)->sender_pc - path_status.at(x)->lastsender_pc;

		fprintf(log_all,
				"%d Bnd = %" PRId64 ", sender_oc (%f), lastsender_oc (%f), sender_pc (%f), last_sender_pc (%f), ehsn(%" PRIu32 ") \n",
				x, rtcprr->Bnd, (double) path_status.at(x)->sender_oc,
				(double) path_status.at(x)->lastsender_oc,
				(double) path_status.at(x)->sender_pc,
				(double) path_status.at(x)->lastsender_pc, rtcprr->ehsn);

		fprintf(log_all, "Totallost = %" PRId32 ", fraclost = %f  \n", (int32_t) rtcprr->totallost, (double) rtcprr->fraclost);

		fprintf(path_par,
				"%d %10" PRId64 " %9.6f %17" PRId64 " %14" PRIu32 " %14" PRIu32 " %14" PRIu32 " %18" PRId64 " %10" PRId64 " %7" PRIu32 " "
						"%7" PRIu32 " %7" PRId32 " %7" PRId32 "\n",
				x, rtcprr->rtt, rtcprr->lossrate, rtcprr->Bnd, payload,
				path_status.at(x)->sender_oc * 8,
				path_status.at(x)->lastsender_oc * 8, p_rrtime,
				delta_t, rtcprr->ehsn, path_status.at(x)->last_ehsn, dif_pc,
				(uint32_t)rtcprr->totallost);
	}

	/* save last received characteristics */
	path_status.at(x)->last_rrtime = p_rrtime;
	path_status.at(x)->lastsender_oc = path_status.at(x)->sender_oc;
	path_status.at(x)->lastsender_pc = path_status.at(x)->sender_pc;
	path_status.at(x)->last_ehsn = rtcprr->ehsn;
}

int UDP_Init(int x, int localport, char * ipout)
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
		printf("UDP socket has been initialized %s:%d\n", ipout, localport);
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
	struct timeval timeout;
	int bytes_sent = 0;

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	int64_t delay = timeout.tv_sec * 1000000 + timeout.tv_usec;

	memset((char *) &udpserver, 0, sizeof(udpserver));
	udpserver.sin_family = PF_INET;
	udpserver.sin_port = htons(path_status[x]->txport);

	if (inet_aton((char *) path_status[x]->ip, &udpserver.sin_addr) == 0)
	{
		fprintf(stderr, "inet_aton() failed\n");
		pthread_exit(NULL);
	}

	MPRTP_update_subflow(mrheader.at(x), packet->buf, x);

	if (packet->seq == 1 && packet->frame_number == 1)
		initial_sent_time = now();

	int64_t timenow = now();

//	fprintf(log_all, "timenow %" PRId64 " - ideal %" PRId64 " - delay = %" PRId64 "\n",
//			timenow, st0 + packet->ts, timenow - (st0 + (int64_t)packet->ts));

	if(timenow - delay > (st0 + (int64_t)packet->ts))
	{
		/* Set the first symbol to "8" to show that this packets was dropped */
		printf("Select: timeout is expired \n");
		fprintf(log_rtp, "9 %4d %6d %6d %10" PRIu16 " %10" PRIu16 " %10d %10" PRIu32 " %16" PRId64 " %16" PRId64 " %16" PRId64 " %16" PRId64 " "
				" \n",
				x, packet->packetlen, packet->payloadlen,
				ntohs(*(uint16_t*) (packet->buf + 2)), ntohs(*(uint16_t*) (packet->buf + 22)), packet->frame_number,
				ntohl(*(uint64_t*) (packet->buf + 4)), sent_time,
				st0 + packet->ts, packet->create_time, packet->alloc_time);

		fprintf(log_all, "Timeout Subflowseq for path %d = %" PRIu16 "\n", x,	ntohs(*(uint16_t*) (packet->buf + 22)));

		return 0;
	}
	else
	{
		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(fds, &rset);

		int sel = select(fds + 1, NULL, &rset, NULL,  &timeout);

		if (sel == -1)
		{
			perror("Error in select!");
			throw std::runtime_error("Bad");
		}
		else
		{
			/*		if(sel == 0)
			{
				printf("Select: timeout is expired \n");
				/* Set the first symbol as "f" to show that this packets was dropped */
			/*				fprintf(log_rtp, "9 %4d %6d %6d %10" PRIu16 " %10d %10" PRIu32 " %16" PRId64 " %16" PRId64 " %16" PRId64 " %16" PRId64 " "
						" 0  \n",
						x, packet->packetlen, packet->payloadlen,
						ntohs(*(uint16_t*) (packet->buf + 2)), packet->frame_number, ntohl(*(uint64_t*) (packet->buf + 4)), sent_time,
						st0 + packet->ts, packet->create_time, packet->alloc_time);
				return 0;

			}
			 */	if (FD_ISSET(fds, &rset))
			 {

				 /* We can use sendto in non-blocking way for rescheduling information for another path (using MSG_DONTWAIT flag)
				  * (Don't forget to check the return value after that!)*/

				 sent_time = now();

				 bytes_sent = sendto(fds, packet->buf, packet->packetlen, MSG_DONTWAIT, (struct sockaddr *) &udpserver, sizeof(udpserver));

 				 if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EMSGSIZE))
				 {
					 printf("Buffer is overloaded...\n");
					 /*			printf("While sending rtp file frame %d packet %" PRIu16 " : %d, %s\n",
						packet->frame_number, packet->seq, errno, strerror(errno));
				fprintf(log_all, "Buffer is overloaded...\n");
				fprintf(log_all, "While sending rtp file frame %d packet %d : %d, %s\n", packet->frame_number, packet->seq, errno,
						strerror(errno));

				bytes_sent = packet->packetlen;
				printf("bytes_sent = packetlen = %d \n", bytes_sent);
				fprintf(log_rtp, "LOST %4d %6d %10" PRIu16 " %10d %10" PRIu32 " %16" PRIu64 " %16" PRIu64 "\n",
						x, packet->packetlen, packet->seq, packet->frame_number,
						packet->ts, sent_time, st0 + packet->ts );
					  */
					 return 0;
				 }
				 else
					 /* Set the first symbol to "9" to show that this packets was sended */
					 fprintf(log_rtp, "8 %4d %6d %6d %10" PRIu16 " %10" PRIu16 " %10d %10" PRIu32 " %16" PRId64 " %16" PRId64 " %16" PRId64 " %16" PRId64 " "
							 " 0\n",
							 x, packet->packetlen, packet->payloadlen,
							 ntohs(*(uint16_t*) (packet->buf + 2)), ntohs(*(uint16_t*) (packet->buf + 22)), packet->frame_number, ntohl(*(uint64_t*) (packet->buf + 4)), sent_time,
							 st0 + packet->ts, packet->create_time, packet->alloc_time);

				 fprintf(log_all, "Send Subflowseq for path %d = %" PRIu16 "\n", x,	ntohs(*(uint16_t*) (packet->buf + 22)));

			 }
		}
	}
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

			for (uint i = 0; i < thread_path.size(); i++)
			{
				if (thread_path.at(i) == SENT)
				{
					fprintf(log_path, "%d 	%5" PRIu16 "		%5" PRIu32 " \n", i,
							(*it)->seq, (*it)->ts);
					//			fprintf(log_all, "%d 	%" PRIu16 "		was scheduled ROUND ROBIN \n", i, (*it)->seq);
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
//	fprintf(log_all,"probe fb_interval = %f \n", probe);

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
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

			if (count != NUM)
				continue;
			else
			{
				//if it is the first packet of Frame
				if (ts == -1)
					ts = (*it)->ts;
			}

			//		fprintf(log_all, "ts of the frame in scheduling %d \n", ts);
			if (ts == (*it)->ts)
			{
				for (uint i = 0; i < (*it)->path.size(); i++)
				{
					(*it)->path.at(i) = SENT;
					thread_path.at(i) = SENT;
					fprintf(log_path, "!!!%d 	%5" PRIu16 "		%5" PRIu32 " \n", i,
							(*it)->seq, (*it)->ts);

					//				fprintf(log_all, "%" PRId64 " Scheduled %7d %7" PRIu16 " %16d \n", t, i, (*it)->seq, (*it)->frame_number);
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

vector<path_t> packets_path_scheduling(const vector<path_t>& lastpath)
{
	thread_path = lastpath;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0)
	{
		if (pthread_mutex_lock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");

		for (std::list<struct rtppacket *>::iterator it = packets_to_send.begin(); it != packets_to_send.end(); ++it)
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);

			if (count != NUM)
				continue;
			else
			{
				for (uint i = 0; i < (*it)->path.size(); i++)
				{
					(*it)->path.at(i) = SENT;
					thread_path.at(i) = SENT;
					fprintf(log_path, "!!!%d %5" PRIu16 " %5" PRIu32 " \n", i, (*it)->seq, (*it)->ts);
				}
				break;
			}
		}
		if (pthread_mutex_unlock(&list_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
	else
	{
		//		path_smart_select(lastpath);
		thread_path = packets_path_select(lastpath);
	}
	return thread_path;
}

vector<path_t> packets_path_select(const vector<path_t>& lastpath)
		{
	if (pthread_mutex_lock(&list_mutex) != 0)
		throw std::runtime_error("Mutex unlock failed");

	thread_path = lastpath;

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
					fprintf(log_path, "%d 	%5" PRIu16 "		%5" PRIu32 " \n", i,
							(*it)->seq, (*it)->ts);
					//				fprintf(log_all, "%d 	%" PRIu16 "		was scheduled ROUND ROBIN \n", i, (*it)->seq);
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
	double AgB = 0;
	double AvB_total = 0;
	double sum_of_SB = 0;
	double Add_SB = 0;
	double Media_rate = 0;

	std::vector<double> path_sum_Bnd(NUM, 0);
	std::vector<std::pair<double, int> > SAvB(NUM, std::make_pair(0.0, 0));
	std::vector<std::pair<double, int> > SB_total(NUM, std::make_pair(0.0, 0));

	for (uint i = 0; i < NUM; i++)
	{
		if (pthread_mutex_lock(&path_status.at(i)->rtcprr_thread_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (rtcp_rr_packets.at(i).empty() == 1)
		{
			fprintf(log_all, "rtcp_rr_packets for %d %zu \n ", i,rtcp_rr_packets.at(i).size());

			if (pthread_mutex_unlock(&path_status.at(i)->rtcprr_thread_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

			if (pthread_mutex_lock(&media_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			Media_index.first = 0;
			Media_index.second = 0;

			SB_total.at(i).second = i;
			//		SB_total.at(i).first = simple;

			SB_total.at(i).first = Media_bitrate.at(Media_index.first).at(Media_index.second);

			if (pthread_mutex_unlock(&media_mutex) != 0)
				throw std::runtime_error("Mutex lock failed");

			if (i == NUM - 1)
			{
				for (uint j = 0; j < SB_total.size(); j++)
					fprintf(log_all, "1 @SB_total %d %f \n",
							SB_total.at(j).second, SB_total.at(j).first);

				return SB_total;
			}
			else
				continue;
		}
		else
		{
			/*Calculate aggregated bandwidth for all paths*/
			//	for(int j = 0; j < NUM; j++)
			fprintf(log_all, "! rtcp_rr_packets for %d %zu \n ", i,rtcp_rr_packets.at(i).size());

			AgB += rtcp_rr_packets.at(i).back()->Bnd;

			fprintf(log_all, " Aggregated Bandwidth = %f \n", AgB);

			fprintf(log_all, "&%d Size of rtcp_rr list =%zu \n", i, rtcp_rr_packets.at(i).size());
			fprintf(log_all, "1 AvB for path %d = %f \n", i, AvB.at(i).first);
			fprintf(log_all, "%d Bnd from report = %" PRId64 " \n", i, rtcp_rr_packets.at(i).back()->Bnd);

			if (common_loss.at(i).size() > 5)
			{
	//			printf("common_loss.at(%d).size = %zu before \n", i, common_loss.at(i).size());
				std::vector<double>::iterator itb = common_loss.at(i).begin();
				itb = common_loss.at(i).erase(itb);
//				printf("common_loss.at(%d).size = %zu after \n", i, common_loss.at(i).size());
			}
			else
			{
				fprintf(log_all,"%d Push the value %f to common_loss \n",i, rtcp_rr_packets.at(i).back()->lossrate);
				common_loss.at(i).push_back(rtcp_rr_packets.at(i).back()->lossrate);
			}

			for (std::list<struct rtcprrbuf *>::iterator it = rtcp_rr_packets.at(i).begin(); it != rtcp_rr_packets.at(i).end(); ++it)
			{
				/* find the sum of bandwidth values for path i in sliding window and write this sum to element of pair(path, Average_Bandwidth) */
				path_sum_Bnd.at(i) += (*it)->Bnd;
				fprintf(log_all, "%d path_sum_Bnd = %f \n", i, path_sum_Bnd.at(i));
			}

			AvB.at(i).second = i;
			AvB.at(i).first = path_sum_Bnd.at(i) / rtcp_rr_packets.at(i).size();
			fprintf(log_all, "2 AvB for path %d = %f \n", i, AvB.at(i).first);

			if (pthread_mutex_unlock(&path_status.at(i)->rtcprr_thread_mutex) != 0)
				throw std::runtime_error("Mutex unlock failed");

			fprintf(log_all, "1 Aggregated Bandwidth = %f \n", AgB);

		}
	}
	/*	else
	 fprintf(sched, "! AgB = %f  Media_rate = %f \n", AgB, Media_rate);
	 */

	SAvB.assign(NUM, std::make_pair(0.0, 0));
	SAvB = AvB;

	/*Sort AverageBandwidth values from min to max*/
	std::sort(SAvB.begin(), SAvB.end());

	/*Sum all values of Average Bandwidth for all paths*/
	for (uint j = 0; j < AvB.size(); j++)
	{
		fprintf(log_all, "!!!AvB path %d = %f \n", AvB.at(j).second, AvB.at(j).first);
		AvB_total += AvB.at(j).first;
	}

	fprintf(log_all, "AvB_total = %f \n", AvB_total);

	double paths_losses = 0;

	old_common = common;

	for (int i = 0; i < NUM; i++)
	{
		common.at(i) = std::accumulate(common_loss.at(i).begin(), common_loss.at(i).end(), 0.0);
		fprintf(log_all, "common.at(%d) = %f \n", i, common.at(i));
		paths_losses += common.at(i);
	}

	fprintf(log_all, "Calculated paths_losses = %f \n", paths_losses);


	if (paths_losses == 0)
	{
		if (pthread_mutex_lock(&media_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (Media_index.first < LAYERS - 1)
			Media_index.first++;
		else
		{
			if (Media_index.second < FPS - 1)
			{
				Media_index.second++;
				printf("Encrease Media_index %d %d \n", Media_index.first, Media_index.second);
				fprintf(log_all,"Encrease Media_index %d %d \n", Media_index.first, Media_index.second);
			}
		}
		if (pthread_mutex_unlock(&media_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
	else
	{
		int k = 0;
		for(int j = 0; j < NUM; j++)
		{
			if(common.at(j) != 0)
				k++;

			fprintf(log_all, "k = %d \n", k);

			if(k == NUM)
				media_index_calculation(AgB);
		}
	}


/*	for (int i = 0; i < NUM; i++)
	{
		//		printf("!!!common_loss.at(%d).size = %zu \n", i, common_loss.at(i).size());

		if (pthread_mutex_lock(&media_mutex) != 0)
			throw std::runtime_error("Mutex lock failed");

		if (common.at(i) == 0)
		{
			if (Media_index.first < LAYERS - 1)
				Media_index.first++;
			else
			{
				if (Media_index.second < FPS - 1)
					Media_index.second++;
				printf("Encrease Media_index %d %d \n", Media_index.first, Media_index.second);
			}
		}
		else
		{
			media_index_calculation(AgB);

			fprintf(log_all, "Media_rate %f\n", Media_bitrate.at(Media_index.first).at(Media_index.second));

			fprintf(log_all, "!Media_index %d %d \n", Media_index.first, Media_index.second);
		}

		if (pthread_mutex_unlock(&media_mutex) != 0)
			throw std::runtime_error("Mutex unlock failed");
	}
*/
	///////////////////////////////////////////////////////////////
	Media_rate = Media_bitrate.at(Media_index.first).at(Media_index.second);

	int64_t time_changed = now();

	if (Media_rate_prev != Media_rate)
	{
		fprintf(log_all, "Media rate has changed to %f!!!! \n", Media_rate);
		fprintf(sched, "%f %10" PRId64 " \n", Media_rate, time_changed);
		Media_rate_prev = Media_rate;
	}

	for (uint j = 0; j < SAvB.size(); j++)
	{
		fprintf(log_all, "Number of element SB_total %d \n", SAvB.at(j).second);
		fprintf(log_all, "SAvB.at(%d).first (%f) / AvB_total (%f)  = %f\n", SAvB.at(j).second, SAvB.at(j).first, AvB_total, SAvB.at(j).first / AvB_total);

		fprintf(log_all, "!common.at(%d) = %f \n", SB_total.at(j).second, common.at(SB_total.at(j).second));
		fprintf(log_all, "!old_common.at(%d) = %f \n", SB_total.at(j).second, old_common.at(SB_total.at(j).second));

		if(common.at(SB_total.at(j).second) == 0 || common.at(SB_total.at(j).second) <= old_common.at(SB_total.at(j).second))
		{
			SB_total.at(SAvB.at(j).second).first = (SAvB.at(j).first / AvB_total) * Media_rate;
			SB_total.at(SAvB.at(j).second).second = SAvB.at(j).second;

			fprintf(log_all, "For path %d SB = %f without extra \n", SB_total.at(SAvB.at(j).second).second, SB_total.at(SAvB.at(j).second).first);

		}
		else
		{
			SB_total.at(SAvB.at(j).second).first = (SAvB.at(j).first / AvB_total) * Media_rate * alpha;
			SB_total.at(SAvB.at(j).second).second = SAvB.at(j).second;

			fprintf(log_all, "For path %d SB = %f without extra in case of losses \n", SB_total.at(SAvB.at(j).second).second, SB_total.at(SAvB.at(j).second).first);
		}
	}

	/* If Sending Bit rate is smaller that Media Rate */
	int k = 0;
	for(uint h = 0; h < NUM; h++)
	{
//		printf("Common,at(%d) = %f \n", h, common.at(h));
		if(common.at(h) != 0)
			k++;
//		printf("k = %d \n", k);

	}

	while (1)
	{
		for (uint i = 0; i < NUM; i++)
		{
			if(common.at(i) == 0 || k == NUM)
			{
				sum_of_SB += Add_SB+1;
	//			fprintf(log_all, "Difference between MediaRate and sum_of_SB %f =  %f \n", sum_of_SB, Media_rate - sum_of_SB);

		//		printf("Media_rate (%f) - sum_of_SB (%f) = %" PRId64 "\n", Media_rate, sum_of_SB, (int64_t) (Media_rate * 100000000) - (int64_t) (sum_of_SB * 100000000));

	//			fprintf(log_all, "Media_rate (%f) - sum_of_SB (%f) = %" PRId64 "\n", Media_rate, sum_of_SB, (int64_t) (Media_rate * 100000000) - (int64_t) (sum_of_SB * 100000000));

				if ((int64_t) (Media_rate * 100000000) > (int64_t) (sum_of_SB * 100000000))
				{
					Add_SB = (SAvB.at(i).first / Media_rate) * (Media_rate - sum_of_SB);
					SB_total.at(i).first += Add_SB;
					//	printf("!2 sum_of_SB = %f \n", sum_of_SB);
	//				fprintf(log_all, "For path %d SB = %f with extra \n", SAvB.at(i).second, SB_total.at(i).first);
				}
				else
				{
					for (uint j = 0; j < SB_total.size(); j++)
						fprintf(log_all, "@!SB_total %d %f \n", SB_total.at(j).second, SB_total.at(j).first);

					return SB_total;
				}
			}
		}
	}

	for (uint j = 0; j < SB_total.size(); j++)
		fprintf(log_all, "@!!SB_total %d %f \n", SB_total.at(j).second, SB_total.at(j).first);
	return SB_total;
}

int create_packet()
{
	int payload = 0;
	uint16_t seq_fr = 0;

	for (std::list<struct trace *>::iterator it = frame_list.begin();it != frame_list.end();  )
	{
		fprintf(log_all, "frame in the list #%d \n", (*it)->number);
		if ((*it)->location == NONALLOC)
		{
	//		fprintf(log_all, "Media_index.first %d Media_index.second %d \n", Media_index.first, Media_index.second);

			for (int i = 0; i <= Media_index.first; i++)
				payload += (*it)->size.at(i).at(Media_index.second);

			if (payload == 0)
			{
				(*it)->location = ALLOC;

				fprintf(log_quantity, "%d   empty %s \n", (*it)->number, (*it)->type);

	//			printf("Frame #%d with zero payload \n", (*it)->number);

		//		return 0;
			}
			else
			{
				double q = payload / (double) (MTU - MPRTP_HEADER - 12);
				int quantity = 0;
				quantity = (int) ceil(q);

				for (int i = 0; i != quantity; i++)
				{
					/*Don't forget to consider MPRTP header in 12 bytes!*/
					if (payload <= (MTU - MPRTP_HEADER - 12))
					{
						struct rtppacket *packet = NULL;
						packet = new struct rtppacket;

						packet->create_time = now();
						packet->payloadlen = payload;
						packet->packetlen = packet->payloadlen + MPRTP_HEADER + 12; // plus rtp header
						packet->ts = (int32_t)((*it)->time * 1000000);
						packet->seq_fr = i;
						packet->seq = seq;
						packet->frame_number = (*it)->number;

						strcpy(packet->frame_type, (*it)->type);

						seq_fr = packet->seq_fr;
						seq++;

						memset(packet->buf + 12, 0, 12);
						*(uint16_t*) (packet->buf + 2) = htons(packet->seq);
						*(int32_t*) (packet->buf + 4) = htonl(packet->ts);

/*						fprintf(log_all,
								"1 Frame #%d packet_fr#%d packet#%" PRIu16 " payload %d \n",
								(*it)->number, packet->seq_fr, packet->seq,
								packet->payloadlen);
*/
						memset(packet->buf + 24, 1, packet->payloadlen);

						insert_data_to_list(packet);
					}
					else
					{
						struct rtppacket *packet = NULL;

						packet = new struct rtppacket;

						packet->create_time = now();
						packet->payloadlen = MTU - MPRTP_HEADER - 12;
						packet->packetlen = packet->payloadlen + MPRTP_HEADER
								+ 12;
						packet->ts = (int32_t)((*it)->time * 1000000);
						packet->seq_fr = i;
						packet->seq = seq;
						packet->frame_number = (*it)->number;
						strcpy(packet->frame_type, (*it)->type);

						seq++;
						seq_fr = packet->seq_fr;

						*(uint16_t*) (packet->buf + 2) = htons(packet->seq);
						*(int32_t*) (packet->buf + 4) = htonl(packet->ts);

						memset(packet->buf + 12, 0, 12);
						memset(packet->buf + 24, 1, packet->payloadlen);

	/*					fprintf(log_all,
								"2 Frame #%d packet_fr#%d packet#%" PRIu16 " payload %d \n",
								(*it)->number, packet->seq_fr, packet->seq,
								packet->payloadlen);
*/
						insert_data_to_list(packet);

						payload -= packet->payloadlen;
					}

					if (seq_fr == quantity - 1)
					{
						(*it)->location = ALLOC;
						int64_t t_finish = now();

						fprintf(log_quantity,
								"%d    %8" PRIu16 " %10" PRId64 " %10" PRId64 " %s\n",
								(*it)->number, quantity, t_finish - st0,
								(int64_t) ((*it)->time * 1000000), (*it)->type);

						break;
					}
				}
			}
			/*Erase frame from a list after creating packets on the basis of it*/

			if ((*it)->location == ALLOC)
			{
				it = frame_list.erase(it);
		//		printf("Erase frame %d \n", (*it)->number);
				return 0;
			}

		}
		else
		{
			++it;
			continue;
		}
	}
	return 0;
}

void open_bitrate_file() {
	FILE *fr = fopen("/home/ekaterina/experiment/bitrate", "r");
	if (fr == NULL) {
		printf("Read called without fr opening file\n");
		printf("While reading fr file : %d, %s\n", errno, strerror(errno));
		exit(0);
	}
	/* read from a bitrate trace file information
	 * put these values to vector Media_bitrate where strings reflects quality scalability
	 * and colomns reflects temporal scalability */

	for (uint j = 0; j < Media_bitrate.size(); j++) {
		for (uint i = 0; i < Media_bitrate.at(i).size(); i++) {
			if (fscanf(fr, "%lf", &Media_bitrate.at(j).at(i)) == EOF)
			{
				printf("Error during the procedure of reading Media_bitrate file: %d \n",errno);
				break;
			}
//			else
//				fprintf(log_all, "Bitrate[j=%d][i=%d] = %f \n", j, i, Media_bitrate.at(j).at(i));
		}
	}
	if (fclose(fr) == 0)
		printf("Bitrate file was successfully closed \n");
	else
		printf( "Error during the procedure of closing Media_bitrate file: %d \n",errno);
}

vector<path_t> path_SB_scheduling(std::vector<std::pair<double, int> > SB_total)
{
	double delta_t = 0;
	uint64_t tc = 0;

	/* Initial phase of an algorithm. Send the same packets to all paths until the first RR packets*/
	if (RTCP_RR_flag == 0)
	{
		std::list<struct rtppacket *>::iterator it = packets_to_send.begin();

		while (it != packets_to_send.end())
		{
			int count = std::count((*it)->path.begin(), (*it)->path.end(), INIT);
//			fprintf(log_all, "!!!count init	%d \n", count);

			if (count == NUM)
			{
				std::list<struct rtppacket *>::iterator itor = packets_to_send.begin();
//				fprintf(log_all, "it.ts =%" PRIu32 " it.seq = %" PRIu32 " itor.ts =%" PRIu32 " itor.seq =%" PRIu16 " \n",
//						(*it)->ts, (*it)->seq, (*itor)->ts, (*itor)->seq);

				int32_t init_time = (*itor)->ts;
				while ((*it)->ts == 0 || init_time == (*it)->ts)
				{
					for (uint i = 0; i < (*it)->path.size(); i++)
					{
						(*it)->path.at(i) = SENT;
						thread_path.at(i) = SENT;
						fprintf(log_path, "!!!%d 	%5" PRIu16 "	 %5d	%5d \n", i, (*it)->seq, (*it)->frame_number, (*it)->ts);
		//				fprintf(log_all, "!!!thread_path(%d)	%d \n", i, thread_path.at(i));
						(*it)->alloc_time = now();
		//				printf("%" PRId64 " Scheduled %7d %7" PRIu16 " %7d \n", (*it)->alloc_time, i, (*it)->seq, (*it)->frame_number);

						if (pthread_mutex_lock(&path_status.at(i)->rtp_mutex_list)!= 0)
							throw std::runtime_error("Mutex lock failed");

						path_status.at(i)->packet.push_back(*it);

						if (pthread_mutex_unlock(&path_status.at(i)->rtp_mutex_list) != 0)
							throw std::runtime_error("Mutex unlock failed");
					}

					for (uint i = 0; i < (*it)->path.size(); i++)
					{
						if ((*it)->path.at(i) == NSENT)
							(*it)->path.at(i) = DELETE;
					}

					int count_sent = std::count((*it)->path.begin(), (*it)->path.end(), SENT);
					if (count_sent == NUM)
					{
						it = packets_to_send.erase(it);

						if(packets_to_send.end() == it)
							return thread_path;
					}
					else
						++it;
				}
			}
			else
				++it;
		}
	}
	else
	{
		std::list<struct rtppacket *>::iterator it = packets_to_send.begin();

		while (it != packets_to_send.end())
		{
		//	fprintf(log_all,"!!!packet_to_send_size = %zu \n",	packets_to_send.size());

//			struct rtppacket * p;

			int count = std::count((*it)->path.begin(), (*it)->path.end(),	INIT);
	//		fprintf(log_all,"COUNT_INIT = %d \n", count);

			/*if packet in a list isn't scheduled yet*/
			if (count == NUM)
			{
				std::list<struct rtppacket *>::iterator itor = packets_to_send.begin();
				int32_t init_ts = (*itor)->ts;

				while ((*it)->ts == init_ts)
				{
					for (uint i = 0; i < SB_total.size();i++ )
					{
						tc = now();
//						fprintf(log_all,"%d tc - tp = %" PRId64 "\n", SB_total.at(i).second, tc - tp.at(SB_total.at(i).second));

						delta_t = (*it)->packetlen / SB_total.at(i).first;

						uint64_t delta_tc = delta_t * 1000000;

//						fprintf(log_all, "%d packetlen = %d #%" PRIu16 " \n", SB_total.at(i).second, (*it)->packetlen, (*it)->seq);
//						fprintf(log_all, "%d Sending Bit rate = %f \n", SB_total.at(i).second, SB_total.at(i).first);
//						fprintf(log_all, "delta_t = %f \n", delta_t);
//						fprintf(log_all, "%d delta_tc in Token Bucket = %" PRIu64 " \n", SB_total.at(i).second, delta_tc);

//						printf( "%d Sending Bit rate = %f \n", SB_total.at(i).second, SB_total.at(i).first);
//						printf( "%d delta_tc in Token Bucket = %" PRIu64 " \n", SB_total.at(i).second, delta_tc);

						if (tc - tp.at(SB_total.at(i).second) > delta_tc)
						{
							for (uint j = 0; j < (*it)->path.size(); j++)
								(*it)->path.at(j) = DELETE;

							(*it)->path.at(SB_total.at(i).second) = SENT;

							tp.at(SB_total.at(i).second) = tc;
							thread_path = (*it)->path;

							fprintf(log_path, "%d %5" PRIu16 " %5d %5d \n", SB_total.at(i).second, (*it)->seq,
									(*it)->frame_number, (*it)->ts);

							if (pthread_mutex_lock(&path_status.at(i)->rtp_mutex_list) != 0)
								throw std::runtime_error("Mutex lock failed");

/*							struct timespec timeout;
							struct timeval timenow;

							gettimeofday(&timenow, NULL);
							timeout.tv_sec = timenow.tv_sec;
							timeout.tv_nsec = 200000;

							int ret = pthread_mutex_timedlock(&path_status.at(i)->rtp_mutex_list, &timeout);
							if(ret == ETIMEDOUT)
							{
								printf( "Error during rtp mutex %s \n",  strerror(errno));
							}
							if(ret != 0)
								throw std::runtime_error("Mutex lock failed");
*/
							fprintf(log_all,"%d Put pointer to (*it) #%" PRIu16 " to vector fr_number %d \n",
									SB_total.at(i).second, (*it)->seq, (*it)->frame_number);

			//				printf("%d Put pointer to (*it) #%" PRIu16 " to vector fr_number %d \n",
			//						SB_total.at(i).second, (*it)->seq, (*it)->frame_number);

							(*it)->alloc_time = now();
							path_status.at(SB_total.at(i).second)->packet.push_back(*it);

							if (pthread_mutex_unlock(&path_status.at(i)->rtp_mutex_list) != 0)
								throw std::runtime_error("Mutex unlock failed");

							int count_sent = std::count((*it)->path.begin(), (*it)->path.end(), SENT);

							if (count_sent != 0)
							{
								it = packets_to_send.erase(it);
								if (it == packets_to_send.end())
								{
									return thread_path;
								}
							}
							else
								++it;
						}
						else
						{
							continue;
						}
					}
				}
			}
		}
	}
	return thread_path;

}

int media_index_calculation(double AgB)
{
	for (int i = FPS - 1; i >= 0; i--)
	{
		for (int j = LAYERS - 1; j >= 0; j--)
		{
			if (AgB > Media_bitrate.at(j).at(i))
			{
				if (pthread_mutex_lock(&media_mutex) != 0)
					throw std::runtime_error("Mutex lock failed");

				fprintf(log_all, "Media_bitrate.at(j=%d).at(i=%d) = %f \n", j, i, Media_bitrate.at(j).at(i));

				Media_index.first = j;
				Media_index.second = i;

				if (pthread_mutex_unlock(&media_mutex) != 0)
					throw std::runtime_error("Mutex unlock failed");

				return 1;
			}
			else
				continue;
		}
	}
	return 1;

}
