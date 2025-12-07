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

// Hardware control registers for PCI-DAS1602
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

/* Program state structure to track current waveform settings */
struct {
    enum WaveformType type;         /* Current waveform type */
    float frequency;                /* Current frequency in Hz */
    int amplitude;                  /* Current amplitude (0-100%) */
    int running;                    /* Program running flag */
    unsigned int* data;             /* Waveform data buffer */
} state;

/* Global variables for program operation */
int i;                             /* General purpose counter */
double t;                          /* Time variable for waveform generation */
int freq;                          /* Temporary frequency storage */
int amp;                           /* Temporary amplitude storage */
FILE* file;                        /* File handle for settings */
char* equals;                      /* String parsing helper */
char* value;                       /* String parsing helper */
char* newline;                     /* String parsing helper */
int fd;                            /* File descriptor for logging */
float frequency;                   /* Working frequency value */
int amplitude;                     /* Working amplitude value */
int mode;                         /* Input mode (1=keyboard, 2=potentiometer) */

/* Thread synchronization */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* PCI hardware interface variables */
struct pci_dev_info info;
void *hdl;
uintptr_t iobase[6];
int badr[5];

/* ADC variables */
uint16_t adc_in;                   /* ADC input value */
unsigned int count;                /* Channel counter */
unsigned short chan;               /* Current channel */

/* Thread identifiers */
pthread_t input_tid;               /* Keyboard/command input thread */
pthread_t wave_tid;                /* Waveform generation thread */
pthread_t analog_tid;              /* Potentiometer input thread */

// Initialize PCI card and set up hardware registers
void init_hardware(void) {
    /* Initialize PCI hardware connection */
    memset(&info, 0, sizeof(info));
    if(pci_attach(0) < 0) {
        perror("pci_attach");
        exit(EXIT_FAILURE);
    }

    /* Configure PCI device parameters */
    info.VendorId = 0x1307;
    info.DeviceId = 0x01;

    /* Attach to PCI device */
    if ((hdl = pci_attach_device(0, PCI_SHARE|PCI_INIT_ALL, 0, &info)) == 0) {
        perror("pci_attach_device");
        exit(EXIT_FAILURE);
    }
    
    /* Map I/O base addresses */
    for(i = 0; i < 5; i++) {
        badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
        iobase[i] = mmap_device_io(0x0f, badr[i]);
    }
    
    /* Set up thread I/O privileges */
    if(ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("Thread Control");
        exit(1);
    }
}

/* Generate waveform data based on current settings */
void generate_waveform(void) {
    float delta = (2.0 * PI) / POINTS_PER_CYCLE;
    float value;

    pthread_mutex_lock(&mutex);
    
    /* Generate points for one complete cycle */
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
                value = (2.0 *((double) i / (double) (POINTS_PER_CYCLE -1)) -1.0);
                break;
            case PULSE:
                value = (i < POINTS_PER_CYCLE * PULSE_WIDTH_RATIO) ? 1.0 : 0.0;
                break;
            case CARDIAC:
                /* Simulate cardiac waveform using Gaussian functions */
                t = (double) i / POINTS_PER_CYCLE;
                value = exp(-200.0 * pow(t-0.2, 2)) - 
                       0.1 * exp(-50.0 * pow(t - 0.35, 2)) + 
                       0.05 * exp(-300.0 * pow(t-0.75, 2));
                break;
            case NOTHING:
                value = 1;
                break;
        }
        
        /* Scale value to DAC range and apply amplitude */
        state.data[i] = (unsigned int)((value + 1.0) * 0x7fff * state.amplitude / 100);
    }
    
    pthread_mutex_unlock(&mutex);
}

/* Log waveform settings to file */
void write_file(void) {
    char buffer[256];
    int len;
    
    /* Create/open log file */
    fd = open("waveform_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Cannot open log file");
        exit(1);
    }
    
    /* Write current settings to log */
    len = sprintf(buffer, "Initial settings:\n");
    write(fd, buffer, len);
    len = sprintf(buffer, "Waveform type: %d\n", state.type);
    write(fd, buffer, len);
    len = sprintf(buffer, "Frequency: %.2f Hz\n", state.frequency);
    write(fd, buffer, len);
    len = sprintf(buffer, "Amplitude: %d%%\n\n", state.amplitude);
    write(fd, buffer, len);
}

/* Save current settings to default configuration file */
void save_default_settings(void) {
    pthread_mutex_lock(&mutex);
    file = fopen("default.txt", "w");
    if (!file) {
        printf("Error: Could not save default settings\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    
    /* Write current waveform settings to file */
    fprintf(file, "waveform=%s\n",
        state.type == SINE ? "sine" :
        state.type == SQUARE ? "square" :
        state.type == TRIANGLE ? "triangle" :
        state.type == SAWTOOTH ? "sawtooth" :
        state.type == PULSE ? "pulse" :
        state.type == CARDIAC ? "cardiac" : "nothing");
    fprintf(file, "freq=%.2f\n", state.frequency);
    fprintf(file, "amp=%d\n", state.amplitude);
    
    fclose(file);
    pthread_mutex_unlock(&mutex);
}

/* Save current settings to specified file */
void save_settings(const char* filename) {
    pthread_mutex_lock(&mutex);
    file = fopen(filename, "w");
    if (!file) {
        printf("Error: Could not save settings\n");
        pthread_mutex_unlock(&mutex);
        return;
    }
    
    /* Write current configuration to file */
    fprintf(file, "waveform=%s\n",
        state.type == SINE ? "sine" :
        state.type == SQUARE ? "square" :
        state.type == TRIANGLE ? "triangle" :
        state.type == SAWTOOTH ? "sawtooth" :
        state.type == PULSE ? "pulse" :
        state.type == CARDIAC ? "cardiac" : "nothing");
    fprintf(file, "freq=%.2f\n", state.frequency);
    fprintf(file, "amp=%d\n", state.amplitude);
    
    fclose(file);
    printf("Settings saved successfully.");
    pthread_mutex_unlock(&mutex);
}

/* Load settings from specified file */
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
    
    /* Parse configuration file line by line */
    while (fgets(line, sizeof(line), file)) {
        /* Remove trailing newline */
        newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';  /* Split at equals sign */
            value = equals + 1;
            
            /* Remove leading whitespace from value */
            while (*value && isspace(*value)) value++;
            
            /* Update program state based on setting */
            if (strcmp(line, "wave_type") == 0) {
                int type = atoi(value);
                if (type >= SINE && type <= NOTHING) {
                    type = type;
                }
            }
            else if (strcmp(line, "frequency") == 0) {
                float freq = atof(value);
                if (freq >= 1 && freq <= 1000) {
                    state.frequency = freq;
                }
            }
            else if (strcmp(line, "amplitude") == 0) {
                int amp = atoi(value);
                if (amp >= 0 && amp <= 100) {
                    state.amplitude = amp;
                }
            }
        }
    }
    
    fclose(file);
    printf("Settings loaded from %s\n", filename);
    pthread_mutex_unlock(&mutex);
    return 1;
}

/* Handle user input commands */
void* input_thread(void* arg) {
    char input[50];
    char filename[256];
    int len;

    /* Enable thread cancellation */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while(state.running) {
        /* Display appropriate prompt based on mode */
        if (mode == 1) {
            printf("\nEnter command (type 'help' for options): ");
        } else {
            printf("\nEnter 'quit' to exit or 'help' for options: ");
        }
        fflush(stdout);
        
        /* Read user input */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (!state.running) break;
            continue;
        }
        
        /* Remove trailing newline */
        len = strlen(input);
        if (len > 0 && input[len-1] == '\n') {
            input[len-1] = '\0';
            len--;
        }
        
        if (len == 0) continue;

        /* Process commands */
        if(strcmp(input, "help") == 0) {
            printf("Commands:\n");
            if (mode == 1) {
                printf("sine, square, triangle, sawtooth, pulse, cardiac - Change waveform\n");
                printf("freq <value> - Set frequency (1-1000 Hz)\n");
                printf("amp <value> - Set amplitude (0-100%%)\n");
                printf("save <filename> - Save current settings\n");
                printf("load <filename> - Load settings from file\n");
            }
            printf("quit - Exit program\n");
        }
        else if(strcmp(input, "quit") == 0) {
            save_default_settings();
            printf("Saved current settings as default\n");
            state.running = 0;
            break;
        }
        else if(mode == 1) {
            /* Process keyboard mode commands */
            if(strcmp(input, "sine") == 0) {
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
            else if(strcmp(input, "quit") == 0) {
                pthread_mutex_lock(&mutex);
                state.running = 0;
                printf("\n Quitting the program.\n");
                pthread_mutex_unlock(&mutex);
            }
            else if(strncmp(input, "freq ", 5) == 0) {
                freq = atoi(&input[5]);
                if (freq >= 1 && freq <= 1000) {
                    pthread_mutex_lock(&mutex);
                    state.frequency = freq;
                    printf("\nFrequency set to %d Hz\n", freq);
                    pthread_mutex_unlock(&mutex);
                } else {
                    printf("Invalid frequency. Must be 1 - 1000Hz\n");
                }
            }
            else if(strncmp(input, "amp ", 4) == 0) {
                amp =  atoi(&input[4]);
                if (amp >= 0 && amp <= 100) {
                    pthread_mutex_lock(&mutex);
                    state.amplitude = amp;
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
                if (read_config_file(filename)) {
                	printf("Loaded new file settings.");
                    generate_waveform();
                }
            }
        }
        else if(mode == 2) {
            printf("In potentiometer mode. Only 'help' and 'quit' commands are available.\n");
        }
    }
    return NULL;
}

/* Generate and output waveform data in real-time */
void* waveform_thread(void* arg) {
    struct timespec start, now;
    double period_ns;
    int point_index = 0;
    char buffer[256];
    int len;
    float current_freq;
    unsigned short output_value;

    /* Enable thread cancellation */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    /* Initialize waveform and logging */
    generate_waveform();
    write_file();
    
    while(state.running) {
        /* Get current waveform parameters */
        pthread_mutex_lock(&mutex);
        current_freq = state.frequency;
        output_value = (unsigned short)state.data[point_index];
        pthread_mutex_unlock(&mutex);

        /* Calculate timing for current frequency */
        period_ns = (1000000000.0 / (current_freq * POINTS_PER_CYCLE));

        /* Send data to DAC */
        out16(DA_CTLREG, 0x0a23);
        out16(DA_FIFOCLR, 0);
        out16(DA_Data, output_value);

        /* Log waveform point */
        len = sprintf(buffer, "Point %d: Value = 0x%04X\n", 
                     point_index, output_value);
        write(fd, buffer, len);

        /* Update waveform position */
        point_index = (point_index + 1) % POINTS_PER_CYCLE;

        /* Audio feedback on cycle completion */
        if (point_index == 0) {
            printf("\a");
            fflush(stdout);
        }

        /* Maintain precise timing */
        clock_gettime(CLOCK_MONOTONIC, &start);
        do {
            clock_gettime(CLOCK_MONOTONIC, &now);
        } while ((now.tv_sec - start.tv_sec) * 1000000000 + 
                 (now.tv_nsec - start.tv_nsec) < period_ns);
    }

    close(fd);
    return NULL;
}

/* Handle program termination signals */
void signal_handler(int sig) {
    if(sig == SIGINT) {
        pthread_mutex_lock(&mutex);
        printf("\nCleaning up...\n");
        state.running = 0;

        /* Save settings before exit */
        save_default_settings();
        printf("Saved current settings as default\n");

        /* Close log file if open */
        if (fd > 0) {
            char buffer[256];
            int len = sprintf(buffer, "\nProgram terminated by user\n");
            write(fd, buffer, len);
            close(fd);
        }
        pthread_mutex_unlock(&mutex);

        /* Terminate all running threads */
        pthread_cancel(input_tid);
        pthread_cancel(wave_tid);
        if (mode == 2) {
            pthread_cancel(analog_tid);
        }

        /* Clean exit */
        usleep(1000);
        signal(SIGINT, SIG_DFL);
        write(STDOUT_FILENO, "\nExiting Program\n", 18);
        exit(0);
    }
}

/* Display program usage information */
void print_usage(char* program_name) {
    printf("Usage: %s -w wave_type -f frequency -a amplitude\n", program_name);
    printf("  wave_type: sine, square, triangle, sawtooth, pulse, cardiac\n");
    printf("  frequency: 1-1000 Hz\n");
    printf("  amplitude: 0-100%%\n");
    printf("Example: %s -w sine -f 100 -a 50\n", program_name);
}

/* Read and process potentiometer inputs */
void* analog_input_thread(void* arg) {
    int write_len;
    char log_buffer[256];
    double scaled_freq;
    double scaled_amp;

    /* Enable thread cancellation */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    /* Initialize ADC hardware */
    out16(INTERRUPT, 0x60c0);
    out16(TRIGGER, 0x2081);
    out16(AUTOCAL, 0x007f);
    out16(AD_FIFOCLR, 0);
    out16(MUXCHAN, 0x0D00);

    printf("\nStarting analog input monitoring...\n");
    
    count = 0;
    while(state.running) {
        pthread_mutex_lock(&mutex);
        
        /* Configure and read ADC channel */
        chan = ((count & 0x0f) << 4) | (0x0f & count);
        out16(MUXCHAN, 0x0D00 | chan);
        delay(1);
        out16(AD_DATA, 0);
        
        /* Wait for ADC conversion */
        while(!(in16(MUXCHAN) & 0x4000));
        adc_in = in16(AD_DATA);

        /* Process frequency potentiometer */
        if(count == 0) {
            double normalized = (double)adc_in / 65535.0;
            scaled_freq = MIN_FREQ + (pow(normalized, 2) * (MAX_FREQ - MIN_FREQ));
            state.frequency = round(scaled_freq);
            
            write_len = sprintf(log_buffer, "Frequency updated: %.2f Hz\n", state.frequency);
            write(fd, log_buffer, write_len);
        }
        /* Process amplitude potentiometer */
        else if(count == 1) {
            scaled_amp = ((double)adc_in / 65535.0) * 100;
            state.amplitude = (int)scaled_amp;
            
            write_len = sprintf(log_buffer, "Amplitude updated: %d%%\n", state.amplitude);
            write(fd, log_buffer, write_len);
            
            generate_waveform();
        }

        /* Update display */
        printf("\rCurrent: Freq = %.2f Hz, Amp = %d%%    ", state.frequency, state.amplitude);
        fflush(stdout);

        /* Switch between channels */
        count = (count + 1) % 2;
        delay(1);

        pthread_mutex_unlock(&mutex);
    }

    printf("\nAnalog input monitoring stopped\n");
    return NULL;
}

/* Load and parse configuration file */
int read_config_file(const char* filename) {
    char line[256];
    char param_name[32];
    char param_value[32];
    FILE* config_file;
    int success = 0;
    
    /* Validate input */
    if(!filename) {
        printf("Error: No filename provided\n");
        return 0;
    }

    pthread_mutex_lock(&mutex);
    
    /* Open configuration file */
    config_file = fopen(filename, "r");
    if(!config_file) {
        printf("Could not open configuration file: %s\n", filename);
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    
    /* Process each line in config file */
    while(fgets(line, sizeof(line), config_file)) {
        /* Skip empty lines and comments */
        if(line[0] == '\n' || line[0] == '#') {
            continue;
        }
        
        /* Parse parameter-value pairs */
        if(sscanf(line, "%31[^=]=%31s", param_name, param_value) == 2) {
            /* Clean up parameter name */
            char* end = param_name + strlen(param_name) - 1;
            while(end > param_name && isspace(*end)) {
                *end-- = '\0';
            }
            
            /* Update program settings */
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
        generate_waveform();
    }
    
    pthread_mutex_unlock(&mutex);
    return success;
}

/* Display input mode selection menu */
void print_mode_selection(void) {
    printf("\nSelect input mode:\n");
    printf("1. Keyboard input\n");
    printf("2. Potentiometer input\n");
    printf("Enter mode (1 or 2): ");
}

/* Program entry point */
int main(int argc, char* argv[]) {
    char mode_input[10];
    
    /* Initialize program state */
    state.type = SQUARE;
    state.frequency = 1;
    state.amplitude = 100;
    state.running = 1;
    state.data = malloc(POINTS_PER_CYCLE * sizeof(unsigned int));
    pthread_mutex_init(&mutex, NULL);

    /* Load saved settings if available */
    if(access("default.txt", F_OK) != -1) {
        printf("Loading default settings...\n");
        if(load_settings("default.txt")) {
            printf("Default settings loaded successfully\n");
        } else {
            printf("Error loading default settings, using program defaults\n");
        }
    }

    /* Process command line arguments */
    if(argc == 1) {
        printf("\nNo arguments provided. You can start the program with initial settings using:\n");
        print_usage(argv[0]);
        if (read_config_file("default.txt")) {
            printf("\nStarting with current settings...\n");
        }
    } 
    else {
        /* Parse command line options */
        for(i = 1; i < argc; i++) {
            if(strcmp(argv[i], "-h") == 0) {
                print_usage(argv[0]);
                return 0;
            }
            else if(strcmp(argv[i], "-w") == 0) {
                /* Set waveform type */
                if(i + 1 >= argc) {
                    printf("Error: -w requires a wave type\n");
                    print_usage(argv[0]);
                    return 1;
                }
                i++;
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
                /* Set frequency */
                if(i + 1 >= argc) {
                    printf("Error: -f requires a frequency value\n");
                    print_usage(argv[0]);
                    return 1;
                }
                i++;
                state.frequency = atoi(argv[i]);
                if(state.frequency < 1 || state.frequency > 1000) {
                    printf("Frequency must be between 1 and 1000 Hz\n");
                    print_usage(argv[0]);
                    return 1;
                }
            }
            else if(strcmp(argv[i], "-a") == 0) {
                /* Set amplitude */
                if(i + 1 >= argc) {
                    printf("Error: -a requires an amplitude value\n");
                    print_usage(argv[0]);
                    return 1;
                }
                i++;
                state.amplitude = atoi(argv[i]);
                if(state.amplitude < 0 || state.amplitude > 100) {
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
        printf("\nCommand line arguments override default settings.\n");
    }

    /* Display current configuration */
    printf("\nCurrent settings:\n");
    printf("Waveform: %s\n", 
        state.type == SINE ? "sine" :
        state.type == SQUARE ? "square" :
        state.type == TRIANGLE ? "triangle" :
        state.type == SAWTOOTH ? "sawtooth" :
        state.type == PULSE ? "pulse" :
        state.type == CARDIAC ? "cardiac" : "unknown");
    printf("Frequency: %.2f Hz\n", state.frequency);
    printf("Amplitude: %d%%\n", state.amplitude);

    /* Initialize hardware and signal handling */
    init_hardware();
    signal(SIGINT, signal_handler);
    generate_waveform();

    /* Get user input mode */
    printf("\n=== Input Mode Selection ===\n");
    print_mode_selection();
    if (fgets(mode_input, sizeof(mode_input), stdin) != NULL) {
        mode = atoi(mode_input);
        if (mode != 1 && mode != 2) {
            printf("Invalid mode. Defaulting to keyboard input.\n");
            mode = 1;
        }
    } else {
        printf("Error reading input. Defaulting to keyboard input.\n");
        mode = 1;
    }

    /* Create program threads */
    pthread_create(&input_tid, NULL, input_thread, NULL);
    pthread_create(&wave_tid, NULL, waveform_thread, NULL);
    
    if (mode == 2) {
        pthread_create(&analog_tid, NULL, analog_input_thread, NULL);
        printf("\nPotentiometer mode active. Use pots to control frequency and amplitude.\n");
        printf("Type 'quit' to exit program.\n");
    }

    /* Wait for threads to complete */
    pthread_join(input_tid, NULL);
    pthread_join(wave_tid, NULL);
    if (mode == 2) {
        pthread_join(analog_tid, NULL);
    }

    /* Cleanup and exit */
    pthread_mutex_destroy(&mutex);
    free(state.data);
    pci_detach_device(hdl);
    printf("End of Program\n");
    
    return 0;
}

