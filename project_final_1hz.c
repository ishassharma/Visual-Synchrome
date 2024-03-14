#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <linux/videodev2.h>
#include <getopt.h>             /* getopt_long() */
#include <fcntl.h>              /* low-level i/o */
#include <ctype.h>
#include <sys/utsname.h>

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

#define COLOR_CONVERT_RGB
#define TRANSFORM_ON
#define USEC_PER_MSEC (1000)
#define NANOSEC_PER_SEC (1000000000)
#define NUM_CPU_CORES (1)
#define TRUE (1)
#define FALSE (0)
#define NUM_THREADS (4)
#define HRES 640
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define PIXIDX ((i*col*chan)+(j*chan)+k)
#define SAT (255)

static char            *dev_name;
static enum io_method   io = IO_METHOD_MMAP;
static int              fd = -1;
struct buffer          *buffers;
static unsigned int     n_buffers;
static int              out_buf;
static int              force_format=1;
static int              frame_count = (189);
int framecnt=-8;
unsigned char bigbuffer[(1280*960)];
static int size_input =0;

int abortTest=FALSE;
int abortS1=FALSE, abortS2=FALSE, abortS3=FALSE;
sem_t semS1, semS2, semS3, semS4;
struct timeval start_time_val;
static struct v4l2_format fmt;
struct timespec frame_time;
struct v4l2_buffer buf;
int count = 189;
int dump_count=0;
int dump_correct=0;

typedef struct
{
    int threadIdx;
    unsigned long long sequencePeriods;
} threadParams_t;

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
    printf("open device done\n");
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

        buffers = calloc(req.count, sizeof(*buffers));

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
                        mmap(NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fd, buf.m.offset);

                if (MAP_FAILED == buffers[n_buffers].start)
                        errno_exit("mmap");
        }
     printf("init mmap done\n");
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

    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (force_format)
    {
        printf("FORCING FORMAT\n");
        fmt.fmt.pix.width       = HRES;
        fmt.fmt.pix.height      = VRES;

        // This one works for Logitech C200
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
                errno_exit("VIDIOC_S_FMT");

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

    
    init_mmap();
    printf("init device done\n");
       
}

static void start_capturing(void)
{
        unsigned int i;
        enum v4l2_buf_type type;

        for (i = 0; i < n_buffers; ++i) 
        {
                printf("allocated buffer %d\n", i);
                struct v4l2_buffer buf;

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
    printf("start cap done\n");

}

static void close_device(void)
{
        if (-1 == close(fd))
                errno_exit("close");

        fd = -1;
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

static void uninit_device(void)
{
        unsigned int i;
      
        for (i = 0; i < n_buffers; ++i)
                if (-1 == munmap(buffers[i].start, buffers[i].length))
                        errno_exit("munmap");

        free(buffers);
}

static int read_frame(void)
{
    // struct timespec start_s1, end_s1;
    // double start,end, execution_time;
    // double wcet_read = 0.0;
    
    // clock_gettime(CLOCK_REALTIME, &start_s1);
    // start = ((double)start_s1.tv_sec + (double)((start_s1.tv_nsec)/(double)1000000000)); //time in seconds
    // syslog(LOG_CRIT," Read frame start Time: %lf seconds\n",start);

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
                return 0;

            default:
                printf("mmap failure\n");
                errno_exit("VIDIOC_DQBUF");
        }
    }

    assert(buf.index < n_buffers);

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
            errno_exit("VIDIOC_QBUF");

    return 1;   

}

static void mainloop(void)
{
    unsigned int count;
    struct timespec read_delay;
    struct timespec time_error;

    count = frame_count;

    while (count > 0)
    {
        for (;;)
        {
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

            read_frame();
            count--;
            if(count <= 0) break;
        }

        if(count <= 0) 
        {
            printf("189 done\n");
            break;
    }
    }              
}

void brighten (unsigned char *image, int size,unsigned int tag, struct timespec *time)
{
    int i, j, k;
    unsigned char *ptr = image;
    double alpha = 1.25;
    unsigned char beta = 25;
    unsigned row = 480, col = 640, chan = 3;
 #if defined(TRANSFORM_ON)
    printf("transform on \n");
    for (i = 0; i < row; i++)
    {
        for (j = 0; j < col; j++)
        {
            for (k = 0; k < chan; k++)
            {
                unsigned idx = (i * col * chan) + (j * chan) + k;
                unsigned pix = (unsigned)((ptr[idx]) * alpha) + beta;
                ptr[idx] = pix > 255 ? 255 : pix;
            }
        }

    }
    #else
    printf("transform off \n");
    #endif

}

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


static void process_image(const void *p, int size)
{
    struct timespec start_s2, end_s2;
    double start2,end2, execution_time2;
    double wcet_transform = 0.0;
    clock_gettime(CLOCK_REALTIME, &start_s2);
    start2 = ((double)start_s2.tv_sec + (double)((start_s2.tv_nsec)/(double)1000000000)); //time in seconds
    syslog(LOG_CRIT," Transform start Time: %lf seconds\n",start2);

    int i, newi, newsize=0;
    struct timespec frame_time;
    unsigned char *pptr = (unsigned char *)p;
    int y_temp, y2_temp, u_temp, v_temp;

    framecnt++;
    clock_gettime(CLOCK_REALTIME, &frame_time);  

    if(fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
    {
    #if defined(COLOR_CONVERT_RGB)
        for(i=0, newi=0; i<size; i=i+4, newi=newi+6)
        {
            y_temp=(int)pptr[i]; u_temp=(int)pptr[i+1]; y2_temp=(int)pptr[i+2]; v_temp=(int)pptr[i+3];
            yuv2rgb(y_temp, u_temp, v_temp, &bigbuffer[newi], &bigbuffer[newi+1], &bigbuffer[newi+2]);
            yuv2rgb(y2_temp, u_temp, v_temp, &bigbuffer[newi+3], &bigbuffer[newi+4], &bigbuffer[newi+5]);
        }
        if(framecnt > -1) 
        {
        size_input =(size * 6) / 4;
        brighten(bigbuffer, size_input , framecnt, &frame_time);       
        printf("Dump YUYV converted to RGB size %d\n", size);
        }
    #else
        for(i=0, newi=0; i<size; i=i+4, newi=newi+2)
        {
            // Y1=first byte and Y2=third byte
            bigbuffer[newi]=pptr[i];
            bigbuffer[newi+1]=pptr[i+2];
        }

        if(framecnt > -1)
        {
            size_input=(size/2);
            brighten(bigbuffer, size_input, framecnt, &frame_time);
            printf("Dump YUYV converted to YY size %d\n", size);
        }
    #endif
    }
    
    fflush(stderr);
    fflush(stdout);

    clock_gettime(CLOCK_REALTIME, &end_s2);
    end2 = ((double)end_s2.tv_sec + (double)((end_s2.tv_nsec)/(double)1000000000)); //time in seconds
    syslog(LOG_CRIT,"transform end time: %lf seconds\n",end2);

    execution_time2 =  end2-start2;
    syslog(LOG_CRIT," transform execution time: %lf seconds\n",execution_time2);

    if(execution_time2 > wcet_transform)
    {
        wcet_transform = execution_time2;
        syslog(LOG_CRIT," transform wcet time: %lf seconds\n",wcet_transform);
    }
}


// char ppm_header[]="P6\n#9999999999 sec 9999999999 msec #isha-remote desktop 10.0.0.16\n"HRES_STR" "VRES_STR"\n255\n";
// char ppm_dumpname[]="frames/test0000.ppm";

//char ppm_header[]="P6\n#9999999999 sec 9999999999 msec isha-desktop 10.0.0.16\n"HRES_STR" "VRES_STR"\n255\n";


static void dump_ppm(const void *p, int size, unsigned int tag, struct timespec *time)
{
    double wcet_dump = 0.0;
    struct timespec start_s3, end_s3;
    double start3,end3, execution_time3;
    clock_gettime(CLOCK_REALTIME, &start_s3);
    start3 = ((double)start_s3.tv_sec + (double)((start_s3.tv_nsec)/(double)1000000000)); 
    syslog(LOG_CRIT," writeback start Time: %lf seconds\n",start3);

    int written, i, total, dumpfd;
    char ppm_dumpname[30]="frames/test0000.ppm";

    snprintf(ppm_dumpname, sizeof(ppm_dumpname), "frames/test%04d.ppm", dump_count);

    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    char ppm_header[] = "P6\n#9999999999 sec 9999999999 msec \nisha-desktop 10.0.0.16\nHRES_STR VRES_STR\n255\n";
    
    snprintf(&ppm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&ppm_header[14], " sec ", 5);
    snprintf(&ppm_header[19], 11, "%010d", (int)((time->tv_nsec) / 1000000));
    strncat(&ppm_header[29], " msec \n", 8);
    strncat(ppm_header, HRES_STR " " VRES_STR "\n255\n", sizeof(ppm_header) - strlen(ppm_header) - 1);			

	strncat(&ppm_header[35],"isha-remote desktop 10.0.0.16\n H640 V480 \n255\n", 62);
   
    written=write(dumpfd, ppm_header, strlen(ppm_header));

    total=0;

    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    printf("wrote %d bytes\n", total);

    close(dumpfd);

    clock_gettime(CLOCK_REALTIME, &end_s3);
    end3 = ((double)end_s3.tv_sec + (double)((end_s3.tv_nsec)/(double)1000000000)); //time in seconds
    syslog(LOG_CRIT," writeback end time: %lf seconds\n",end3);

    execution_time3 =  end3-start3;
    syslog(LOG_CRIT," writeback execution time: %lf seconds\n",execution_time3);  
    
    if(execution_time3 > wcet_dump)
    {
        wcet_dump = execution_time3;
        syslog(LOG_CRIT," writeback wcet time: %lf seconds\n",wcet_dump);
    }
    
}

void *Sequencer(void *threadp);
void *Service_1_read(void *threadp);
void *Service_2_transform(void *threadp);
void *Service_3_writeback(void *threadp);
void print_scheduler(void);

void main(int argc, char **argv)
{

    if(argc > 1)
        dev_name = argv[1];
    else
        dev_name = "/dev/video0";

    struct timeval current_time_val;
    int i, rc, scope;
    cpu_set_t threadcpu;
    pthread_t threads[NUM_THREADS];
    threadParams_t threadParams[NUM_THREADS];
    pthread_attr_t rt_sched_attr[NUM_THREADS];
    int rt_max_prio, rt_min_prio;
    struct sched_param rt_param[NUM_THREADS];
    struct sched_param main_param;
    pthread_attr_t main_attr;
    pid_t mainpid;
    cpu_set_t allcpuset;

    open_device();
    init_device();
    start_capturing();

    printf("Starting Sequencer Demo\n");
    gettimeofday(&start_time_val, (struct timezone *)0);
    gettimeofday(&current_time_val, (struct timezone *)0);
    syslog(LOG_CRIT, "Sequencer @ sec=%d, msec=%d\n", (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);

    printf("System has %d processors configured and %d available.\n", get_nprocs_conf(), get_nprocs());

    CPU_ZERO(&allcpuset);

    for(i=0; i < NUM_CPU_CORES; i++)
       CPU_SET(i, &allcpuset);

     printf("Using CPUS=%d from total available.\n", CPU_COUNT(&allcpuset));
    //CAMERA INITIALISATION
    // initialize the  semaphores

    if (sem_init (&semS1, 0, 0)) { printf ("Failed to initialize S1 semaphore\n"); exit (-1); }
    if (sem_init (&semS2, 0, 0)) { printf ("Failed to initialize S2 semaphore\n"); exit (-1); }
    if (sem_init (&semS3, 0, 0)) { printf ("Failed to initialize S3 semaphore\n"); exit (-1); }
    if (sem_init (&semS4, 0, 0)) { printf ("Failed to initialize S4 semaphore\n"); exit (-1); }

    mainpid=getpid();
    rt_max_prio = sched_get_priority_max(SCHED_FIFO);
    rt_min_prio = sched_get_priority_min(SCHED_FIFO);

    rc=sched_getparam(mainpid, &main_param);
    main_param.sched_priority=rt_max_prio;
    rc=sched_setscheduler(getpid(), SCHED_FIFO, &main_param);
    if(rc < 0) perror("main_param");
    print_scheduler();

    pthread_attr_getscope(&main_attr, &scope);

    if(scope == PTHREAD_SCOPE_SYSTEM)
      printf("PTHREAD SCOPE SYSTEM\n");
    else if (scope == PTHREAD_SCOPE_PROCESS)
      printf("PTHREAD SCOPE PROCESS\n");
    else
      printf("PTHREAD SCOPE UNKNOWN\n");

    printf("rt_max_prio=%d\n", rt_max_prio);
    printf("rt_min_prio=%d\n", rt_min_prio);

    for(i=0; i < NUM_THREADS; i++)
    {

      CPU_ZERO(&threadcpu);
      CPU_SET(3, &threadcpu);

      rc=pthread_attr_init(&rt_sched_attr[i]);
      rc=pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);
      rc=pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);
      //rc=pthread_attr_setaffinity_np(&rt_sched_attr[i], sizeof(cpu_set_t), &threadcpu);

      rt_param[i].sched_priority=rt_max_prio-i;
      pthread_attr_setschedparam(&rt_sched_attr[i], &rt_param[i]);

      threadParams[i].threadIdx=i;
    }

    printf("Service threads will run on %d CPU cores\n", CPU_COUNT(&threadcpu));

    // Create Service threads which will block awaiting release for:

    // Servcie_1 = RT_MAX-1	@ 30 Hz

    rt_param[1].sched_priority=rt_max_prio-1;
    pthread_attr_setschedparam(&rt_sched_attr[1], &rt_param[1]);
    rc=pthread_create(&threads[1],               // pointer to thread descriptor
                      &rt_sched_attr[1],         // use specific attributes
                      //(void *)0,               // default attributes
                      Service_1_read,                 // thread function entry point
                      (void *)&(threadParams[1]) // parameters to pass in
                     );
    if(rc < 0)
        perror("pthread_create for service 1");
    else
        printf("pthread_create successful for service 1\n");


    // Service_2 = RT_MAX-2	@ 10 Hz

    rt_param[2].sched_priority=rt_max_prio-2;
    pthread_attr_setschedparam(&rt_sched_attr[2], &rt_param[2]);
    rc=pthread_create(&threads[2], &rt_sched_attr[2], Service_2_transform, (void *)&(threadParams[2]));
    if(rc < 0)
        perror("pthread_create for service 2");
    else
        printf("pthread_create successful for service 2\n");


    // Service_3 = RT_MAX-3	@ 10 Hz

    rt_param[3].sched_priority=rt_max_prio-3;
    pthread_attr_setschedparam(&rt_sched_attr[3], &rt_param[3]);
    rc=pthread_create(&threads[3], &rt_sched_attr[3], Service_3_writeback, (void *)&(threadParams[3]));
    if(rc < 0)
        perror("pthread_create for service 3");
    else
        printf("pthread_create successful for service 3\n");

    // Create Sequencer thread, which like a cyclic executive, is highest prio
    printf("Start sequencer\n");
    threadParams[0].sequencePeriods=5600;

    // Sequencer = RT_MAX	@ 30 Hz
    //
    rt_param[0].sched_priority=rt_max_prio;
    pthread_attr_setschedparam(&rt_sched_attr[0], &rt_param[0]);
    rc=pthread_create(&threads[0], &rt_sched_attr[0], Sequencer, (void *)&(threadParams[0]));
    if(rc < 0)
        perror("pthread_create for sequencer service 0");
    else
        printf("pthread_create successful for sequeencer service 0\n");

    
    for(i=0;i<NUM_THREADS;i++)
    pthread_join(threads[i], NULL);
    

    stop_capturing();
    uninit_device();
    close_device();
    
    printf("\nTEST COMPLETE\n");

}

void *Sequencer(void *threadp)
{
    struct timeval current_time_val;
    struct timespec delay_time = {0,33333333}; // delay for 33.33 msec, 30 Hz
    struct timespec remaining_time;
    double current_time;
    double residual;
    int rc, delay_cnt=0;
    unsigned long long seqCnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    int trigger_flag=0;
    char user_key; 

    gettimeofday(&current_time_val, (struct timezone *)0);
    syslog(LOG_CRIT, "Sequencer thread @ sec=%d, msec=%d\n", (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);
    printf("Sequencer thread @ sec=%d, msec=%d\n", (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);

    do
    {
        delay_cnt=0; residual=0.0;

        //gettimeofday(&current_time_val, (struct timezone *)0);
        //syslog(LOG_CRIT, "Sequencer thread prior to delay @ sec=%d, msec=%d\n", (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);
        do
        {
            rc=nanosleep(&delay_time, &remaining_time);

            if(rc == EINTR)
            { 
                residual = remaining_time.tv_sec + ((double)remaining_time.tv_nsec / (double)NANOSEC_PER_SEC);

                if(residual > 0.0) printf("residual=%lf, sec=%d, nsec=%d\n", residual, (int)remaining_time.tv_sec, (int)remaining_time.tv_nsec);
 
                delay_cnt++;
            }
            else if(rc < 0)
            {
                perror("Sequencer nanosleep");
                exit(-1);
            }
           
        } while((residual > 0.0) && (delay_cnt < 100));

        while(trigger_flag !=1)
        {   
            printf ("Press Enter to trigger sequencer\n");

            user_key = getchar();

            if(user_key == '\n')
            {
                printf("Sequencer triggered!\n");
                trigger_flag = 1;
                break;
            }
        }

        seqCnt++;
        // gettimeofday(&current_time_val, (struct timezone *)0);
        // syslog(LOG_CRIT, "Sequencer cycle %llu @ sec=%d, msec=%d\n", seqCnt, (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);

        if(delay_cnt > 1) printf("Sequencer looping delay %d\n", delay_cnt);


        // Release each service at a sub-rate of the generic sequencer rate

        // Servcie_1 = RT_MAX-1	@ 30 Hz
        if((seqCnt % 30) == 0) sem_post(&semS1);

        // Service_2 = RT_MAX-2	@ 10 Hz
        if((seqCnt % 10) == 0) sem_post(&semS2);

        // Service_3 = RT_MAX-3	@ 10 Hz
        if((seqCnt % 30) == 0) sem_post(&semS3);


        //gettimeofday(&current_time_val, (struct timezone *)0);
        //syslog(LOG_CRIT, "Sequencer release all sub-services @ sec=%d, msec=%d\n", (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);

    } while(!abortTest && (seqCnt < threadParams->sequencePeriods));

    sem_post(&semS1); sem_post(&semS2); sem_post(&semS3);
    
    abortS1=TRUE; abortS2=TRUE; abortS3=TRUE;

    pthread_exit((void *)0);
}

void * Service_1_read(void *threadp)
{   
    double wcet_read = 1.0;
    struct timeval current_time_val;
    struct timespec start_s1, end_s1;
    double start,end, execution_time;
    unsigned long long S1Cnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    clock_gettime(CLOCK_REALTIME, &start_s1);
    start = ((double)start_s1.tv_sec + (double)((start_s1.tv_nsec)/(double)1000000000)); //time in seconds
    syslog(LOG_CRIT," Read frame start Time: %lf seconds\n",start);

    while(!abortS1)
    {
        sem_wait(&semS1);
        S1Cnt++;
        mainloop();     
        //read_frame();
        //gettimeofday(&current_time_val, (struct timezone *)0);
        //syslog(LOG_CRIT, "Read frame release %llu @ sec=%d, msec=%d\n", S1Cnt, (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);
    }

    clock_gettime(CLOCK_REALTIME, &end_s1);
    end = ((double)end_s1.tv_sec + (double)((end_s1.tv_nsec)/(double)1000000000)); //time in seconds
    syslog(LOG_CRIT," read frame end time: %lf seconds\n",end);

    execution_time =  end-start;
    syslog(LOG_CRIT," read frame execution time: %lf seconds\n",execution_time);

    if(execution_time < wcet_read)
    {
        wcet_read = execution_time;
        syslog(LOG_CRIT," read frame wcet time: %lf seconds\n",wcet_read);
    }
    pthread_exit((void *)0);
}

void *Service_2_transform(void *threadp)
{

    struct timeval current_time_val;
    double current_time;
    unsigned long long S2Cnt=0;

    threadParams_t *threadParams = (threadParams_t *)threadp;


    while(!abortS2)
    {
        sem_wait(&semS2);
        S2Cnt++;


        process_image(buffers[0].start, buffers[0].length);
        
        //gettimeofday(&current_time_val, (struct timezone *)0);
        //syslog(LOG_CRIT, "Transform release %llu @ sec=%d, msec=%d\n", S2Cnt, (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);
    }


    pthread_exit((void *)0);
}

void *Service_3_writeback(void *threadp)
{
  
	struct timeval current_time_val;
    double current_time;

    unsigned long long S3Cnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

   

    while(!abortS3)
    {
        sem_wait(&semS3);
        S3Cnt++;
        clock_gettime(CLOCK_REALTIME, &frame_time);    
        dump_correct++;

        if ((dump_count<=180)&&(dump_correct>2))
        {
        dump_ppm (bigbuffer,size_input,framecnt,&frame_time);
        //dump_ppm(cir_captured_frame_buffer + write_position - captured_frame_size_cap, captured_frame_size_cap, framecnt, &frame_time);
        dump_count++;
        }
        //gettimeofday(&current_time_val, (struct timezone *)0);
        //syslog(LOG_CRIT, "writeback release %llu @ sec=%d, msec=%d\n", S3Cnt, (int)(current_time_val.tv_sec-start_time_val.tv_sec), (int)current_time_val.tv_usec/USEC_PER_MSEC);
    }
    
    

    pthread_exit((void *)0);
}



void print_scheduler(void)
{
   int schedType;

   schedType = sched_getscheduler(getpid());

   switch(schedType)
   {
       case SCHED_FIFO:
           printf("Pthread Policy is SCHED_FIFO\n");
           break;
       case SCHED_OTHER:
           printf("Pthread Policy is SCHED_OTHER\n"); exit(-1);
         break;
       case SCHED_RR:
           printf("Pthread Policy is SCHED_RR\n"); exit(-1);
           break;
       default:
           printf("Pthread Policy is UNKNOWN\n"); exit(-1);
   }
}