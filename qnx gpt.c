#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <sys/neutrino.h>  // QNX specific
#include <sys/netmgr.h>    // QNX specific
#include <sys/syspage.h>   // QNX specific
#include <hw/inout.h>      // QNX specific for I/O
#include <time.h>

// Channel and pulse IDs for QNX message passing
#define PULSE_CODE_TIMER   _PULSE_CODE_MINAVAIL
#define PULSE_CODE_KEYBOARD (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_PRIORITY     10

// DAC Configuration (adjust based on your hardware)
#define DAC_BASE_ADDR     0x300   // Example base address
#define DAC_DATA_REG      (DAC_BASE_ADDR + 0)
#define DAC_CTRL_REG      (DAC_BASE_ADDR + 1)

typedef struct _pulse msg_header_t;

// Program state structure
typedef struct {
    int chid;              // Channel ID for message passing
    int timer_id;          // Timer ID
    pthread_mutex_t mutex;
    volatile uint16_t dac_value;
    double frequency;
    double amplitude;
    double phase;
    int waveform_type;
    int running;
} ProgramState;

// Global state
static ProgramState state;

// Function prototypes
void* waveform_thread(void* arg);
void* keyboard_thread(void* arg);
void* display_thread(void* arg);
int setup_timer(void);
int init_hardware(void);
void cleanup_hardware(void);
void write_to_dac(uint16_t value);

// Initialize QNX timer
int setup_timer(void) {
    struct sigevent event;
    timer_t timer_id;
    struct itimerspec timer;
    
    // Create a timer that delivers a pulse
    SIGEV_PULSE_INIT(&event, state.chid, PULSE_PRIORITY, PULSE_CODE_TIMER, 0);
    
    if (timer_create(CLOCK_REALTIME, &event, &timer_id) == -1) {
        perror("timer_create failed");
        return -1;
    }
    
    // Configure timer period (e.g., 1ms)
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_nsec = 1000000; // 1ms
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_nsec = 1000000;
    
    if (timer_settime(timer_id, 0, &timer, NULL) == -1) {
        perror("timer_settime failed");
        return -1;
    }
    
    state.timer_id = timer_id;
    return 0;
}

// Initialize hardware (DAC)
int init_hardware(void) {
    // Request I/O privileges
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl failed");
        return -1;
    }
    
    // Map I/O ports for DAC
    if (mmap_device_io(2, DAC_BASE_ADDR) == MAP_DEVICE_FAILED) {
        perror("mmap_device_io failed");
        return -1;
    }
    
    return 0;
}

// Write to DAC hardware
void write_to_dac(uint16_t value) {
    out16(DAC_DATA_REG, value);
}

// Waveform generation thread
void* waveform_thread(void* arg) {
    msg_header_t msg;
    
    while (state.running) {
        // Wait for timer pulse
        if (MsgReceivePulse(state.chid, &msg, sizeof(msg), NULL) == -1) {
            perror("MsgReceivePulse failed");
            continue;
        }
        
        if (msg.code == PULSE_CODE_TIMER) {
            pthread_mutex_lock(&state.mutex);
            
            // Calculate next sample
            double value = 0.0;
            switch (state.waveform_type) {
                case 0: // Sine
                    value = state.amplitude * sin(state.phase);
                    break;
                case 1: // Square
                    value = state.amplitude * (state.phase < M_PI ? 1.0 : -1.0);
                    break;
                case 2: // Triangle
                    value = state.amplitude * (1.0 - fabs(fmod(state.phase, 2.0 * M_PI) / M_PI - 1.0));
                    break;
                // Add other waveforms as needed
            }
            
            // Update phase
            state.phase += 2.0 * M_PI * state.frequency / 1000.0; // Assuming 1ms timer
            if (state.phase >= 2.0 * M_PI) {
                state.phase -= 2.0 * M_PI;
            }
            
            // Scale to DAC range and write
            uint16_t dac_value = (uint16_t)((value + 1.0) * 32767.5);
            write_to_dac(dac_value);
            
            pthread_mutex_unlock(&state.mutex);
        }
    }
    
    return NULL;
}

// Keyboard input thread
void* keyboard_thread(void* arg) {
    struct termios old_term, new_term;
    
    // Set up terminal for raw input
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    
    while (state.running) {
        char c = getchar();
        pthread_mutex_lock(&state.mutex);
        
        switch (c) {
            case 'q':
                state.running = 0;
                break;
            case '1':
                state.waveform_type = 0; // Sine
                break;
            case '2':
                state.waveform_type = 1; // Square
                break;
            case '3':
                state.waveform_type = 2; // Triangle
                break;
            case '+':
                state.frequency *= 1.1;
                break;
            case '-':
                state.frequency /= 1.1;
                break;
            case '[':
                state.amplitude *= 0.9;
                break;
            case ']':
                state.amplitude *= 1.1;
                break;
        }
        
        pthread_mutex_unlock(&state.mutex);
    }
    
    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    return NULL;
}

// Display thread
void* display_thread(void* arg) {
    while (state.running) {
        pthread_mutex_lock(&state.mutex);
        
        printf("\033[2J\033[H"); // Clear screen
        printf("=== QNX Waveform Generator ===\n\n");
        printf("Waveform: %s\n", 
               state.waveform_type == 0 ? "Sine" :
               state.waveform_type == 1 ? "Square" :
               state.waveform_type == 2 ? "Triangle" : "Unknown");
        printf("Frequency: %.2f Hz\n", state.frequency);
        printf("Amplitude: %.2f\n", state.amplitude);
        
        printf("\nControls:\n");
        printf("1-3: Change waveform type\n");
        printf("+/-: Adjust frequency\n");
        printf("[/]: Adjust amplitude\n");
        printf("q: Quit\n");
        
        pthread_mutex_unlock(&state.mutex);
        delay(100); // 100ms update rate
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t wave_tid, key_tid, disp_tid;
    
    // Initialize state
    memset(&state, 0, sizeof(state));
    state.running = 1;
    state.frequency = 1.0;
    state.amplitude = 1.0;
    pthread_mutex_init(&state.mutex, NULL);
    
    // Create channel for timer pulses
    state.chid = ChannelCreate(0);
    if (state.chid == -1) {
        perror("ChannelCreate failed");
        return EXIT_FAILURE;
    }
    
    // Initialize hardware
    if (init_hardware() == -1) {
        return EXIT_FAILURE;
    }
    
    // Set up timer
    if (setup_timer() == -1) {
        return EXIT_FAILURE;
    }
    
    // Create threads
    pthread_create(&wave_tid, NULL, waveform_thread, NULL);
    pthread_create(&key_tid, NULL, keyboard_thread, NULL);
    pthread_create(&disp_tid, NULL, display_thread, NULL);
    
    // Wait for threads
    pthread_join(wave_tid, NULL);
    pthread_join(key_tid, NULL);
    pthread_join(disp_tid, NULL);
    
    // Cleanup
    timer_delete(state.timer_id);
    ChannelDestroy(state.chid);
    pthread_mutex_destroy(&state.mutex);
    cleanup_hardware();
    
    return EXIT_SUCCESS;
}

void cleanup_hardware(void) {
    // Set DAC to mid-range
    write_to_dac(0x8000);
    
    // Unmap I/O ports
    munmap_device_io(DAC_BASE_ADDR, 2);
}
