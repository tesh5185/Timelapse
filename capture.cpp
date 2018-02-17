/*
 *
 *  
 *
 *  
 * 
 *  
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <iostream>
#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>              /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <linux/videodev2.h>

#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
 //#include <opencv2/imgcodecs/imgcodecs.hpp>

#include <opencv2/photo/photo.hpp>
using namespace cv;
using namespace std;
//#define higher_rate
#define lower_rate
#define NUM_THREADS 3
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define COLOR_CONVERT
/*set resolution*/
#define HRES 640 
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"
#define frame_capture 0
#define compression 1
#define analysis 2
sem_t sem_capture;
sem_t sem_compress;
sem_t sem_jitter;
void compresstojpeg(void);
unsigned int *frameno;
struct timespec tstamp;
long int *time_s;
long int *time_ns;
struct timespec test1;
struct timespec test2;
long int *time_sc;
long int *time_nsc;
long int *time_scd;
long int *time_nscd;
//Mat loadagain;
long int comp_start_s[10000];
long int comp_end_s[10000];
long int comp_start_ns[10000];
long int comp_end_ns[10000];
// Format is used by a number of functions, so made as a file global
static struct v4l2_format fmt;

typedef struct threadParams_t
  {
  int threadIdx;
  } threadParams_t;




pthread_t threads[NUM_THREADS];
threadParams_t threadParams[NUM_THREADS];
pthread_attr_t rt_sched_attr[NUM_THREADS];
struct sched_param rt_param[NUM_THREADS];
struct sched_param nrt_param;
// Format is used by a number of functions, so made as a file global


enum io_method 
{
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
};

struct buffer 
{
        void   *start;
        size_t  length;
};

static char            *dev_name;
//static enum io_method   io = IO_METHOD_USERPTR;
//static enum io_method   io = IO_METHOD_READ;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format=1;
static int              frame_count = 106;

static void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg)
{
        int r;

        do 
        {
            r = ioctl(fh, request, arg);

        } while (-1 == r && EINTR == errno);

        return r;
}


char ppm_header[]="P6\n#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n255\n";
char ppm_dumpname[]="test00000000.ppm";
char jpg_header[]= "#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n";
char jpg_dumpname[]="comp00000000.jpg";
char ppm_uname[100];
struct utsname user;
static void dump_ppm(const void *p, int size, unsigned int tag, struct timespec *time)
{
    int written, i, total, dumpfd;
    *frameno=tag;
    snprintf(&ppm_dumpname[4], 9, "%08d", tag);
    strncat(&ppm_dumpname[12], ".ppm", 5);
  
	uname(&user);
    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);
    /*write a string for time and resolution and print into the ppm file*/
    //printf("Timestamp2: %ld sec %ld nsec\n",time->tv_sec,time->tv_nsec);
    snprintf(&ppm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&ppm_header[14], " sec ", 5);
    snprintf(&ppm_header[19], 11, "%010d", (int)((time->tv_nsec)/1000000));
    strncat(&ppm_header[29], " msec \n"HRES_STR" "VRES_STR"\n255\n", 19);
	/*write a string for uname and print into the ppm file*/
    memset(ppm_uname,0,sizeof(ppm_uname));    
    strncat(ppm_uname,"uname\n",sizeof("uname\n"));
	
    strncat(ppm_uname,user.sysname,sizeof(user.sysname));
    strncat(ppm_uname,user.nodename,sizeof(user.nodename));
    strncat(ppm_uname,user.release,sizeof(user.release));
    strncat(ppm_uname,user.version,sizeof(user.version));
    strncat(ppm_uname,user.machine,sizeof(user.machine));    
    
    written=write(dumpfd, ppm_header, sizeof(ppm_header));
    written=write(dumpfd, ppm_uname, sizeof(ppm_uname));
	/*different delays for 1hz and 10hz*/
    #ifdef higher_rate
    usleep(79700);
    #endif
    #ifdef lower_rate
    usleep(980400);
   #endif
    printf("here\n");
    total=0;
	

    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);
   clock_gettime(CLOCK_REALTIME, &test1); 
   printf("Timestamp 3: %ld sec %ld nsec\n",test1.tv_sec,test1.tv_nsec);
    
    //sem_wait(&sem_capture);
    printf("wrote %d bytes\n", total);
	/*close the ppm file and go to compress*/
    close(dumpfd);
    sem_post(&sem_compress);
}
void *compresstojpeg(void *threadp)
{
   unsigned int count=frame_count;
   int i=0;
   while (count>0)
    {   
    //wait for semaphore
    sem_wait(&sem_compress);	
    Mat loadagain;
	/*print the jpeg tag*/
    snprintf(&jpg_dumpname[4], 9, "%08d", *frameno);
    strncat(&jpg_dumpname[12], ".jpg", 5);
  
    loadagain = imread(ppm_dumpname,1);	//read the ppm frame
    imwrite( jpg_dumpname, loadagain); //write and compress in jpeg
     
	 /*store times in contiguous memory*/
    clock_gettime(CLOCK_REALTIME, &test2);
    *(time_sc+i)=test2.tv_sec;
    *(time_scd+i)=test1.tv_sec;
    *(time_nsc+i)=test2.tv_nsec;
    *(time_nscd+i)=test1.tv_nsec;
     count--;
     i++;
     printf("Timestamp 4: %ld sec %ld nsec\n",test2.tv_sec,test2.tv_nsec);
    sem_post(&sem_capture); // go into capture
    
   }
	sem_post(&sem_jitter); // go to the jitter thread
}
  double arr[10000];
  double arr1[10000];
void *jitteranalysis(void *threadp)
{
   
   sem_wait(&sem_jitter);
   float total_jitter=0;
   int i=0;
   double average_jitter;
   double *prejitter=arr;
   double *jitter=arr1;
   memset(jitter,0,frame_count);
   memset(prejitter,0,frame_count);
   printf ("Jitter analysis for capture thread\n");
   for(i=0;i<frame_count;i++)
   {
  	*(prejitter+i)=*(time_scd+i) +(*(time_nscd+i)/(1000000000.0));
	//printf("prejitter=%10.5f\n",*(prejitter+i));
   }
   for(i=1;i<frame_count;i++)
   {	
	//1 hz or 10hz
	#ifdef higher_rate
	*(jitter+i)=*(prejitter+i)-*(prejitter+i-1)-0.1;   //calcuate jitter
	#endif
	#ifdef lower_rate
	*(jitter+i)=*(prejitter+i)-*(prejitter+i-1)-1.0;
	#endif	
	printf("Jitter for frame %d is %10.5f\n",i,*(jitter+i));
	total_jitter+=*(jitter+i);
	
   }
	average_jitter=total_jitter/(frame_count-1);
	printf("Total latency= %9.5f\naverage jitter =%9.5f\n",total_jitter,average_jitter);
	printf("Now jitter for compression thread\n");
	total_jitter=0.0;
   for(i=0;i<frame_count;i++)
   {
	//printf("prejitter for frame %d is %ld sec and %ld nsec\n",i+1,*(time_sc+i),*(time_nsc+i));	  
	*(jitter+i)=*(time_sc+i) +(*(time_nsc+i)/(1000000000.0));    //adding the times
	
	*(jitter+i)=*(jitter+i)-*(prejitter+i)-0.007;   //calculate the jitter
	printf("jitter for frame %d is %10.5f\n",i+1,*(jitter+i));
	total_jitter+=*(jitter+i);    //sum of jitters
   }
	average_jitter=total_jitter/(frame_count);  //find average
	printf("Total latency= %9.5f\naverage jitter =%9.5f\n",total_jitter,average_jitter);
/*if(prejitter!=NULL)
	free(prejitter);


	if(jitter!=NULL)
	free(jitter);
    */
}


char pgm_header[]="P5\n#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n255\n";
char pgm_dumpname[]="test00000000.pgm";

static void dump_pgm(const void *p, int size, unsigned int tag, struct timespec *time)
{
    int written, i, total, dumpfd;
   
    snprintf(&pgm_dumpname[4], 9, "%08d", tag);
    strncat(&pgm_dumpname[12], ".pgm", 5);
    dumpfd = open(pgm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&pgm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&pgm_header[14], " sec ", 5);
    snprintf(&pgm_header[19], 11, "%010d", (int)((time->tv_nsec)/1000000));
    strncat(&pgm_header[29], " msec \n"HRES_STR" "VRES_STR"\n255\n", 19);
    written=write(dumpfd, pgm_header, sizeof(pgm_header));

    total=0;

    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    printf("wrote %d bytes\n", total);

    close(dumpfd);
    
}


void yuv2rgb_float(float y, float u, float v, 
                   unsigned char *r, unsigned char *g, unsigned char *b)
{
    float r_temp, g_temp, b_temp;

    // R = 1.164(Y-16) + 1.1596(V-128)
    r_temp = 1.164*(y-16.0) + 1.1596*(v-128.0);  
    *r = r_temp > 255.0 ? 255 : (r_temp < 0.0 ? 0 : (unsigned char)r_temp);

    // G = 1.164(Y-16) - 0.813*(V-128) - 0.391*(U-128)
    g_temp = 1.164*(y-16.0) - 0.813*(v-128.0) - 0.391*(u-128.0);
    *g = g_temp > 255.0 ? 255 : (g_temp < 0.0 ? 0 : (unsigned char)g_temp);

    // B = 1.164*(Y-16) + 2.018*(U-128)
    b_temp = 1.164*(y-16.0) + 2.018*(u-128.0);
    *b = b_temp > 255.0 ? 255 : (b_temp < 0.0 ? 0 : (unsigned char)b_temp);
}


// This is probably the most acceptable conversion from camera YUYV to RGB
//
// Wikipedia has a good discussion on the details of various conversions and cites good references:
// http://en.wikipedia.org/wiki/YUV
//
// Also http://www.fourcc.org/yuv.php
//
// What's not clear without knowing more about the camera in question is how often U & V are sampled compared
// to Y.
//
// E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each pixel
//      YUV422, which we assume here, where there are 2 bytes for each pixel, with two Y samples for one U & V,
//              or as the name implies, 4Y and 2 UV pairs
//      YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for each pixel or 36 bytes for 24 pixels

void yuv2rgb(int y, int u, int v, unsigned char *r, unsigned char *g, unsigned char *b)
{
   int r1, g1, b1;

   // replaces floating point coefficients
   int c = y-16, d = u - 128, e = v - 128;       

   // Conversion that avoids floating point
   r1 = (298 * c           + 409 * e + 128) >> 8;
   g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
   b1 = (298 * c + 516 * d           + 128) >> 8;

   // Computed values may need clipping.
   if (r1 > 255) r1 = 255;
   if (g1 > 255) g1 = 255;
   if (b1 > 255) b1 = 255;

   if (r1 < 0) r1 = 0;
   if (g1 < 0) g1 = 0;
   if (b1 < 0) b1 = 0;

   *r = r1 ;
   *g = g1 ;
   *b = b1 ;
}



unsigned int framecnt=0;
unsigned char bigbuffer[(1280*960)];

static void process_image(const void *p, int size)
{
    int i, newi, newsize=0;
    struct timespec frame_time;
    int y_temp, y2_temp, u_temp, v_temp;
    unsigned char *pptr = (unsigned char *)p;

    // record when process was called
    clock_gettime(CLOCK_REALTIME, &frame_time);    

    framecnt++;
    printf("frame %d: ", framecnt);

    // This just dumps the frame to a file now, but you could replace with whatever image
    // processing you wish.
    //

    if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_GREY)
    {
        printf("Dump graymap as-is size %d\n", size);
        dump_pgm(p, size, framecnt, &frame_time);
    }

    else if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
    {

#if defined(COLOR_CONVERT)
        printf("Dump YUYV converted to RGB size %d\n", size);
       
        // Pixels are YU and YV alternating, so YUYV which is 4 bytes
        // We want RGB, so RGBRGB which is 6 bytes
        //
        for(i=0, newi=0; i<size; i=i+4, newi=newi+6)
        {
            y_temp=(int)pptr[i]; u_temp=(int)pptr[i+1]; y2_temp=(int)pptr[i+2]; v_temp=(int)pptr[i+3];
            yuv2rgb(y_temp, u_temp, v_temp, &bigbuffer[newi], &bigbuffer[newi+1], &bigbuffer[newi+2]);
            yuv2rgb(y2_temp, u_temp, v_temp, &bigbuffer[newi+3], &bigbuffer[newi+4], &bigbuffer[newi+5]);
        }

        dump_ppm(bigbuffer, ((size*6)/4), framecnt, &frame_time);
#else
        printf("Dump YUYV converted to YY size %d\n", size);
       
        // Pixels are YU and YV alternating, so YUYV which is 4 bytes
        // We want Y, so YY which is 2 bytes
        //
        for(i=0, newi=0; i<size; i=i+4, newi=newi+2)
        {
            // Y1=first byte and Y2=third byte
            bigbuffer[newi]=pptr[i];
            bigbuffer[newi+1]=pptr[i+2];
        }

        dump_pgm(bigbuffer, (size/2), framecnt, &frame_time);
#endif

    }

    else if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24)
    {
        printf("Dump RGB as-is size %d\n", size);
        dump_ppm(p, size, framecnt, &frame_time);
    }
    else
    {
        printf("ERROR - unknown dump format\n");
    }

    fflush(stderr);
    //fprintf(stderr, ".");
    fflush(stdout);
}


static int read_frame(void)
{
    struct v4l2_buffer buf;
    unsigned int i;

    switch (io)
    {

        case IO_METHOD_READ:
            if (-1 == read(fd, buffers[0].start, buffers[0].length))
            {
                switch (errno)
                {

                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("read");
                }
            }

            process_image(buffers[0].start, buffers[0].length);
            break;

        case IO_METHOD_MMAP:
            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
            {
                switch (errno)
                {
                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, but drivers should only set for serious errors, although some set for
                           non-fatal errors too.
                         */
                        return 0;


                    default:
                        printf("mmap failure\n");
                        errno_exit("VIDIOC_DQBUF");
                }
            }

            assert(buf.index < n_buffers);

            process_image(buffers[buf.index].start, buf.bytesused);

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                    errno_exit("VIDIOC_QBUF");
            break;

        case IO_METHOD_USERPTR:
            CLEAR(buf);

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;

            if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
            {
                switch (errno)
                {
                    case EAGAIN:
                        return 0;

                    case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                    default:
                        errno_exit("VIDIOC_DQBUF");
                }
            }

            for (i = 0; i < n_buffers; ++i)
                    if (buf.m.userptr == (unsigned long)buffers[i].start
                        && buf.length == buffers[i].length)
                            break;

            assert(i < n_buffers);

            process_image((void *)buf.m.userptr, buf.bytesused);

            if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                    errno_exit("VIDIOC_QBUF");
            break;
    }

    //printf("R");
    return 1;
}


static void mainloop(void)
{
    unsigned int count;
    struct timespec read_delay;
    struct timespec time_error;
    
    read_delay.tv_sec=0;
    read_delay.tv_nsec=30000;

    count = frame_count;
     int i=0;
    usleep(1000000);
	/*loop for the number of frames*/
    while (count > 0)
    {
	
        for (;;)
        {
	sem_wait(&sem_capture);
 	clock_gettime(CLOCK_REALTIME, &tstamp);
	printf("Timestamp 1: %ld sec %ld nsec\n",tstamp.tv_sec,tstamp.tv_nsec);
	*(time_s+i)=tstamp.tv_sec;
	*(time_ns+i)=tstamp.tv_nsec;
	//printf("Timestamp 5: %ld sec %ld nsec\n",*(time_s+i),*(time_ns+i));
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r)
            {
                if (EINTR == errno)
                    continue;
                errno_exit("select");
            }

            if (0 == r)
            {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if (read_frame())
            {
                if(nanosleep(&read_delay, &time_error) != 0)
                    perror("nanosleep");
                else
                    //printf("time_error.tv_sec=%ld, time_error.tv_nsec=%ld\n", time_error.tv_sec, time_error.tv_nsec);

                count--;
		i++;
                break;
            }

            /* EAGAIN - continue select loop unless count done. */
            if(count <= 0) break;
        }

        if(count <= 0) break;
    }
	/*if(frameno=NULL)
	free(frameno);
*/
}

static void stop_capturing(void)
{
        enum v4l2_buf_type type;

        switch (io) {
        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
                        errno_exit("VIDIOC_STREAMOFF");
                break;
        }
}

static void start_capturing(void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        switch (io) 
        {

        case IO_METHOD_READ:
                /* Nothing to do. */
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i) 
                {
                        printf("allocated buffer %d\n", i);
                        struct v4l2_buffer buf;
			//usleep(1000000);

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_MMAP;
                        buf.index = i;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i) {
                        struct v4l2_buffer buf;

                        CLEAR(buf);
                        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                        buf.memory = V4L2_MEMORY_USERPTR;
                        buf.index = i;
                        buf.m.userptr = (unsigned long)buffers[i].start;
                        buf.length = buffers[i].length;

                        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
                                errno_exit("VIDIOC_QBUF");
                }
                type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
                        errno_exit("VIDIOC_STREAMON");
                break;
        }
}

static void uninit_device(void)
{
        unsigned int i;

        switch (io) {
        case IO_METHOD_READ:
                free(buffers[0].start);
                break;

        case IO_METHOD_MMAP:
                for (i = 0; i < n_buffers; ++i)
                        if (-1 == munmap(buffers[i].start, buffers[i].length))
                                errno_exit("munmap");
                break;

        case IO_METHOD_USERPTR:
                for (i = 0; i < n_buffers; ++i)
                        free(buffers[i].start);
                break;
        }

        free(buffers);
}

static void init_read(unsigned int buffer_size)
{
        buffers = (buffer *)calloc(1, sizeof(*buffers));

        if (!buffers) 
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        buffers[0].length = buffer_size;
        buffers[0].start = malloc(buffer_size);

        if (!buffers[0].start) 
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }
}

static void init_mmap(void)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = 6;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) 
        {
                if (EINVAL == errno) 
                {
                        fprintf(stderr, "%s does not support "
                                 "memory mapping\n", dev_name);
                        exit(EXIT_FAILURE);
                } else 
                {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        if (req.count < 2) 
        {
                fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
                exit(EXIT_FAILURE);
        }

        buffers = (buffer *)calloc(req.count, sizeof(*buffers));

        if (!buffers) 
        {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;

                CLEAR(buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.index       = n_buffers;

                if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit("VIDIOC_QUERYBUF");

                buffers[n_buffers].length = buf.length;
                buffers[n_buffers].start =
                        mmap(NULL, // start anywhere 
                              buf.length,
                              PROT_READ | PROT_WRITE, // required,
                              MAP_SHARED, // recommended
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit("mmap");
        }
}

static void init_userp(unsigned int buffer_size)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "%s does not support "
                                 "user pointer i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        buffers = (buffer *)calloc(4, sizeof(*buffers));

        if (!buffers) {
                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);
        }

        for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
                buffers[n_buffers].length = buffer_size;
                buffers[n_buffers].start = malloc(buffer_size);

                if (!buffers[n_buffers].start) {
                        fprintf(stderr, "Out of memory\n");
                        exit(EXIT_FAILURE);
                }
        }
}

static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    unsigned int min;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap))
    {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n",
                     dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
                errno_exit("VIDIOC_QUERYCAP");
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s is no video capture device\n",
                 dev_name);
        exit(EXIT_FAILURE);
    }

    switch (io)
    {
        case IO_METHOD_READ:
            if (!(cap.capabilities & V4L2_CAP_READWRITE))
            {
                fprintf(stderr, "%s does not support read i/o\n",
                         dev_name);
                exit(EXIT_FAILURE);
            }
            break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            if (!(cap.capabilities & V4L2_CAP_STREAMING))
            {
                fprintf(stderr, "%s does not support streaming i/o\n",
                         dev_name);
                exit(EXIT_FAILURE);
            }
            break;
    }


    /* Select video input, video standard and tune here. */


    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap))
    {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop))
        {
            switch (errno)
            {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                        break;
            }
        }

    }
    else
    {
        /* Errors ignored. */
    }


    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (force_format)
    {
        printf("FORCING FORMAT\n");
        fmt.fmt.pix.width       = HRES;
        fmt.fmt.pix.height      = VRES;

        // Specify the Pixel Coding Formate here

        // This one work for Logitech C200
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_VYUY;

        // Would be nice if camera supported
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

        //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
                errno_exit("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */
    }
    else
    {
        printf("ASSUMING FORMAT\n");
        /* Preserve original settings as set by v4l2-ctl for example */
        if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
                    errno_exit("VIDIOC_G_FMT");
    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
            fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
            fmt.fmt.pix.sizeimage = min;

    switch (io)
    {
        case IO_METHOD_READ:
            init_read(fmt.fmt.pix.sizeimage);
            break;

        case IO_METHOD_MMAP:
            init_mmap();
            break;

        case IO_METHOD_USERPTR:
            init_userp(fmt.fmt.pix.sizeimage);
            break;
    }
}


static void close_device(void)
{
        if (-1 == close(fd))
                errno_exit("close");

        fd = -1;
}

static void open_device(void)
{
        struct stat st;

        if (-1 == stat(dev_name, &st)) {
                fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", dev_name);
                exit(EXIT_FAILURE);
        }

        fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd) {
                fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
}
void printscheduler(void)
  {
  int schedType;

  schedType = sched_getscheduler(getpid());

  switch(schedType)
    {
    case SCHED_FIFO:
      printf("Pthread Policy is SCHED_FIFO\n");
      break;
    case SCHED_OTHER:
      printf("Pthread Policy is SCHED_OTHER\n");
      break;
    case SCHED_RR:
      printf("Pthread Policy is SCHED_OTHER\n");
      break;
    default:
      printf("Pthread Policy is UNKNOWN\n");
    }
  }
static void usage(FILE *fp, int argc, char **argv)
{
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Version 1.3\n"
                 "Options:\n"
                 "-d | --device name   Video device name [%s]\n"
                 "-h | --help          Print this message\n"
                 "-m | --mmap          Use memory mapped buffers [default]\n"
                 "-r | --read          Use read() calls\n"
                 "-u | --userp         Use application allocated buffers\n"
                 "-o | --output        Outputs stream to stdout\n"
                 "-f | --format        Force format to 640x480 GREY\n"
                 "-c | --count         Number of frames to grab [%i]\n"
                 "",
                 argv[0], dev_name, frame_count);
}

static const char short_options[] = "d:hmruofc:";

static const struct option
long_options[] = {
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { "mmap",   no_argument,       NULL, 'm' },
        { "read",   no_argument,       NULL, 'r' },
        { "userp",  no_argument,       NULL, 'u' },
        { "output", no_argument,       NULL, 'o' },
        { "format", no_argument,       NULL, 'f' },
        { "count",  required_argument, NULL, 'c' },
        { 0, 0, 0, 0 }
};



void *framecapture(void *threadp)
{
    /*main thread*/
    open_device();
    init_device();
    start_capturing();
    mainloop();
    stop_capturing();
    uninit_device();			
    close_device();  			
   
}




int main(int argc, char **argv)
{
    if(argc > 1)
        dev_name = argv[1];
    else
        dev_name = "/dev/video0";
    
	int rt_max_prio,rt_min_prio,rc,i=0;
	/*initaize pointers for timestamps and clear the memory*/
    frameno=(unsigned int *)malloc(1*sizeof(unsigned int));
    time_s=(long int*)malloc(frame_count*sizeof(long int));
    time_ns=(long int*)malloc(frame_count*sizeof(long int));
    memset(frameno,0,frame_count);
    memset(time_s,0,frame_count);
    memset(time_ns,0,frame_count);
    time_sc=comp_start_s;
    time_nsc=comp_start_ns;
    time_scd=comp_end_s;
    time_nscd=comp_end_ns;
    for (;;)
    {
        int idx;
        int c;

        c = getopt_long(argc, argv,
                    short_options, long_options, &idx);

        if (-1 == c)
            break;

        switch (c)
        {
            case 0: /* getopt_long() flag */
                break;

            case 'd':
                dev_name = optarg;
                break;

            case 'h':
                usage(stdout, argc, argv);
                exit(EXIT_SUCCESS);

            case 'm':
                io = IO_METHOD_MMAP;
                break;

            case 'r':
                io = IO_METHOD_READ;
                break;

            case 'u':
                io = IO_METHOD_USERPTR;
                break;

            case 'o':
                out_buf++;
                break;

            case 'f':
                force_format++;
                break;

            case 'c':
                errno = 0;
                frame_count = strtol(optarg, NULL, 0);
                if (errno)
                        errno_exit(optarg);
                break;

            default:
                usage(stderr, argc, argv);
                exit(EXIT_FAILURE);
        }
    }
    rt_max_prio = sched_get_priority_max(SCHED_FIFO);
    rt_min_prio = sched_get_priority_min(SCHED_FIFO);
    printf("max=%d,min=%d",rt_max_prio,rt_min_prio);
    printscheduler();
    for(i=0;i<NUM_THREADS-1;i++)
    {	
    	pthread_attr_init(&rt_sched_attr[i]);
    	pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);  //set default schedule
    	pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);			// Changing scheduling to fifo
 	rt_param[i].sched_priority=rt_max_prio+i-1;
	rc=sched_setscheduler(getpid(), SCHED_FIFO, &rt_param[i]);


   if (rc)
   {
       printf("ERROR; sched_setscheduler rc is %d\n", rc);
       perror("sched_setschduler"); exit(-1);
   }
	
    }
    printscheduler();						//print the schedule policy
	/* initialization of semaphores*/
  	sem_init(&sem_capture,0,1);
	sem_init(&sem_compress,0,0);
	sem_init(&sem_jitter,0,0);
	
	/*create threads*/
   pthread_create(&threads[frame_capture],&rt_sched_attr[frame_capture],framecapture,NULL);
   pthread_create(&threads[compression],&rt_sched_attr[compression],compresstojpeg,NULL);
   pthread_create(&threads[analysis],NULL,jitteranalysis,NULL);
  
  /*wait for the threads to get destroyed*/
    for(i=0;i<NUM_THREADS;i++)
    pthread_join(threads[i],NULL);




   

    fprintf(stderr, "\n");
    return 0;
}