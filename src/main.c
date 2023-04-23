/*

Creates three processes, which take video feed from /dev/video0, transform it from RGB888 to RGB565 and feed it to screen buffer /dev/fb0.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

// For determining display size
#include <string.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>

// Shared memory
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define SEM1_READ 0
#define SEM1_WRITE 1
#define SEM2_READ 2
#define SEM2_WRITE 3

pid_t pid1, pid2;

int semID;
int shm1ID;
int shm2ID;

// Function definitions
int grab();
int transform();
int display();
void getDisplayDimensions(int *p_display_width, int *p_display_height);
void semaphoreLock(int semId, unsigned short semNum);
void semaphoreUnlock(int semId, unsigned short semNum);

int main(int argc, char *argv[])
{
    int display_width, display_height;

    // Initialize semaphores for write first
    semID = semget(IPC_PRIVATE, 2, 0644);
    if (semID == -1)
    {
        perror("semget error\n");
        exit(1);
    }
    unsigned short semArray[4];
    semArray[SEM1_READ] = 0;
    semArray[SEM1_WRITE] = 1;
    semArray[SEM2_READ] = 0;
    semArray[SEM2_WRITE] = 1;
    if (semctl(semID, 0, SETALL, semArray) == -1)
    {
        printf("%s: semaphore initialization error\n", argv[0]);
        exit(1);
    }

    // Initialize shared memory
    getDisplayDimensions(&display_width, &display_height);
    shm1ID = shmget(IPC_PRIVATE, FRAME_HEIGHT * FRAME_WIDTH * 3, 0600);
    shm2ID = shmget(IPC_PRIVATE, display_width * display_height * 2, 0600);
    if (shm1ID == -1 || shm2ID == -1)
    {
        printf("shmget error\n");
    }

    pid1 = fork();

    if (pid1 == 0)
    {
        grab();
    }
    else
    {
        pid2 = fork();
        if (pid2 == 0)
        {
            transform();
        }
        else
        {
            display();
        }
    }
    exit(0);
}

// Grab //////////////////////////////////////////////////////
int grab()
{
    int file_src;
    char *shm1;
    char *buff;

    int frame_width, frame_height;
    ssize_t frame_size;

    file_src = open("/dev/video0", O_RDONLY);
    if (file_src == -1)
    {
        printf("Invalid source file\n");
        exit(2);
    }

    // Attach shared memory
    shm1 = (char *)shmat(shm1ID, NULL, 0);
    if (shm1 == (char *)-1)
    {
        printf("shmat error\n");
        exit(2);
    }

    frame_width = FRAME_WIDTH;
    frame_height = FRAME_HEIGHT;
    frame_size = frame_width * frame_height * 3; // ker je 24bpp

    if ((buff = (char *)malloc(frame_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(5);
    }

    while (1)
    {
        // Read from video0
        ssize_t num_bytes_read = read(file_src, buff, frame_size);
        if (num_bytes_read == -1)
        {
            printf("Grab: Error during read\n");
            exit(6);
        }
        // Write to shared memory
        semaphoreLock(semID, SEM1_WRITE);
        memcpy(shm1, buff, frame_size);
        semaphoreUnlock(semID, SEM1_READ);
    }
}

// Transform /////////////////////////////////////////////////
int transform()
{
    char *frame_buff;
    char *disp_buff;
    char *shm1;
    char *shm2;

    int frame_width, frame_height;
    int display_width, display_height;
    unsigned long frame_size;
    unsigned long display_size;

    char r, g, b;
    unsigned short short_px;

    // Attach shared memory
    shm1 = (char *)shmat(shm1ID, NULL, 0);
    shm2 = (char *)shmat(shm2ID, NULL, 0);
    if (shm1 == (char *)-1 || shm2 == (char *)-1)
    {
        printf("shmat error\n");
        exit(2);
    }

    // Display buffer
    getDisplayDimensions(&display_width, &display_height);
    display_size = display_width * display_height * 2; // *2, ker je 16bpp
    if ((disp_buff = (char *)malloc(display_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(6);
    }
    // Image buffer
    frame_width = FRAME_WIDTH;
    frame_height = FRAME_HEIGHT;
    frame_size = frame_width * frame_height * 3; // *3, ker je 24bpp
    if ((frame_buff = (char *)malloc(frame_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(7);
    }

    while (1)
    {
        // Read from shared memory
        semaphoreLock(semID, SEM1_READ);
        memcpy(frame_buff, shm1, frame_size);
        semaphoreUnlock(semID, SEM1_WRITE);

        // Transform and copy input image to display buffer and create borders
        unsigned long j = 0; // Image pointer
        for (unsigned long i = 0; i < (unsigned long)(display_width * display_height); i++)
        {
            // Go through every display pixel, not individual "channel"
            // Check if within width bounds
            if ((i % display_width) >= frame_width)
            {
                disp_buff[2 * i] = 0;
                disp_buff[2 * i + 1] = 0;
            }
            // Check if within height bounds
            else if ((i / display_width) > frame_height)
            {
                disp_buff[2 * i] = 0;
                disp_buff[2 * i + 1] = 0;
            }
            // Else draw image pixel
            else
            {

                r = frame_buff[3 * j];
                g = frame_buff[3 * j + 1];
                b = frame_buff[3 * j + 2];

                j++;

                short_px = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); // convert to RGB
                short_px = (short_px >> 8) | (short_px << 8);             // Convert to big endian

                // Write to output buffer
                disp_buff[2 * i] = (short_px & 0xFF00) >> 8;
                disp_buff[2 * i + 1] = short_px & 0x00FF;
            }
        }
        // Write to shared memory
        semaphoreLock(semID, SEM2_WRITE);
        memcpy(shm2, disp_buff, display_size);
        semaphoreUnlock(semID, SEM2_READ);
    }
}

// Display ///////////////////////////////////////////////////
int display()
{
    char *disp_buff;
    char *shm2;
    int file_dest;

    int display_width, display_height;
    unsigned long display_size;

    shm2 = (char *)shmat(shm2ID, NULL, 0);
    if (shm2 == (char *)-1)
    {
        printf("shmat error\n");
        exit(2);
    }

    file_dest = open("/dev/fb0", O_RDWR);
    if (file_dest == -1)
    {
        printf("Invalid destination file\n");
        exit(3);
    }

    // Display buffer
    getDisplayDimensions(&display_width, &display_height);
    display_size = display_width * display_height * 2; // *2, ker je 16bpp
    if ((disp_buff = (char *)malloc(display_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(5);
    }

    while (1)
    {
        // Read from shared memory
        semaphoreLock(semID, SEM2_READ);
        memcpy(disp_buff, shm2, display_size);
        semaphoreUnlock(semID, SEM2_WRITE);

        lseek(file_dest, 0, SEEK_SET);

        // Write to fb0
        ssize_t blockWritten = write(file_dest, disp_buff, display_size);
        if (blockWritten == -1)
        {
            printf("Display: Error during write\n");
            exit(5);
        }
    }
}

void getDisplayDimensions(int *p_display_width, int *p_display_height)
{
    int fbfd = 0; // framebuffer filedescriptor
    struct fb_var_screeninfo var_info;

    // Open the framebuffer device file for reading and writing
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1)
    {
        printf("Error: cannot open framebuffer device.\n");
        exit(1);
    }

    // Get variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &var_info))
    {
        printf("Error reading variable screen info.\n");
        exit(1);
    }

    *p_display_width = (int)(var_info.xres);
    *p_display_height = (int)(var_info.yres);

    // close file
    close(fbfd);
    return;
}

void semaphoreLock(int semID, unsigned short semIndex)
{
    struct sembuf semaphore;

    semaphore.sem_num = semIndex;
    semaphore.sem_op = -1;
    semaphore.sem_flg = 0;
    semop(semID, &semaphore, 1);

    return;
}

void semaphoreUnlock(int semID, unsigned short semIndex)
{
    struct sembuf semaphore;

    semaphore.sem_num = semIndex;
    semaphore.sem_op = +1;
    semaphore.sem_flg = 0;
    semop(semID, &semaphore, 1);

    return;
}