#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <ctype.h>

#define PI 3.14159265358979323846
#define MAX_DAC_VALUE 0xFFFF
#define MID_DAC_VALUE 0x7FFF
#define DEFAULT_POINTS 100
#define DEFAULT_FREQUENCY 1.0
#define DEFAULT_AMPLITUDE 1.0
#define DEFAULT_MEAN 0.0
#define CONFIG_FILE "waveform.cfg"
#define MAX_FILENAME 256

// Waveform types
typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SAWTOOTH,
    WAVE_ARBITRARY
} WaveformType;

// Program state structure
typedef struct {
    WaveformType type;
    double frequency;
    double amplitude;
    double mean;
    int points;
    int running;
    char arb_filename[MAX_FILENAME];
    pthread_mutex_t mutex;
    double* arb_data;
    int arb_points;
} ProgramState;

// Global variables
static ProgramState state;
static struct termios orig_termios;
static volatile sig_atomic_t program_running = 1;

// Function prototypes
void* waveform_thread(void* arg);
void* keyboard_thread(void* arg);
void* display_thread(void* arg);
void setup_terminal(void);
void restore_terminal(void);
void signal_handler(int signum);
void save_config(void);
void load_config(void);
void load_arbitrary_waveform(const char* filename);
void cleanup(void);
void beep(void);
void display_waveform(double value);
unsigned short scaleToDAC(double value);
void writeToDAC(unsigned short value);

// Initialize program state
void init_state() {
    state.type = WAVE_SINE;
    state.frequency = DEFAULT_FREQUENCY;
    state.amplitude = DEFAULT_AMPLITUDE;
    state.mean = DEFAULT_MEAN;
    state.points = DEFAULT_POINTS;
    state.running = 1;
    state.arb_data = NULL;
    state.arb_points = 0;
    pthread_mutex_init(&state.mutex, NULL);
}

// Main waveform generation thread
void* waveform_thread(void* arg) {
    double phase = 0.0;
    double step;
    
    while(program_running) {
        pthread_mutex_lock(&state.mutex);
        step = 2.0 * PI / state.points;
        
        // Generate waveform based on type
        double value;
        switch(state.type) {
            case WAVE_SINE:
                value = state.amplitude * sin(phase) + state.mean;
                break;
                
            case WAVE_SQUARE:
                value = (phase < PI) ? state.amplitude : -state.amplitude;
                value += state.mean;
                break;
                
            case WAVE_TRIANGLE:
                value = (2 * state.amplitude / PI) * 
                       (phase < PI ? phase : (2 * PI - phase)) - state.amplitude + state.mean;
                break;
                
            case WAVE_SAWTOOTH:
                value = (state.amplitude * phase / (2 * PI)) - 
                       (state.amplitude / 2) + state.mean;
                break;
                
            case WAVE_ARBITRARY:
                if (state.arb_data && state.arb_points > 0) {
                    int index = (int)((phase / (2 * PI)) * state.arb_points);
                    value = state.arb_data[index] * state.amplitude + state.mean;
                } else {
                    value = 0;
                }
                break;
        }
        
        pthread_mutex_unlock(&state.mutex);
        
        // Output to DAC and display
        unsigned short dac_value = scaleToDAC(value);
        writeToDAC(dac_value);
        display_waveform(value);
        
        // Update phase
        phase += step;
        if(phase >= 2.0 * PI) phase -= 2.0 * PI;
        
        // Delay for frequency control
        usleep((useconds_t)(1000000.0 / (state.frequency * state.points)));
    }
    
    return NULL;
}

// Keyboard input handling thread
void* keyboard_thread(void* arg) {
    char c;
    while(program_running && read(STDIN_FILENO, &c, 1) == 1) {
        pthread_mutex_lock(&state.mutex);
        
        switch(c) {
            case 'q':
                program_running = 0;
                break;
            case '\033': // ESC sequence for arrow keys
                if(read(STDIN_FILENO, &c, 1) == 1) {
                    if(c == '[') {
                        read(STDIN_FILENO, &c, 1);
                        switch(c) {
                            case 'A': // Up - increase frequency
                                state.frequency *= 1.1;
                                beep();
                                break;
                            case 'B': // Down - decrease frequency
                                state.frequency /= 1.1;
                                beep();
                                break;
                            case 'C': // Right - increase amplitude
                                state.amplitude *= 1.1;
                                beep();
                                break;
                            case 'D': // Left - decrease amplitude
                                state.amplitude /= 1.1;
                                beep();
                                break;
                        }
                    }
                }
                break;
            case '1': state.type = WAVE_SINE; beep(); break;
            case '2': state.type = WAVE_SQUARE; beep(); break;
            case '3': state.type = WAVE_TRIANGLE; beep(); break;
            case '4': state.type = WAVE_SAWTOOTH; beep(); break;
            case '5': state.type = WAVE_ARBITRARY; beep(); break;
        }
        
        pthread_mutex_unlock(&state.mutex);
    }
    return NULL;
}

// Display thread for UI updates
void* display_thread(void* arg) {
    while(program_running) {
        pthread_mutex_lock(&state.mutex);
        
        // Clear screen
        printf("\033[2J\033[H");
        printf("=== Waveform Generator ===\n\n");
        printf("Current Settings:\n");
        printf("Type: %s\n", 
               state.type == WAVE_SINE ? "Sine" :
               state.type == WAVE_SQUARE ? "Square" :
               state.type == WAVE_TRIANGLE ? "Triangle" :
               state.type == WAVE_SAWTOOTH ? "Sawtooth" : "Arbitrary");
        printf("Frequency: %.2f Hz\n", state.frequency);
        printf("Amplitude: %.2f\n", state.amplitude);
        printf("Mean: %.2f\n", state.mean);
        
        printf("\nControls:\n");
        printf("↑/↓: Frequency  ←/→: Amplitude\n");
        printf("1-5: Change Waveform Type\n");
        printf("Q: Quit\n");
        
        pthread_mutex_unlock(&state.mutex);
        usleep(100000); // Update every 100ms
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // Initialize
    init_state();
    setup_terminal();
    signal(SIGINT, signal_handler);
    
    // Load configuration
    load_config();
    
    // Process command line arguments
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-f") == 0 && i + 1 < argc)
            state.frequency = atof(argv[++i]);
        else if(strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            state.amplitude = atof(argv[++i]);
        else if(strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            load_arbitrary_waveform(argv[++i]);
    }
    
    // Create threads
    pthread_t wave_tid, key_tid, disp_tid;
    pthread_create(&wave_tid, NULL, waveform_thread, NULL);
    pthread_create(&key_tid, NULL, keyboard_thread, NULL);
    pthread_create(&disp_tid, NULL, display_thread, NULL);
    
    // Wait for threads
    pthread_join(wave_tid, NULL);
    pthread_join(key_tid, NULL);
    pthread_join(disp_tid, NULL);
    
    // Cleanup
    cleanup();
    return 0;
}

// Utility functions (implementations)
void setup_terminal() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
}

void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void signal_handler(int signum) {
    program_running = 0;
}

void cleanup() {
    save_config();
    restore_terminal();
    pthread_mutex_destroy(&state.mutex);
    if(state.arb_data) free(state.arb_data);
}

void beep() {
    printf("\a");
    fflush(stdout);
}

// Placeholder implementations
void writeToDAC(unsigned short value) {
    // TODO: Implement actual DAC writing
}

void display_waveform(double value) {
    // TODO: Implement visual waveform display
}

unsigned short scaleToDAC(double value) {
    return (unsigned short)((value + 1.0) * MID_DAC_VALUE);
}

void save_config() {
    FILE* f = fopen(CONFIG_FILE, "w");
    if(f) {
        fprintf(f, "%d\n%f\n%f\n%f\n", 
                state.type, state.frequency, state.amplitude, state.mean);
        fclose(f);
    }
}

void load_config() {
    FILE* f = fopen(CONFIG_FILE, "r");
    if(f) {
        int type;
        fscanf(f, "%d\n%lf\n%lf\n%lf", 
               &type, &state.frequency, &state.amplitude, &state.mean);
        state.type = (WaveformType)type;
        fclose(f);
    }
}
