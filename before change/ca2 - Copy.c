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
#include <termios.h>
#include <time.h>
#include <fcntl.h>

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
#define POINTS_PER_CYCLE 20
#define MAX_FREQ 1000
#define MIN_FREQ 1
#define FREQ_MULT 2
#define FREQ_STEP 1
#define AMP_STEP 1
#define PULSE_WIDTH_RATIO 0.1

// Waveform types
enum WaveformType {
    SINE,
    SQUARE,
    TRIANGLE,
    SAWTOOTH,
    PULSE,
    CARDIAC,
    NOTHING
};


// Global variables
struct {
    enum WaveformType type;
    float frequency;
    int amplitude;
    int running;
    unsigned int* data;
} state;

int i; 
double t; 
int freq; 
int amp; 
FILE* file;
char* equals;
char* value;
char* newline;
int fd; // for file read write
float frequency;
int amplitude;

//variables for thread control
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// PCI device variables
struct pci_dev_info info;
void *hdl;
uintptr_t iobase[6];
int badr[5];

// Add these global variables
uint16_t adc_in;
double pot_freq = 1.0;  // Renamed from pot_frequency
double pot_amp = 1.0;   // Renamed from pot_amplitude
unsigned int count;     // Renamed from count
unsigned short chan;       // Renamed from chan

// Function prototypes
void init_hardware(void);
void cleanup_hardware(void);
void generate_waveform(void);
void* input_thread(void* arg);
void* display_thread(void* arg);
void signal_handler(int sig);
void print_usage(char* program_name);
void save_settings(const char* filename);
int load_settings(const char* filename);
void* adc_thread(void* arg);
void* analog_input_thread(void* arg);
int read_config_file(const char* filename);



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

void set_raw_mode(int enable) {
	static struct termios oldt, newt;
	if (enable) {
		tcgetattr(STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	} else {
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	}
}

void generate_waveform(void) {
    float delta = (2.0 * PI) / POINTS_PER_CYCLE;
    float value;

    pthread_mutex_lock(&mutex);
    
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
                value = (double) i / (double) (POINTS_PER_CYCLE -1);
                break;
           	case PULSE:
                value = (i < POINTS_PER_CYCLE * PULSE_WIDTH_RATIO) ? 1.0 : 0.0;
                break;
           	case CARDIAC:
                t =  (double) i / POINTS_PER_CYCLE;
                value = exp(-200.0 * pow(t-0.2, 2)) - 0.1 * exp(-50.0 * pow(t - 0.35, 2)) + 0.05 * exp(-300.0 * pow(t-0.75, 2));
                break;
            case NOTHING:
                value = 1;
                break;
        }
        
        // Scale value to DAC range and apply amplitude
        state.data[i] = (unsigned int)((value + 1.0) * 0x7fff * state.amplitude / 100);
    }
    
    pthread_mutex_unlock(&mutex);
}



void write_file(void) {
    char buffer[256];
    int len;
    
    // Open file with write, create, and truncate flags
    fd = open("waveform_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Cannot open log file");
        exit(1);
    }
    
    // Write initial settings
    len = sprintf(buffer, "Initial settings:\n");
    write(fd, buffer, len);
    len = sprintf(buffer, "Waveform type: %d\n", state.type);
    write(fd, buffer, len);
    len = sprintf(buffer, "Frequency: %.2f Hz\n", state.frequency);
    write(fd, buffer, len);
    len = sprintf(buffer, "Amplitude: %d%%\n\n", state.amplitude);
    write(fd, buffer, len);
}

// Settings file functions
void save_settings(const char* filename) {
    if (!filename || strlen(filename) == 0) {
        printf("Error: Invalid filename\n");
        return;
    }

    pthread_mutex_lock(&mutex);
    file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not open %s for writing\n", filename);
        pthread_mutex_unlock(&mutex);
        return;
    }
    
    fprintf(file, "wave_type=%d\n", state.type);
    fprintf(file, "frequency=%f\n", state.frequency);
    fprintf(file, "amplitude=%d\n", state.amplitude);
    
    fclose(file);
    printf("Settings saved to %s\n", filename);
    pthread_mutex_unlock(&mutex);
}

int load_settings(const char* filename) {
    char line[256];
    char* equals;
    char* value;
    char* newline;
    int len;
    
    
    if (!filename || strlen(filename) == 0) {
        printf("Error: Invalid filename\n");
        return 0;
    }

    pthread_mutex_lock(&mutex);
    file = fopen(filename, "r");
    if (!file) {
        printf("Could not open %s for reading\n", filename);
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline if present
        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';  // Split at equals sign
            value = equals + 1;
            
            // Trim any whitespace
            while (*value && isspace(*value)) value++;
            
            if (strcmp(line, "wave_type") == 0) {
                int type = atoi(value);
                if (type >= SINE && type <= NOTHING) {
                    type = type;
                }
            }
            else if (strcmp(line, "frequency") == 0) {
                float freq = atof(value);
                if (freq >= 1 && freq <= 1000) {
                    frequency = freq;
                }
            }
            else if (strcmp(line, "amplitude") == 0) {
                int amp = atoi(value);
                if (amp >= 0 && amp <= 100) {
                    amplitude = amp;
                }
            }
        }
    }
    
    fclose(file);
    printf("Settings loaded from %s\n", filename);
    pthread_mutex_unlock(&mutex);
    return 1;
}

void* input_thread(void* arg) {
    char input[50];
    char filename[256];  // Separate buffer for filename
    int len;
    


    while(state.running) {
        printf("\nEnter command (type 'help' for options): ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) { //when error
            if (!state.running) break;
            continue;
        }
        
        // Remove newline
        len = strlen(input);

        if (len > 0 && input[len-1] == '\n') {
            input[len-1] = '\0';
            len--;
        }
        
        if (len == 0) continue;  // Skip empty lines

        if(strcmp(input, "help") == 0) {
            printf("Commands:\n");
            printf("sine, square, triangle, sawtooth, pulse, cardiac - Change waveform\n");
            printf("freq <value> - Set frequency (1-1000 Hz)\n");
            printf("amp <value> - Set amplitude (0-100%%)\n");
            printf("save <filename> - Save current settings\n");
            printf("load <filename> - Load settings from file\n");
            printf("quit - Exit program\n");
        }
        else if(strcmp(input, "sine") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = SINE;
            printf("\n Setting to SINE\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strcmp(input, "square") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = SQUARE;
            printf("\n Setting to SQUARE\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strcmp(input, "triangle") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = TRIANGLE;
            printf("\n Setting to TRIANGLE\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strcmp(input, "sawtooth") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = SAWTOOTH;
            printf("\n Setting to SAWTOOTH\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strcmp(input, "pulse") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = PULSE;
            printf("\n Setting to PULSE\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strcmp(input, "cardiac") == 0) {
            pthread_mutex_lock(&mutex);
            state.type = CARDIAC;
            printf("\n Setting to CARDIAC\n");
            generate_waveform();
            pthread_mutex_unlock(&mutex);
        }
        else if(strncmp(input, "freq ", 5) == 0) {
        	freq =  atoi(&input[5]);
        	if (freq >= 1 && freq <= 1000) {
        		pthread_mutex_lock(&mutex);
            	frequency = freq;
            	printf("\n Frequency set to %d Hz\n", freq);
            	generate_waveform();
            	pthread_mutex_unlock(&mutex);
            } else {
            	printf("Invalid frequency. Must be 1 - 1000Hz\n");
            }
    	}
    	else if(strncmp(input, "amp ", 4) == 0) {
        	amp =  atoi(&input[4]);
        	if (amp >= 0 && amp <= 100) {
        		pthread_mutex_lock(&mutex);
            	amplitude = amp;
            	printf("\n Amplitude set to %d %\n", amp);
            	generate_waveform();
            	pthread_mutex_unlock(&mutex);
            } else {
            	printf("Invalid amplitude. Must be 1 - 100 %\n");
            }
    	}
        else if(strncmp(input, "save ", 5) == 0) {
            if (len <= 5) {
                printf("Error: Please specify a filename\n");
                continue;
            }
            strncpy(filename, input + 5, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            save_settings(filename);
        }
        else if(strncmp(input, "load ", 5) == 0) {
            if (len <= 5) {
                printf("Error: Please specify a filename\n");
                continue;
            }
            strncpy(filename, input + 5, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            if (load_settings(filename)) {
                generate_waveform();
            }
        }
    }
    return NULL;
}

// New function to read potentiometer values
void* adc_thread(void* arg) {
    int len;
    char buffer[256];

    // Initialize ADC
    out16(INTERRUPT, 0x60c0); // sets interrupts - Clears
    out16(TRIGGER, 0x2081);   // sets trigger control: 10MHz, clear, Burst off, SW trig
    out16(AUTOCAL, 0x007f);   // sets automatic calibration
    out16(AD_FIFOCLR, 0);     // clear ADC buffer
    out16(MUXCHAN, 0x0D00);   // set mux for potentiometer input

    printf("\nReading ADC values...\n");
    
    count = 0x00;
    while(state.running) {
        pthread_mutex_lock(&mutex);
        
        chan = ((count & 0x0f) << 4) | (0x0f & count);
        out16(MUXCHAN, 0x0D00 | chan); // Set channel - burst mode off
        delay(1);                      // allow mux to settle
        out16(AD_DATA, 0);             // start ADC
        while (!(in16(MUXCHAN) & 0x4000));

        adc_in = in16(AD_DATA);

        if (count == 0) // Frequency control potentiometer
        {
            pot_freq = 0.5 + ((double)adc_in / 65535.0);
            // Write to file
            len = sprintf(buffer, "Frequency pot: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                perror("write failed");
                exit(1);
            }        
        }
        if (count == 1) // Amplitude control potentiometer
        {
            pot_amp = 0.5 + 0.5 * (double)(adc_in-1.0) / 0xFFFF; // Scale to 0 to 1
            pot_amp *= (amplitude/2.5);
            // Write to file
            len = sprintf(buffer, "Amplitude pot: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                perror("write failed");
                exit(1);
            }
        }

        printf("Pot frequency: %.2f\n", state.frequency * pot_freq);
        printf("Pot amplitude: %.2f\n", state.amplitude * pot_amp);
        
        fflush(stdout);
        count++;
        if (count == 0x02) count = 0x00;
        delay(1);

        pthread_mutex_unlock(&mutex);
    }

    printf("ADC thread finished\n");
    return NULL;
}

// Modify waveform_thread to use potentiometer values
void* waveform_thread(void* arg) {
    struct timespec start, now;
    double period_ns;
    int point_index = 0;
    char buffer[256];
    int len;
    float actual_amplitude;

    generate_waveform();
    write_file();
    
    while(state.running) {
        pthread_mutex_lock(&mutex);
        
        // Use potentiometer values to modify frequency and amplitude
        period_ns = (1000000000.0 / ((frequency * pot_freq) * POINTS_PER_CYCLE));
        actual_amplitude = (short) state.data[point_index] * pot_amp;

        clock_gettime(CLOCK_MONOTONIC, &start);

        out16(DA_CTLREG, 0x0a23);
        out16(DA_FIFOCLR, 0);
        out16(DA_Data, (short)actual_amplitude);

        len = sprintf(buffer, "Point %d: Value = 0x%04X (modified by pots)\n", 
                     point_index, (unsigned short)actual_amplitude);
        write(fd, buffer, len);

        point_index = (point_index + 1) % POINTS_PER_CYCLE;

        if (point_index == 0) {
            printf("\a");
            fflush(stdout);
            
        }
        
        do {
            clock_gettime(CLOCK_MONOTONIC, &now);
        } while ((now.tv_sec - start.tv_sec) * 1000000000 + 
                 (now.tv_nsec - start.tv_nsec) < period_ns);

        pthread_mutex_unlock(&mutex);
    }

    close(fd);
    return NULL;
}

void signal_handler(int sig) {
    if(sig == SIGINT) {
        pthread_mutex_lock(&mutex);
        printf("\nCleaning up...\n");
        state.running = 0;

        if (fd > 0) {
            char buffer[256];
            int len = sprintf(buffer, "\nProgram terminated by user\n");
            write(fd, buffer, len);
            close(fd);
        }
        printf("done cleaning up!");
        pthread_mutex_unlock(&mutex);
        usleep(1000);
        signal(SIGINT, SIG_DFL);
        write(STDOUT_FILENO, "\nExiting Program\n", 27);
    }
}


pthread_t input_tid;
pthread_t wave_tid;
pthread_t adc_tid;
pthread_t analog_tid;  // New thread ID for analog input

void print_usage(char* program_name) {
    printf("Usage: %s -w wave_type -f frequency -a amplitude\n", program_name);
    printf("  wave_type: sine, square, triangle, sawtooth, pulse, cardiac\n");
    printf("  frequency: 1-1000 Hz\n");
    printf("  amplitude: 0-100%%\n");
    printf("Example: %s -w sine -f 100 -a 50\n", program_name);
}


// New function to read analog inputs
void* analog_input_thread(void* arg) {
    /* Variables for ADC control */
    int write_len;
    char log_buffer[256];
    double scaled_freq;
    double scaled_amp;

    /* ADC initialization sequence */
    out16(INTERRUPT, 0x60c0);  /* Clear/set interrupts */
    out16(TRIGGER, 0x2081);    /* Set trigger settings */
    out16(AUTOCAL, 0x007f);    /* Enable auto-calibration */
    out16(AD_FIFOCLR, 0);      /* Reset ADC FIFO */
    out16(MUXCHAN, 0x0D00);    /* Configure MUX */

    printf("\nStarting analog input monitoring...\n");
    
    count = 0;
    while(state.running) {
        pthread_mutex_lock(&mutex);
        
        /* Set ADC channel and read value */
        chan = ((count & 0x0f) << 4) | (0x0f & count);
        out16(MUXCHAN, 0x0D00 | chan);
        delay(1);  /* Settling time */
        out16(AD_DATA, 0);
        
        /* Wait for conversion complete */
        while(!(in16(MUXCHAN) & 0x4000));
        adc_in = in16(AD_DATA);

        /* Process channel 0 - Frequency scaling */
        if(count == 0) {
            pot_freq = 0.5 + ((double)adc_in / 65535.0);
            scaled_freq = state.frequency * pot_freq;
            
            write_len = sprintf(log_buffer, "ADC0 (Freq): Raw=%d, Scale=%.3f, Result=%.2fHz\n", 
                              adc_in, pot_freq, scaled_freq);
            write(fd, log_buffer, write_len);
        }
        /* Process channel 1 - Amplitude scaling */
        else if(count == 1) {
            pot_amp = 0.25 + (0.75 * (double)adc_in / 65535.0);
            scaled_amp = state.amplitude * pot_amp;
            
            write_len = sprintf(log_buffer, "ADC1 (Amp): Raw=%d, Scale=%.3f, Result=%.2f%%\n", 
                              adc_in, pot_amp, scaled_amp);
            write(fd, log_buffer, write_len);
        }

        /* Debug output */
        printf("Current values - Freq: %.2fHz, Amp: %.2f%%\n", 
               state.frequency * pot_freq, 
               state.amplitude * pot_amp);
        fflush(stdout);

        /* Cycle between channels */
        count = (count + 1) % 2;
        delay(1);

        pthread_mutex_unlock(&mutex);
    }

    printf("Analog input monitoring stopped\n");
    return NULL;
}

// Modified settings functions
int read_config_file(const char* filename) {
    /* Variables for file parsing */
    char line[256];
    char param_name[32];
    char param_value[32];
    FILE* config_file;
    int success = 0;
    
    if(!filename) {
        printf("Error: No filename provided\n");
        return 0;
    }

    pthread_mutex_lock(&mutex);
    
    config_file = fopen(filename, "r");
    if(!config_file) {
        printf("Could not open configuration file: %s\n", filename);
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    
    /* Read configuration line by line */
    while(fgets(line, sizeof(line), config_file)) {
        /* Skip empty lines and comments */
        if(line[0] == '\n' || line[0] == '#') {
            continue;
        }
        
        /* Parse parameter and value */
        if(sscanf(line, "%31[^=]=%31s", param_name, param_value) == 2) {
            /* Remove trailing whitespace */
            char* end = param_name + strlen(param_name) - 1;
            while(end > param_name && isspace(*end)) {
                *end-- = '\0';
            }
            
            /* Process each parameter */
            if(strcmp(param_name, "waveform") == 0) {
                if(strcmp(param_value, "sine") == 0) state.type = SINE;
                else if(strcmp(param_value, "square") == 0) state.type = SQUARE;
                else if(strcmp(param_value, "triangle") == 0) state.type = TRIANGLE;
                else if(strcmp(param_value, "sawtooth") == 0) state.type = SAWTOOTH;
                else if(strcmp(param_value, "pulse") == 0) state.type = PULSE;
                else if(strcmp(param_value, "cardiac") == 0) state.type = CARDIAC;
                success = 1;
            }
            else if(strcmp(param_name, "freq") == 0) {
                float f = atof(param_value);
                if(f >= MIN_FREQ && f <= MAX_FREQ) {
                    state.frequency = f;
                    success = 1;
                }
            }
            else if(strcmp(param_name, "amp") == 0) {
                int a = atoi(param_value);
                if(a >= 0 && a <= 100) {
                    state.amplitude = a;
                    success = 1;
                }
            }
        }
    }
    
    fclose(config_file);
    
    if(success) {
        printf("Configuration loaded from %s\n", filename);
        generate_waveform();  /* Update waveform with new settings */
    }
    
    pthread_mutex_unlock(&mutex);
    return success;
}

int main(int argc, char* argv[]) {
    // Initialize with defaults
    state.type = SQUARE;
    state.frequency = 1;
    state.amplitude = 100;
    state.running = 1;
    state.data = malloc(POINTS_PER_CYCLE * sizeof(unsigned int));
    pthread_mutex_init(&mutex, NULL);

    // Try to load default configuration
    if(access("default.txt", F_OK) == 0) {
        printf("Loading default configuration...\n");
        read_config_file("default.txt");
    }

    // Parse command line arguments (these override default settings)
    for(i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if(strcmp(argv[i], "-w") == 0) {
            if(i + 1 >= argc) {
                printf("Error: -w requires a wave type\n");
                print_usage(argv[0]);
                return 1;
            }
            i++;  // Move to wave type argument
            if(strcmp(argv[i], "sine") == 0) state.type = SINE;
            else if(strcmp(argv[i], "square") == 0) state.type = SQUARE;
            else if(strcmp(argv[i], "triangle") == 0) state.type = TRIANGLE;
            else if(strcmp(argv[i], "sawtooth") == 0) state.type = SAWTOOTH;
            else if(strcmp(argv[i], "pulse") == 0) state.type = PULSE;
            else if(strcmp(argv[i], "cardiac") == 0) state.type = CARDIAC;
            else {
                printf("Invalid wave type: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }
        }
        else if(strcmp(argv[i], "-f") == 0) {
            if(i + 1 >= argc) {
                printf("Error: -f requires a frequency value\n");
                print_usage(argv[0]);
                return 1;
            }
            i++;  // Move to frequency argument
            frequency = atoi(argv[i]);
            if(frequency < 1 || frequency > 1000) {
                printf("Frequency must be between 1 and 1000 Hz\n");
                print_usage(argv[0]);
                return 1;
            }
        }
        else if(strcmp(argv[i], "-a") == 0) {
            if(i + 1 >= argc) {
                printf("Error: -a requires an amplitude value\n");
                print_usage(argv[0]);
                return 1;
            }
            i++;  // Move to amplitude argument
            amplitude = atoi(argv[i]);
            if(amplitude < 0 || amplitude > 100) {
                printf("Amplitude must be between 0 and 100%%\n");
                print_usage(argv[0]);
                return 1;
            }
        }
        else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Initialize hardware
    init_hardware();
    
    // Set up signal handler
    signal(SIGINT, signal_handler);

    // Generate initial waveform
    generate_waveform();

    // Create threads
    
    pthread_create(&input_tid, NULL, input_thread, NULL);
    pthread_create(&wave_tid, NULL, waveform_thread, NULL);
    pthread_create(&adc_tid, NULL, adc_thread, NULL);
    pthread_create(&analog_tid, NULL, analog_input_thread, NULL);

    // Wait for threads
    pthread_join(input_tid, NULL);
    pthread_join(wave_tid, NULL);
    pthread_join(adc_tid, NULL);
    pthread_join(analog_tid, NULL);

    // Cleanup
    pthread_mutex_destroy(&mutex);
    free(state.data);
    pci_detach_device(hdl);
    printf("End of Program");
    
    return 0;
}
