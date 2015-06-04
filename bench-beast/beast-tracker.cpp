
// OpenCV offline
#include "flycapture/FlyCapture2.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <string>
#include <fstream>
#include <iostream>
#include <array>
#include <cstring>

#include <stdio.h>
#include <math.h>
#include <highgui.h>
#include <time.h>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <comedilib.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <cstdlib>
//#include "eye_tracker_config.h"

#include <netdb.h>


using namespace FlyCapture2;
using namespace cv;
using namespace std;

comedi_t *devx;
comedi_t *devy;
#define SAMPLE_CT 5 // about as short as you can get
#define BUF_LEN 0x8000
#define COMEDI_DEVICE_AO "/dev/comedi0"

// initialize positions
float xpos = 0;
float ypos = 0;

double amplitude_x = 4000;
double amplitude_y = 4000;
double fudge_x = 2048;
double fudge_y = 2048;

const char *comdevice = COMEDI_DEVICE_AO;
const char *comdevice2 = COMEDI_DEVICE_AO;

int external_trigger_number = 0;

sampl_t datall[2][SAMPLE_CT];
sampl_t datax[SAMPLE_CT];
sampl_t datay[SAMPLE_CT];

lsampl_t dataxl[SAMPLE_CT];
lsampl_t datayl[SAMPLE_CT];

struct parsed_options_hold{
  char filename[256];
  double value;
  int subdevice;
  int channel;
  int aref;
  int range;
  int physical;
  int verbose;
  int n_chan;
  int n_scan;
  double freq;
};


char *cmd_src(int src,char *buf)
{
        buf[0]=0;

        if(src&TRIG_NONE)strcat(buf,"none|");
        if(src&TRIG_NOW)strcat(buf,"now|");
        if(src&TRIG_FOLLOW)strcat(buf, "follow|");
        if(src&TRIG_TIME)strcat(buf, "time|");
        if(src&TRIG_TIMER)strcat(buf, "timer|");
        if(src&TRIG_COUNT)strcat(buf, "count|");
        if(src&TRIG_EXT)strcat(buf, "ext|");
        if(src&TRIG_INT)strcat(buf, "int|");
        if(src&TRIG_OTHER)strcat(buf, "other|");

        if(strlen(buf)==0){
                sprintf(buf,"unknown(0x%08x)",src);
        }else{
                buf[strlen(buf)-1]=0;
        }

        return buf;
}


int comedi_internal_trigger_cust(comedi_t* device, int subdevice, int channelx, int channely, lsampl_t* dataxl, lsampl_t* datayl, int range, int aref)
{
   
  comedi_insn insn[2];
  comedi_insnlist il;
  
  il.n_insns=2;
  il.insns=insn;
  //lsampl_t datax[SAMPLE_CT];
  //lsampl_t datay[SAMPLE_CT];

  memset(&insn[0], 0, sizeof(comedi_insn));
  insn[0].insn = INSN_WRITE; //INSN_INTTRIG
  insn[0].subdev = subdevice;
  insn[0].data = dataxl;
  insn[0].n = SAMPLE_CT;
  insn[0].chanspec = CR_PACK(channelx,range,aref);

  memset(&insn[1], 0, sizeof(comedi_insn));
  insn[1].insn = INSN_WRITE; //INSN_INTTRIG
  insn[1].subdev = subdevice;
  insn[1].data = datayl;
  insn[1].n = SAMPLE_CT;
  insn[1].chanspec = CR_PACK(channely,range,aref);

  return comedi_do_insnlist(device, &il);
}


void dump_cmd(FILE *out,comedi_cmd *cmd)
{
        char buf[10];

        fprintf(out,"subdevice:      %d\n",
                cmd->subdev);

        fprintf(out,"start:      %-8s %d\n",
                cmd_src(cmd->start_src,buf),
                cmd->start_arg);

        fprintf(out,"scan_begin: %-8s %d\n",
                cmd_src(cmd->scan_begin_src,buf),
                cmd->scan_begin_arg);

        fprintf(out,"convert:    %-8s %d\n",
                cmd_src(cmd->convert_src,buf),
                cmd->convert_arg);

        fprintf(out,"scan_end:   %-8s %d\n",
                cmd_src(cmd->scan_end_src,buf),
                cmd->scan_end_arg);

        fprintf(out,"stop:       %-8s %d\n",
                cmd_src(cmd->stop_src,buf),
                cmd->stop_arg);
}



// tcpclient:
// A class that creates a socket to allow communication between machines
// This allows streaming data to another machine
class tcpclient{
private:
	int status;
	struct addrinfo host_info;
	struct addrinfo *host_info_list;
	int socketfd;
	const char *msg;	
	int len;
	ssize_t bytes_sent;
	ssize_t bytes_recieved;
	char incoming_data_buffer[100];


public:
	void initialize(const char* hostname, const char* port){
		// need to block out memory and set to 0s
		memset(&host_info, 0, sizeof host_info);
		std::cout << "Setting up structs..." << std::endl;
		host_info.ai_family = AF_UNSPEC;
		host_info.ai_socktype = SOCK_STREAM;
		status = getaddrinfo(hostname, port, &host_info, &host_info_list);
		if (status != 0) std::cout << "getaddrinfo error" << gai_strerror(status);
		
		std::cout << "Creating a socket... " << std::endl;
		socketfd = socket(host_info_list->ai_family, host_info_list->ai_socktype, host_info_list->ai_protocol);
		if (socketfd == -1) std::cout << "Socket error";

		std::cout << "Connecting..." << std::endl;
		status = connect(socketfd, host_info_list->ai_addr, host_info_list->ai_addrlen);
		if (status == -1) std::cout << "Connect error";
	}


	
};

// CStopWatch:
// A simple timer class with Start, Stop, and GetDuration function calls 
class CStopWatch{
private:
	clock_t start;
	clock_t finish;

public:
	double GetDuration() {return (double)(finish-start) / CLOCKS_PER_SEC;}
	void Start() {start = clock();}
	void Stop() {finish = clock();}

};


// Initialize global variables: These are necessary for GUI function
int max_solves_slider_max = 100;
int max_solves_slider;
int max_solves = 100;

Vec<int,4> coordinates;
float tracking_params[] = {0, 0, 0, 0};

int min_dist_slider_max = 100;
int min_dist_slider;
int min_dist;

int canny_threshold_slider_max = 255;
int canny_threshold_slider;
int canny_threshold;

int center_threshold_slider_max = 255;
int center_threshold_slider;
int center_threshold;

int min_radius_slider_max = 99;
int min_radius_slider;
int min_radius;

int max_radius_slider_max = 100;
int max_radius_slider;
int max_radius;

int med_blur_slider_max = 255;
int med_blur_slider;
int med_blur;

int bin_threshold_slider_max = 255;
int bin_threshold_slider;
int bin_threshold;

int rec_slider_max = 1;
int rec_slider;
int record_video;

int video_display_slider_max = 1;
int video_display_slider;
int video_display;

int run_program_slider_max = 1;
int run_program_slider = 1;
int run_program = 1;

int save_csv_slider_max = 1;
int save_csv_slider = 0;
int save_csv = 0;

int stream_data_slider_max = 1;
int stream_data_slider = 0;
int stream_data = 0;

bool isDrawing = false;
Point start, boxend;


// currentDateTime:
// Returns the current date and time
const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d-%H%M%S", &tstruct);

    return buf;
}


// drawBox:
// A simple drawing function that draws a box around the ROI during setup
void drawBox(Point start, Point boxend, Mat& img){
	imshow("set",img);
	Scalar color = (255,0,0);
	rectangle(img, start, boxend, color, 10, 8, 0);
	return;
}


// mouseEvent:
// Records coordinates for where box is drawn. Saves coordinates for ROI
void mouseEvent(int evt, int x, int y, int flags, void* param){
	if(isDrawing){
		if(evt==CV_EVENT_LBUTTONUP){
	    		printf("up %d %d\n",x,y);
	        	isDrawing = false;
		        boxend.x = x;
		        boxend.y = y;
		        cv::Mat* image  = static_cast<cv::Mat *>(param);
			drawBox(start, boxend, *image);
			coordinates[2] = boxend.x-start.x;
			coordinates[3] = boxend.y-start.y;
	        	return;
    		}
	}
	else{
		if(evt==CV_EVENT_LBUTTONDOWN){
	        	printf("down %d %d\n",x,y);
		        isDrawing = true;
			start.x = x;
		        start.y = y;
			coordinates[0] = start.x;
			coordinates[1] = start.y;
        		return;
    		}
	}
}



// Initialize Trackbars
void min_dist_trackbar(int, void*){
	if (min_dist_slider==0){
		min_dist_slider=1;
	}
	min_dist = (int) min_dist_slider;
}

void canny_threshold_trackbar(int, void*){
	if (canny_threshold_slider==0){
		canny_threshold_slider=1;
	}
	canny_threshold = (int) canny_threshold_slider;
}

void center_threshold_trackbar(int, void*){
	if (center_threshold_slider==0){
		center_threshold_slider=1;
	}
	center_threshold = (int) center_threshold_slider;
}

void min_radius_trackbar(int, void*){
	if (min_radius_slider==0){
		min_radius_slider=1;
	}
	min_radius = (int) min_radius_slider;
}

void max_radius_trackbar(int,void*){
	max_radius = (int) max_radius_slider;
	min_radius_slider_max = max_radius-1;
}

void med_blur_trackbar(int,void*){
	if (med_blur_slider % 2 == 0){ // blur requires even input
		med_blur_slider=med_blur_slider+1;
	}
	if (med_blur_slider < 1){
		med_blur_slider=1;
	}
	med_blur = (int) med_blur_slider;
}

void bin_threshold_trackbar(int,void*){
	bin_threshold = (int) bin_threshold_slider;
}

void run_program_trackbar(int,void*){
	run_program = (int) run_program_slider;
}

void rec_trackbar(int,void*){
	record_video = (int) rec_slider;
}

void video_display_trackbar(int,void*){
	video_display = (int) video_display_slider;
}

void save_csv_trackbar(int,void*){
	save_csv = (int) save_csv_slider;
}

void stream_data_trackbar(int,void*){
	stream_data = (int) stream_data_slider;
}

// Main function:
// This program will track the eye
int main(){
	// Setup comedi
        comedi_cmd cmdx;
	comedi_cmd cmdy;
        int err;
        int n,m, i;
        int total=0, n_chan = 0, freq = 80000;
        int subdevicex = -1;
	int subdevicey = -1;
        int verbose = 0;
        unsigned int chanlistx[2];
	unsigned int chanlisty[1];
        unsigned int maxdata_x;
        unsigned int maxdata_y;
        comedi_range *rng_x;
        comedi_range *rng_y;
        int ret;
        //struct parsed_options options;
        int fn;
        int aref = AREF_GROUND;
        int range = 0;
        int channelx = 0;
	int channely = 1;
        int buffer_length;
        subdevicex = -1;
	subdevicey = -1;
        /* n_chan = -1; */

        /* Use n_chan to select waveform (cheat!) */
        /* fn = n_chan; */

        /* Force n_chan to be 1 */
        n_chan = 2;

        devx = comedi_open(comdevice);
	devy = comedi_open(comdevice2);
        if(devx == NULL){
                fprintf(stderr, "error opening %s\n", comdevice);
                return -1;
        }
	
	if(devy == NULL){
		fprintf(stderr,"error opening %s\n", comdevice2);
		return -1;
	}
	
        if(subdevicex <0)
                subdevicex = comedi_find_subdevice_by_type(devx, COMEDI_SUBD_AO, 0);
        assert(subdevicex >= 0);
		
	if(subdevicey <0)
		subdevicey = comedi_find_subdevice_by_type(devy, COMEDI_SUBD_AO, 0);
	assert(subdevicey >= 0);
	

        
	maxdata_x = comedi_get_maxdata(devx, subdevicex, channelx);
        rng_x = comedi_get_range(devx, subdevicex, channelx, 0);
        fudge_x = (double)comedi_from_phys(0.0, rng_x, maxdata_x);
        amplitude_x = (double)comedi_from_phys(1.0, rng_x, maxdata_x) - fudge_x;

        maxdata_y = comedi_get_maxdata(devy, subdevicey, channely);
        rng_y = comedi_get_range(devy, subdevicey, channely, 0);
        fudge_y = (double)comedi_from_phys(0.0, rng_y, maxdata_y);
        amplitude_y = (double)comedi_from_phys(1.0, rng_y, maxdata_y) - fudge_y;


	/*
        memset(&cmdx,0,sizeof(cmdx));
        cmdx.subdev = subdevicex;
        cmdx.flags = CMDF_WRITE;
        cmdx.start_src = TRIG_INT;
        cmdx.start_arg = 0;
        cmdx.scan_begin_src = TRIG_TIMER;
        cmdx.scan_begin_arg = 1e9 / freq;
        cmdx.convert_src = TRIG_NOW;
        cmdx.convert_arg = 0;
        cmdx.scan_end_src = TRIG_COUNT;
        cmdx.scan_end_arg = n_chan;
        cmdx.stop_src = TRIG_COUNT;
        cmdx.stop_arg = SAMPLE_CT;
        chanlistx[0] = CR_PACK(channelx, range, aref);
	chanlistx[1] = CR_PACK(channely, range, aref);
        cmdx.chanlist = chanlistx;
	cmdx.chanlist_len = n_chan;
	*/

	/*
	n_chan = 1;

	memset(&cmdx,0,sizeof(cmdx));
        cmdx.subdev = subdevicex; 
        cmdx.flags = CMDF_WRITE;
        cmdx.start_src = TRIG_INT; //trig_int
        cmdx.start_arg = 0;
        cmdx.scan_begin_src = TRIG_TIMER; //trig_timer
        cmdx.scan_begin_arg = 1e9 / freq;
        cmdx.convert_src = TRIG_NOW;
        cmdx.convert_arg = 0;
        cmdx.scan_end_src = TRIG_COUNT;
        cmdx.scan_end_arg = n_chan;
        cmdx.stop_src = TRIG_COUNT;
        cmdx.stop_arg = SAMPLE_CT;
        chanlistx[0] = CR_PACK(channelx, range, aref);
        cmdx.chanlist = chanlistx;        
	cmdx.chanlist_len = n_chan;
		


	memset(&cmdy,0,sizeof(cmdy));
        cmdy.subdev = subdevicey; //subdevicey
        cmdy.flags = CMDF_WRITE;
        cmdy.start_src = TRIG_INT; //TRIG_INT
        cmdy.start_arg = 0;
        cmdy.scan_begin_src = TRIG_TIMER;
        cmdy.scan_begin_arg = 1e9 / freq;
        cmdy.convert_src = TRIG_NOW;
        cmdy.convert_arg = 0;
        cmdy.scan_end_src = TRIG_COUNT;
        cmdy.scan_end_arg = n_chan;
        cmdy.stop_src = TRIG_COUNT;
        cmdy.stop_arg = SAMPLE_CT;
	chanlisty[0] = CR_PACK(channely, range, aref);        
	cmdy.chanlist = chanlisty;
	cmdy.chanlist_len = n_chan;
	*/
	

	/*
	if (verbose)
                dump_cmd(stdout,&cmdx);

        err = comedi_command_test(devx, &cmdx); //devx
        if (err < 0) {
                comedi_perror("comedi_command_test for x channel");
                std::cout << err << std::endl;
		exit(1);
        }
	*/

	/*
	err = comedi_command_test(devy, &cmdy);
        if (err < 0) {
                comedi_perror("comedi_command_test for y channel");
                exit(1);
        }
	*/

	/*
        if ((err = comedi_command(devx, &cmdx)) < 0) {
                comedi_perror("comedi_command x");
                std::cout << err << std::endl;
		exit(1);
        }
	*/

	/*
        if ((err = comedi_command(devy, &cmdy)) < 0) {
                comedi_perror("comedi_command y");
                exit(1);
        }

	*/
	
	/*
        n = SAMPLE_CT * sizeof(sampl_t);
        datax[SAMPLE_CT - 1] = fudge_x;
	datay[SAMPLE_CT - 1] = fudge_y;
        for(i=0; i<SAMPLE_CT; i++){
                if(i%10 < 5)
                        datay[i]=rint(fudge_y);
                else if(i%10 >=5)
                        datay[i]=rint(fudge_y+amplitude_y);
        }
        */
	
	/*
	m = write(comedi_fileno(devy), (void *)datay, n);
        if(m < 0){
                perror("write");
                exit(1);
        }else if(m < n)
        {
                fprintf(stderr, "failed to preload output buffer with %i bytes, is it too small?\n"
                        "See the --write-buffer option of comedi_config\n", n);
                exit(1);
        }

        if (verbose)
                printf("m=%d\n",m);

        ret = comedi_internal_trigger(devy, subdevicey,0);
        //ret = comedi_internal_triggerx(subdevicex,0);
	if(ret < 0){
                perror("comedi_internal_triggery\n");
                exit(1);
        }

        comedi_cancel(devx,subdevicex);
	comedi_cancel(devy,subdevicey);
	*/
	
	// initialize timer rec
	double delay;
	CStopWatch sw;
	
	
	
	// save file
	cout << "\nChoose a file name to save to. Defaults to current date and time...\n";
	string input = "";
	string filename;
	string video_filename;
	getline(cin, input);
	if (input == ""){
		filename = currentDateTime();
		video_filename = currentDateTime();
	}
	else{
		filename = input;
		video_filename = input;
	}

	filename.append(".csv");
	const char *filen = filename.c_str();
	
	ofstream save_file (filen);

	// Initialize camera for setup
	Error error;
	Camera camera;
	CameraInfo camInfo;

	// Connect to the camera
	error = camera.Connect(0);
	if(error != PGRERROR_OK){
		std::cout << "failed to connect to camera..." << std::endl;
		return false;
	}

	error = camera.GetCameraInfo(&camInfo);
	if (error != PGRERROR_OK){
		std::cout << "failed to get camera info from camera" << std::endl;
		return false;
	}

	std::cout << camInfo.vendorName << " "
			<< camInfo.modelName << " "
			<< camInfo.serialNumber << std::endl;
	
		error = camera.StartCapture();
	if(error==PGRERROR_ISOCH_BANDWIDTH_EXCEEDED){
		std::cout << "bandwidth exceeded" << std::endl;
		return false;
	}
	else if (error != PGRERROR_OK){
		std::cout << "failed to start image capture" << std::endl;
		return false;
	}
	



	// Setup: User draws rectangle around ROI
	//  Wait for 'c' to be pushed to move on
	// If user enters 'c' without drawing an ROI, use full image
	cout << "click and drag on image to select reference size\n";
	cout << "press c to continue\n";
	char kb = 0;	
	namedWindow("set",WINDOW_NORMAL);
	
	
	Image tmpImage;
	Image rgbTmp;
	cv::Mat tmp;
	while(kb != 'c'){
		// Grab frame from buffer
		Error error = camera.RetrieveBuffer(&tmpImage);
		if (error != PGRERROR_OK){
			std::cout<< "capture error" << std::endl;
			return false;
		}
		
		// Convert image to OpenCV color scheme
		tmpImage.Convert(FlyCapture2::PIXEL_FORMAT_BGR, &rgbTmp);

		unsigned int rowBytes = (double)rgbTmp.GetReceivedDataSize()/(double)rgbTmp.GetRows();

		tmp = cv::Mat(rgbTmp.GetRows(),rgbTmp.GetCols(),CV_8UC3,rgbTmp.GetData(),rowBytes);
	
		imshow("set",tmp);
	
	        cvSetMouseCallback("set", mouseEvent, &tmp);
		if (coordinates[3] != '\0'){
			drawBox(start, boxend, tmp);
			imshow("set",tmp);
		}
		kb = cvWaitKey(30);
	}
	// Set ROI to image size if no coordinates specified
	if (coordinates[0] == 0 and coordinates[1] == 0){
		coordinates[0] = 0;
		coordinates[1] = 0;
		coordinates[2] = tmp.cols;
		coordinates[3] = tmp.rows;
	}
	// Make sure coordinates array is in proper form:
	// elements 0 and 1 should be less than elements 2 and 3 respectively
	else{
		if (coordinates[0] > coordinates[2]){
			int hold = coordinates[0];
			coordinates[0] = coordinates[2]+coordinates[0];
			coordinates[2] = hold+coordinates[0];
		}
		if (coordinates[1] > coordinates[3]){
			int hold = coordinates[1];
			coordinates[1] = coordinates[3]+coordinates[1];
			coordinates[3] = hold+coordinates[1];
		}
	}
	destroyWindow("set");


	// Initialize variables for sliders
	
	int dp = 1;
	
	// Min Dist
	min_dist = 1;
	min_dist_slider = 1;
	
	// Canny threshold
	canny_threshold = 10;
	canny_threshold_slider = 10;
	
	// Center threshold
	center_threshold = 10;
	center_threshold_slider = 10;
	
	// Max radius
	max_radius = 140;
	max_radius_slider = 100;
	max_radius_slider_max = min(coordinates[2]-coordinates[0],coordinates[3]-coordinates[1])+100;

	// Min radius
	min_radius = 50;
	min_radius_slider = 50;
	min_radius_slider_max = max_radius_slider_max-1;

	// Binary threshold
	bin_threshold = 21;
	bin_threshold_slider = 21;

	// Blur
	med_blur = 75;
	med_blur_slider_max = min(coordinates[2]-coordinates[0],coordinates[3]-coordinates[1])-10;
	med_blur_slider = 75;

	// Video display
	video_display_slider_max = 1;
	video_display_slider = 1;
	video_display = 1;

	// Record video
	record_video = 0;
	rec_slider = 0;
	rec_slider_max = 1;
	
	// Set up window with ROI and offset
	Mat window;
	Rect myROI(coordinates[0],coordinates[1],coordinates[2],coordinates[3]);
	Vec<float,2> offset;
	offset[0] = coordinates[0];
	offset[1] = coordinates[1];
	
	// setup windows
	namedWindow("window",WINDOW_NORMAL);
	namedWindow("filtered",WINDOW_NORMAL);
	cvNamedWindow("control",WINDOW_NORMAL);
	resizeWindow("window",400,300);
	resizeWindow("filtered",250,200);
	resizeWindow("control",250,80);
	moveWindow("window",0,0);
	moveWindow("filtered",400,0);
	moveWindow("control",650,0);
	bool refresh = true;

	// Initialize video recorder
	VideoWriter vid;
	double fps = 20;
	Size S = Size((int) rgbTmp.GetCols(), (int) rgbTmp.GetRows());
	video_filename = video_filename.append("-video.avi"); 
	vid.open(video_filename,1196444237,fps,S,true);



	// make sliders
	createTrackbar("Min Distance", "control", &min_dist_slider,min_dist_slider_max,min_dist_trackbar);
	createTrackbar("Canny Threshold", "control", &canny_threshold_slider, canny_threshold_slider_max, canny_threshold_trackbar);
	createTrackbar("Center Threshold", "control", &center_threshold_slider,center_threshold_slider_max, center_threshold_trackbar);
	createTrackbar("Min Radius", "control", &min_radius_slider,min_radius_slider_max, min_radius_trackbar);
	createTrackbar("Max Radius", "control", &max_radius_slider,max_radius_slider_max, max_radius_trackbar);
	createTrackbar("Median blur", "control", &med_blur_slider, med_blur_slider_max, med_blur_trackbar);
	createTrackbar("Bin Threshold", "control", &bin_threshold_slider, bin_threshold_slider_max, bin_threshold_trackbar);
	createTrackbar("Display Video","control",&video_display_slider, video_display_slider_max, video_display_trackbar);
	createTrackbar("Record","control",&rec_slider,rec_slider_max,rec_trackbar);
	
	sw.Start(); // start timer
	char key = 0;
	
	int reset = 1000;
	int iter = 0;


	// initialize some more variables for comedi
        float xmax = 1280;
        float ymax = 960;


	// This is the main loop for the function
	while(key != 'q'){
		
		//start timer
		Image rawImage;
		Error error = camera.RetrieveBuffer( &rawImage );
		if (error != PGRERROR_OK ){
			std::cout << "capture error" << std::endl;
			continue;
		}
		
		Image rgbImage;
		rawImage.Convert(FlyCapture2::PIXEL_FORMAT_BGR, &rgbImage);
		// convert to OpenCV Mat
		unsigned int rowBytes = (double)rgbImage.GetReceivedDataSize()/(double)rgbImage.GetRows();
		Mat image = Mat(rgbImage.GetRows(), rgbImage.GetCols(),CV_8UC3, rgbImage.GetData(),rowBytes);

		// convert to gray	
		Mat image_gray;
		image_gray = image(myROI);
		
		// Pre-process
		cvtColor( image_gray, image_gray, CV_BGR2GRAY);
		blur(image_gray,image_gray,Size(med_blur,med_blur));
		threshold(image_gray, image_gray, bin_threshold, 255, THRESH_BINARY);
		GaussianBlur( image_gray, image_gray, Size(9, 9), 2, 2);


		// Apply Hough Transform to find circles
		vector<Vec3f> circles;
		HoughCircles( image_gray, circles, CV_HOUGH_GRADIENT, dp, min_dist, canny_threshold, center_threshold, min_radius, max_radius);
		

		//Draw circles detected
		float x=0;
		float y=0;
		float r=0;
		float centerX;
		float centerY;
		
		if (circles.size()>0){
			for( size_t i=0; i< circles.size(); i++)
			{
				x=x+circles[i][0];
				y=y+circles[i][1];
				r=r+circles[i][2];
			}
		
			centerX=cvFloor(x/circles.size()+offset[0]);
			centerY=cvFloor(y/circles.size()+offset[1]);
		
			Point center(cvRound(centerX), cvRound(centerY));
			
			if (video_display==1){
				//int radius = cvRound(r/circles.size());
				int radius = (min_radius+max_radius)/2;
				// circle center
				circle(image,center,3,Scalar(0,255,0),-1,8,0);
				// circle outline
				circle(image,center,radius,Scalar(0,0,255),3,8,0);
			}
		}
		else{
			centerX = 0;
			centerY = 0;
		}	
		

		xpos = (centerX/xmax)*amplitude_x+fudge_x;
		ypos = (centerY/ymax)*amplitude_y+fudge_y;
	
		//assert(err >= 0);
                n = SAMPLE_CT * sizeof(sampl_t);
                //datax[SAMPLE_CT - 1] = fudge_x;
		//datay[SAMPLE_CT - 1] = fudge_y;
                for(i=0; i<sizeof(datax); i++){
                        datax[i] = xpos;
			datay[i] = ypos;
			dataxl[i] = xpos;
			datayl[i] = ypos;
			//datall[0][i]=xpos;
			//datall[1][i]=ypos;
                }
                std::cout <<  "\r" << xpos << "," << ypos << std::flush;
	
		/*	
		err = comedi_command(devx, &cmdx);
		m = write(comedi_fileno(devx), (void *)datax, n);
                ret = comedi_internal_trigger(devx,subdevicex,0);
		
		err = comedi_command(devy, &cmdy);
		m = write(comedi_fileno(devy), (void *)datay, n);
		ret = comedi_internal_trigger(devy,subdevicey,0);
		
		usleep(1.1e1);

		comedi_cancel(devx, subdevicex);
		comedi_cancel(devy, subdevicey);
		*/
		
		//err = comedi_command(devx, &cmdx);	
		//m = write(comedi_fileno(devx), (void *)datall, n);
		ret = comedi_internal_trigger_cust(devx,subdevicex,channelx, channely,dataxl,datayl,range,aref);
		
		if (ret < 0){
			comedi_perror("insn error");
		}

		usleep(1.1e1);
		//comedi_cancel(devx,subdevicex);
		

		// Record the video - this is slow!!
		if (record_video == 1){
			vid.write(image);
			sw.Stop();
	                delay = sw.GetDuration();
		}
		

		if (video_display==1 or save_csv==1){
			if (video_display==1){
				imshow("window",image);
				imshow("filtered",image_gray);
			}
		}
		

		
		
		
		key = waitKey(1);
        	//save_file << centerX << "," << centerY << "," << delay << endl;
		sw.Start(); // restart timer
		
	}
	
}
