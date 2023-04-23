/*

Creates three processes, which take video feed from /dev/video0, transform it from RGB888 to RGB565 and feed it to screen buffer /dev/fb0.

Usage (arguments in [] are optional):
./pipe [display_width] [display_height]

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

// Semaphores
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define FRAME_WIDTH_DEFAULT 640
#define FRAME_HEIGHT_DEFAULT 480
#define SEM1_READ 0
#define SEM1_WRITE 1
#define SEM2_READ 2
#define SEM2_WRITE 3

pid_t pid1, pid2;
int pipe1[2];
int pipe2[2];

int semID;

// Function definitions
int grab();
int transform();
int display();
void getDisplayDimensions(int *p_display_width, int *p_display_height);
void semaphoreLock(int semId, unsigned short semNum);
void semaphoreUnlock(int semId, unsigned short semNum);


int main(int argc, char *argv[])
{
    // Initialize semaphores for write first
    semID = semget(IPC_PRIVATE, 2, 0644);
    if (semID == -1) {
        perror("semget error\n");
        exit(1);
    }
    unsigned short semArray[2];
    semArray[SEM1_READ] = 0;
    semArray[SEM1_WRITE] = 1;
    if(semctl(semID, 0, SETALL, semArray) == -1) {
        printf("%s: semaphore initialization error\n", argv[0]);
        exit(1);
    }

    if(pipe(pipe1) == -1) {
        printf("Error while opening the pipe\n");
        return 1;
    }
    if(pipe(pipe2) == -1) {
        printf("Error while opening the pipe\n");
        return 1;
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
    int frame_width, frame_height;

    char *buff;
    ssize_t block_size, num_bytes_read;

    // Close unused pipe ends
    close(pipe1[0]);
    close(pipe2[0]);
    close(pipe2[1]);

    file_src = open("/dev/video0", O_RDONLY);
    if (file_src == -1)
    {
        printf("Invalid source file\n");
        exit(2);
    }

    frame_width = FRAME_WIDTH_DEFAULT;
    frame_height = FRAME_HEIGHT_DEFAULT;
    block_size = frame_width * frame_height * 3;

    if ((buff = (char *)malloc(block_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(5);
    }

    while (1)
    {
        // Read from video0
        num_bytes_read = read(file_src, buff, block_size);
        if (num_bytes_read == -1)
        {
            printf("Error during read\n");
            exit(6);
        }

        semaphoreLock(semID, SEM1_WRITE);

        // Write to shared memory
        // for (int i = 0; i < block_size; i += frame_width * 3)
        // {
        //     ssize_t blockWritten = write(pipe1[1], &buff[i], frame_width * 3);
        //     if (blockWritten == -1)
        //     {
        //         printf("Error during write\n");
        //         exit(5);
        //     }
        // }
        printf("Grab-debug: Quazi writing from video0 to pipe1[1]\n");
        semaphoreUnlock(semID, SEM1_READ);
    }
}

// Transform /////////////////////////////////////////////////
int transform()
{
    char *frame_buff;
    char *disp_buff;

    int frame_width, frame_height;
    int display_width, display_height;
    unsigned long frame_size;
    unsigned long display_size;

    char r, g, b;
    unsigned short short_px;

    frame_width = FRAME_WIDTH_DEFAULT;
    frame_height = FRAME_HEIGHT_DEFAULT;

    // Close unused pipe ends
    close(pipe1[1]);
    close(pipe2[0]);

    getDisplayDimensions(&display_width, &display_height);

    // Display buffer
    display_size = display_width * display_height * 2; // *2, ker je 16bpp
    if ((disp_buff = (char *)malloc(display_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(6);
    }
    // Image buffer
    frame_size = frame_width * frame_height * 3; // *3, ker je 24bpp
    if ((frame_buff = (char *)malloc(frame_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(7);
    }

    while (1)
    {
        semaphoreLock(semID, SEM1_READ);

        // Read from shared memory
        // for (int i = 0; i <= frame_size - 1; i += frame_width * 3)
        // {
        //     ssize_t blockRead = read(pipe1[0], &frame_buff[i], frame_width * 3);
        //     if (blockRead == -1)
        //     {
        //         printf("Error during read\n");
        //         exit(5);
        //     }
        // }
        printf("Transform-debug: Quazi reading from pipe1[0]\n");
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

                // // Draw colors
                // // Blue
                // disp_buff[2 * i] = 0xF8;
                // disp_buff[2 * i + 1] = 0x00;
                // // Red
                // disp_buff[2 * i] = 0x07;
                // disp_buff[2 * i + 1] = 0xE0;
                // Green
                // disp_buff[2 * i] = 0x00;
                // disp_buff[2 * i + 1] = 0x1F;
            }
        }

        for (int i = 0; i <= display_size - 1; i += display_width * 2)
        {
            ssize_t blockWritten = write(pipe2[1], &disp_buff[i], display_width * 2);
            if (blockWritten == -1)
            {
                printf("Error during write\n");
                exit(5);
            }
        }
    }
}

// Display ///////////////////////////////////////////////////
int display()
{

    // File descriptors (file offset & status flags)
    // int file_src;
    int file_dest;

    // int frame_width, frame_height;
    int display_width, display_height;

    char *disp_buff;
    unsigned long display_size;

    // char *frame_buff;
    // unsigned long frame_size;
    // ssize_t num_bytes_read, num_bytes_written;

    // Close unused pipe ends
    close(pipe1[0]);
    close(pipe1[1]);
    close(pipe2[1]);


    // Get display dimensions
    getDisplayDimensions(&display_width, &display_height);

    // Open destination file
    file_dest = open("/dev/fb0", O_RDWR);
    if (file_dest == -1)
    {
        printf("Invalid destination file\n");
        exit(3);
    }

    // Allocate display buffer
    display_size = display_width * display_height * 2; // *2, ker je 16bpp
    if ((disp_buff = (char *)malloc(display_size)) == NULL)
    {
        printf("Error during memory allocation\n");
        exit(5);
    }

    while (1)
    {
        for (int i = 0; i < display_size - 1; i += display_width * 2)
        {
            ssize_t blockRead = read(pipe2[0], &disp_buff[i], display_width * 2);
            // printf("Blockread: %ld\n", blockRead);
            // totalBytesRead += blockRead;
            // printf("i\n");
            if (blockRead == -1)
            {
                printf("Error during read\n");
                exit(5);
            }
        }

        lseek(file_dest, 0, SEEK_SET);

        for (int i = 0; i <= display_size - 1; i += display_width * 2)
        {
            ssize_t blockWritten = write(file_dest, &disp_buff[i], display_width * 2);
            if (blockWritten == -1)
            {
                printf("Error during write\n");
                exit(5);
            }
        }
    }
}

// Other functions ///////////////////////////////////////////
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

// Lock
void semaphoreLock(int semID, unsigned short semIndex) {
    struct sembuf semaphore;

    semaphore.sem_num = semIndex;
    semaphore.sem_op = -1;
    semaphore.sem_flg = 0;
    semop(semID, &semaphore, 1);

    return;
}

void semaphoreUnlock(int semID, unsigned short semIndex) {
    struct sembuf semaphore;

    semaphore.sem_num = semIndex;
    semaphore.sem_op = +1;
    semaphore.sem_flg = 0;
    semop(semID, &semaphore, 1);

    return;
}