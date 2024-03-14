# Visual Synchrome
This project is a system that syncs with an external analog clock. The system acquires time-lapse images using a VGA resolution camera at 1Hz and partially at 10Hz and further verification is done. The system is implemented in real-time using services via Rate Monotonic Theory. The system captures real-time images, filters them, transforms them, and saves them in PPM format. All the services run on the same core (Core 0) .

## Keywords for Technologies/hardware used:
Linux, Pthreads, Scheduler, Synchronization, Raspberry Pi, Jitter Analysis, Sequencer, Rate Monotonic Theory, RTOS, VGA resolution camera, 

## Real-Time requirements
1. Scheduler
This task runs on CPU-0 and is responsible for releasing semaphores and triggering other services. It ran at 33.33ms, ie, 30hz.
2. Read Frame
This task runs on CPU-0 and is responsible for acquires camera images at fixed intervals. It runs at 1 Hz,
a. C = 0.213 us
b. T = D = 1ms
The WCET has been calculated by comparing all the recorded execution times and finding the largest number. These frames are captured and stored in a buffer.
3. Transforming and processing the frames.
This task runs on CPU-0. I am using the brightening transformation on the images. The image is read from the buffer at a fixed interval. It is then transformed into a new image and saved in a same buffer
a. It runs at 3 Hz,
b. C = 4.683us
c. T = D = 0.333ms.
4. Dump PGM
This task runs on CPU-0 and stores the images from the buffer in ppm format along with the header timestamps/frame numbers properly. It saves the frames in a folder called ‘frames’.
a. It runs at 1 Hz,
b. C = 4.9us
c. T = D =1ms
