/*
 * main_send.h
 *
 *  Created on: Aug 27, 2013
 *      Author: streaming
 */

#ifndef MAIN_SEND_H_
#define MAIN_SEND_H_

enum path_t {INIT = 0, SENT = 1, NSENT = 2, DELETE = 3};
enum alloc {NONALLOC = 0, ALLOC = 1};							//deffirinciate the condition of a frame: allocated to a path or not

#define MPR_TYPE_SUBFLOW 0x00
#define MPR_H_EXTID 0x02
#define RTP_H_EXTID 0x00E8
#define EXT_MASK 0x10
#define RTCP_SR_B1 0x80
#define RTCP_SR_B2 0xC8 //200 in decimel
#define RTCP_RR_B2 0xC9
#define BUFLEN 2048
#define NUM 2
#define FPS 3
#define LAYERS 4

#define IP1 "127.0.0.1"
#define IP2 "127.0.0.1"
#define IP3 "127.0.0.1"
#define IPTX "127.0.0.1"

void getargs(int argc, char* argv[], char * filename, int * port, char ip[][20], char iptx[], int *txport,char ipout[][20]);
int create_threads(int n, char ipnew[][20], int * txportnew, char ipout[][20]);
void *rtp_send(void * arg);
void *recv_rtcp(void *arg);
void *send_rtcp(void *arg);
int openfile(char *filemane);
FILE open_tracefile (char *filename);
void open_bitrate_file();
int readpacket(struct rtppacket *packet,int fd);
int UDP_Init(int x, char * ip, int localport, char * ipout);
void SetnonBlocking(int s);
int readtrace(char *filename);
int create_packet();
int media_index_calculation(double Aggregated_bandwidth);

vector <path_t> path_select (const vector <path_t>& lastpath);
vector <path_t> path_scheduling (const vector <path_t>& lastpath);
vector <path_t> packets_path_scheduling(const vector<path_t>& lastpath);
vector<path_t> packets_path_select(const vector<path_t>& lastpath);

//Function for smart selection
vector<std::pair<double, int > > SB_calculation ();
vector <path_t> path_SB_scheduling(std::vector<std::pair<double, int > > SB_total);

void insert_data_to_frame_list(struct trace * frame);
void insert_data_to_list(struct rtppacket * packet);
void MPRTP_update_subflow(struct mprtphead * mrheader, char * buf, int x);
void MPRTP_Init_subflow(struct mprtphead * mrheader, uint16_t x);
void rtcprr_list(struct rtcprrbuf * rtcprr ,struct rtcprrbuf *rtcpbuf, int path);

int sending_rtp (int x, int fds, struct rtppacket * packet);
void waiting(const int64_t wake);
int64_t now();
int getrtcpsr (struct rtcpsrbuf * rtcpsr, int64_t time, int path);

void signalHandler(int sig);


struct rtppacket
{
public:
	rtppacket():dump_ts(0), payloadlen(0),packetlen(0),ts(0), seq(0), seq_fr(0), frame_number(0), erase(0), path(NUM, INIT){}
	uint32_t dump_ts;	/*timestamp of RTP dump. It is similar to timestamp of packet generation from the application*/
	int payloadlen;
	int packetlen;
	int32_t ts;				/*timestamp in RTP file*/
	uint16_t seq;		/* Sequance number in video sequence*/
	int seq_fr;	/* Sequance number in a frame*/
	int frame_number;
	char buf[1600];
	int erase;
	std::vector <path_t> path;       //Declare a vector of path_type elements
//	clock_t wait;		/*number of clocks to wait before sending packet*/
};

struct trace
{
public:
	trace(): time (0), number (0),  location(NONALLOC), size(LAYERS, std::vector<int> (FPS)),PSNRY(LAYERS, std::vector<double> (FPS)),
	PSNRU(LAYERS, std::vector<double> (FPS)), PSNRV(LAYERS, std::vector<double> (FPS)){}
	double time;
	int number;
	char type[10];
    alloc location;
    std::vector <std::vector<int> > size;
    std::vector <std::vector<double> > PSNRY;
    std::vector <std::vector<double> > PSNRU;
    std::vector <std::vector<double> > PSNRV;
};


struct status
{
	pthread_mutex_t rtp_mutex;
	pthread_cond_t rtp_cond;
	pthread_cond_t rtcp_cond;
	pthread_mutex_t rtcp_thread_mutex;
	uint16_t seq_num;
	uint32_t sender_pc; /*packet count*/
	uint32_t sender_oc; /*octet count*/
	uint32_t lastsender_oc;
	uint32_t lastsender_pc;
	uint32_t last_ehsn;
	int64_t last_rrtime;

	int16_t rtp_oc_count;  /*number of octets sent between two consecutive RR packet*/

	int rtcp_sock;
	int txport;
	int portout;
	char ip[20];
	char ipout[20];
	int allread;
	int obytes; /*outstanding bytes*/
	/*path characteristic variables*/
	int rrcount;
	int rrcount_prev;
	double total_lossrate;

};

struct rtcprrbuf
{
	uint8_t b1;
	uint8_t b2;
	uint16_t len; // the length of RTCP packet including the header and any padding
	uint32_t ssrc;	// synchronization source identifier
	uint32_t ssrc1; // source identifier to which information in reception report block pertains
	uint8_t fraclost;// The fraction of RTP data packets from ssrc_1 lost since the previous SR or RR packet was sent
	int totallost:24;// the total number of RTP data packets from ssrc_1 that have been lost since the beggining of reception
	uint32_t ehsn;
	uint32_t jitter;
	uint16_t lsr;
	uint16_t lsr_frac;
	uint16_t dlsr;
	uint16_t dlsr_frac;

	/* Additional information for path scheduling*/
	int path;
	int64_t rrtime;
	int64_t rtt;
	double lossrate;
	double Bnd;


	int rtp_int_sent; //???
	int sched_count;  //???
};

struct mprtphead
{
	uint16_t prospec;	//2
	uint16_t wordlen;	//2
	uint8_t hextid;		//1
	uint8_t len;		//1
	uint16_t mprtype;	//2
	uint16_t subflowid; //2
	uint16_t subflowseq;//2  = 12
};

struct rtcpsrbuf
{
//	pthread_mutex_t rtcpsr_mutex;
	uint8_t b1;
	uint8_t b2;
	uint16_t len;
	uint32_t ssrc;
	uint32_t ntpts;
	uint32_t ntpts_frac;
	uint32_t rtpts;
	uint32_t sender_pc; /*packet count*/
	uint32_t sender_oc; /*octet count*/
};

/*RTCP Common Functions*/
int64_t rtcp_interval(int members,
                        int senders,
                        double rtcp_bw,
                        int we_sent,
                        int packet_size,
                        int *avg_rtcp_size,
                        int initial,
                        double rtcp_min_time);

#endif /* MAIN_SEND_H_ */
