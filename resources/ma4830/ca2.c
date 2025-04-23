// Waveform Generator for QNX
// Based on PCI-DAS1602 hardware
// MA4829 Real-time Project

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hw/pci.h>
#include <hw/inout.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

// Hardware registers definition
#define INTERRUPT   iobase[1] + 0
#define MUXCHAN    iobase[1] + 2
#define TRIGGER    iobase[1] + 4
#define AUTOCAL    iobase[1] + 6
#define DA_CTLREG  iobase[1] + 8
#define AD_DATA    iobase[2] + 0
#define AD_FIFOCLR iobase[2] + 2
#define DA_Data    iobase[4] + 0
#define DA_FIFOCLR iobase[4] + 2
#define DIO_PORTA  iobase[3] + 4

// Constants
#define PI 3.14159265358979323846
#define POINTS_PER_CYCLE 100
#define MAX_FREQ 1000
#define MIN_FREQ 1

// Waveform types
enum WaveformType {
    SINE,
    SQUARE,
    TRIANGLE,
    SAWTOOTH,
    NOTHING
};


// Global variables
struct {
    enum WaveformType type;
    int frequency;
    int amplitude;
    int running;
    unsigned int* data;
    pthread_mutex_t mutex;
} state;

// PCI device variables
struct pci_dev_info info;
void *hdl;
uintptr_t iobase[6];
int badr[5];

// Function prototypes
void init_hardware(void);
void cleanup_hardware(void);
void generate_waveform(void);
void* input_thread(void* arg);
void* display_thread(void* arg);
void signal_handler(int sig);

int i;

void init_hardware(void) {
    // Initialize PCI hardware
    memset(&info, 0, sizeof(info));
    if(pci_attach(0) < 0) {
        perror("pci_attach");
        exit(EXIT_FAILURE);
    }

    // Set Vendor and Device ID
    info.VendorId = 0x1307;
    info.DeviceId = 0x01;

    if ((hdl = pci_attach_device(0, PCI_SHARE|PCI_INIT_ALL, 0, &info)) == 0) {
        perror("pci_attach_device");
        exit(EXIT_FAILURE);
    }
    
    // Map I/O base address

    for(i = 0; i < 5; i++) {
        badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
        iobase[i] = mmap_device_io(0x0f, badr[i]);
    }
    
    // Set thread control privileges
    if(ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("Thread Control");
        exit(1);
    }
}

void generate_waveform(void) {
    float delta = (2.0 * PI) / POINTS_PER_CYCLE;
    float value;

    pthread_mutex_lock(&state.mutex);
    
    for(i = 0; i < POINTS_PER_CYCLE; i++) {
        switch(state.type) {
            case SINE:
                value = sin(i * delta);
                break;
            case SQUARE:
                value = (i < POINTS_PER_CYCLE/2) ? 1.0 : -1.0;
                break;
            case TRIANGLE:
                value = (2.0 * fabs(i * (2.0/POINTS_PER_CYCLE) - 1.0) - 1.0);
                break;
            case SAWTOOTH:
                value = (i * (2.0/POINTS_PER_CYCLE) - 1.0);
                break;
            case NOTHING:
                value = 1;
                break;
        }
        
        // Scale value to DAC range and apply amplitude
        state.data[i] = (unsigned int)((value + 1.0) * 0x7fff * state.amplitude / 100);
    }
    
    pthread_mutex_unlock(&state.mutex);
}


void* input_thread(void* arg) {
    char input[20];
    while(state.running) {
        printf("\nEnter command (type 'help' for options): ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;  // Remove newline

        if(strcmp(input, "help") == 0) {
            printf("Commands:\n");
            printf("sine, square, triangle, sawtooth - Change waveform\n");
            printf("freq <value> - Set frequency (1-1000 Hz)\n");
            printf("amp <value> - Set amplitude (0-100%%)\n");
            printf("quit - Exit program\n");
        }
        else if(strcmp(input, "sine") == 0) {
            pthread_mutex_lock(&state.mutex);
            printf("INPUT SINE has the MUTEX");
            state.type = SINE;
            generate_waveform();
            printf("INPUT SINE finished with the MUTEX");
            pthread_mutex_unlock(&state.mutex);
            printf("\n Setting state to SINE\n");
        }
        else if(strcmp(input, "square") == 0) {
            pthread_mutex_lock(&state.mutex);
            state.type = SQUARE;
            generate_waveform();
            pthread_mutex_unlock(&state.mutex);
        }
        //sleep(1000000 / (state.frequency * POINTS_PER_CYCLE));
        // Add other command handlers...
        printf( "%d\n", state.running);
    }
    return NULL;
}

void* waveform_thread(void* arg) {
    while(state.running) {
        pthread_mutex_lock(&state.mutex);
        printf("WAVE has the MUTEX");
        for(i = 0; i < POINTS_PER_CYCLE; i++) {
            // Output to DAC
            out16(DA_CTLREG, 0x0a23);
            out16(DA_FIFOCLR, 0);
            out16(DA_Data, (short)state.data[i]);
            
            // Delay for frequency control
            usleep(1000000 / (state.frequency * POINTS_PER_CYCLE));
        }
        printf("WAVE finished with the MUTEX");
        pthread_mutex_unlock(&state.mutex);
    }
    return NULL;
}

void signal_handler(int sig) {
    if(sig == SIGINT) {
        pthread_mutex_lock(&state.mutex);
        printf("SIGINT has the MUTEX");
        printf("\nCleaning up...\n");
        state.running = 0;
        printf("done cleaning up!");
        printf("SIGINT finished with the MUTEX");
        pthread_mutex_unlock(&state.mutex);

    }
}

pthread_t input_tid;
pthread_t wave_tid;

int main(int argc, char* argv[]) {
    // Initialize state
    state.type = TRIANGLE;
    state.frequency = 1;
    state.amplitude = 100;
    state.running = 1;
    state.data = malloc(POINTS_PER_CYCLE * sizeof(unsigned int));
    //state.mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(&state.mutex, NULL);

    // Initialize hardware
    init_hardware();
    
    // Set up signal handler
    signal(SIGINT, signal_handler);

    // Generate initial waveform
    generate_waveform();

    // Create threads

    pthread_create(&input_tid, NULL, input_thread, NULL);
    pthread_create(&wave_tid, NULL, waveform_thread, NULL);

    // Wait for threads
    pthread_join(input_tid, NULL);
    printf("hello");
    pthread_join(wave_tid, NULL);

    // Cleanup
    pthread_mutex_destroy(&state.mutex);
    free(state.data);
    pci_detach_device(hdl);
    printf("end");
    
	return 0;
	
	}