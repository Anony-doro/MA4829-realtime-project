
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <hw/pci.h>
#include <hw/inout.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <math.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>

#define	INTERRUPT	iobase[1] + 0		// Badr1 + 0 : also ADC register
#define	MUXCHAN		iobase[1] + 2		// Badr1 + 2
#define	TRIGGER		iobase[1] + 4		// Badr1 + 4
#define	AUTOCAL		iobase[1] + 6		// Badr1 + 6
#define DA_CTLREG	iobase[1] + 8		// Badr1 + 8

#define	 AD_DATA	iobase[2] + 0		// Badr2 + 0
#define	 AD_FIFOCLR	iobase[2] + 2		// Badr2 + 2

#define	TIMER0		iobase[3] + 0		// Badr3 + 0
#define	TIMER1		iobase[3] + 1		// Badr3 + 1
#define	TIMER2		iobase[3] + 2		// Badr3 + 2
#define	COUNTCTL	iobase[3] + 3		// Badr3 + 3
#define	DIO_PORTA	iobase[3] + 4		// Badr3 + 4
#define	DIO_PORTB	iobase[3] + 5		// Badr3 + 5
#define	DIO_PORTC	iobase[3] + 6		// Badr3 + 6
#define	DIO_CTLREG	iobase[3] + 7		// Badr3 + 7
#define	PACER1		iobase[3] + 8		// Badr3 + 8
#define	PACER2		iobase[3] + 9		// Badr3 + 9
#define	PACER3		iobase[3] + a		// Badr3 + a
#define	PACERCTL	iobase[3] + b		// Badr3 + b

#define DA_Data		iobase[4] + 0		// Badr4 + 0
#define	DA_FIFOCLR	iobase[4] + 2		// Badr4 + 2

#define NUM_POINTS 100
#define PI 3.14159

#define SINE 0
#define SQUARE 1
#define TRIANGLE 2
#define SAWTOOTH 3
const char* wave_names[] = { "Sine", "Square", "Triangle", "Sawtooth"};

#define SETTING_FILE "settings.txt"
int empty_file = 0;

// Global Variables
volatile sig_atomic_t stop_flag = 0;
uintptr_t iobase[6];
int badr[5];
void *hdl;


// Setting min and max values for amplitude, frequency and mean
// to ensure that the values are within the range of the DAC
const float AMPLITUDE_MIN = 0.1;   
const float AMPLITUDE_MAX = 2.5;
const float FREQUENCY_MIN = 0.1;
const float FREQUENCY_MAX = 10.0;
const float MEAN_MIN = 0.1;
const float MEAN_MAX = 2.5;

// Default values for waveform type, amplitude, frequency and mean
const int DEFAULT_WAVE_TYPE = SINE;
const float DEFAULT_AMPLITUDE = 2.5;
const float DEFAULT_FREQUENCY = 10.0;
const float DEFAULT_MEAN = 2.5;

// Global variables for waveform type, amplitude, frequency and mean, changed by the user
volatile int wave_type = SINE;
volatile float amplitude = 2.5;
volatile float frequency = 10.0;
volatile float mean = 2.5;

volatile int control_mode = 0; // 0 = keyboard, 1 = potentiometer
volatile int change_waveform = 0;

// Mutex for controlling access to shared variables
pthread_mutex_t control_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread initialization
pthread_t wave_thread, pot_thread, kbd_thread, toggle_thread;

// Function prototypes
void sigint_handler(int);
void init_pci_das1602();
void write_to_dac(unsigned short);
void* waveform_thread(void*);
void* potentiometer_thread(void*);
void* kbd_control(void*);
void* toggle_switch_thread(void*);



void read_settings(char* filename, volatile float* amplitude, volatile float* frequency, volatile float* mean) {
    ///* Read settings from a file and update the global variables accordingly.
    // If the file is empty or the values are invalid, use default values.
	char *waveform = "sine";
    char buffer[256];
    int counter = 0;
    char* newline;

    FILE *file = fopen(filename, "r");

    // Check if the file opened successfully
    if (file == NULL) {
        perror("[ERROR] Error opening file");
        empty_file = 1;
        return;
    }

    // Read the file line by line and parse the values
    while (fgets(buffer, sizeof(buffer), file)) {	
        if (counter == 0) {
            waveform = strdup(buffer);
            newline = strchr(waveform, '\n');
            if (newline) {
            	*newline = '\0';
            }
            strlwr(waveform);
           
            if (!strcmp(waveform, "sine")) {
                wave_type = SINE;
            }
            else if (!strcmp(waveform, "square")) {
                wave_type = SQUARE;
            }
            else if (!strcmp(waveform, "triangle")) {
                wave_type = TRIANGLE;
            }
            else if (!strcmp(waveform, "sawtooth")) {
                wave_type = SAWTOOTH;
            }
            else {
            	printf("[ERROR] The waveform saved is invalid. Continuing with default values\n");
            	wave_type = DEFAULT_WAVE_TYPE;
            	empty_file = 1;
            }
        }
        
        // Convert the strings to a float and check if it's within the valid range
        // If not, set it to the default value
        else if (counter == 1) {
            *amplitude = strtof(buffer, NULL);
            if (*amplitude < AMPLITUDE_MIN || *amplitude > AMPLITUDE_MAX) {
                printf("[ERROR] The amplitude saved is invalid. Continuing with default values\n");
                *amplitude = DEFAULT_AMPLITUDE;
                empty_file = 1;
            }
        }
        
        else if (counter == 2) {
            *frequency = strtof(buffer, NULL);
            if (*frequency < FREQUENCY_MIN || *frequency > FREQUENCY_MAX) {
                printf("[ERROR] The frequency saved is invalid. Continuing with default values\n");
                *frequency = DEFAULT_FREQUENCY;
                empty_file = 1;
            }
        }
        
        else if (counter == 3) {
            *mean = strtof(buffer, NULL);
            if (*mean < MEAN_MIN || *mean > MEAN_MAX || amplitude > mean) {
                printf("[ERROR] The mean saved is invalid. Continuing with default values\n");
                *mean = DEFAULT_MEAN;
                empty_file = 1;
            }
        }
        counter++;
    }
    fclose(file);
    
    // Error handling for empty file
    if (counter == 0){
    	printf("[ERROR] Empty settings file. Continuing with default values\n");
    	empty_file = 1;
    }
    
	if (empty_file) {
		printf("[INFO] Using Default Values:\n");
		printf("[INFO] Waveform: %s\n", wave_names[wave_type]);
		printf("[INFO] Frequency: %.2f\n", frequency);
		printf("[INFO] Amplitude: %.2f\n", amplitude);
		printf("[INFO] Mean: %.2f\n\n", mean);
	}
	else {
		printf("[INFO] Using Saved Values:\n");
		printf("[INFO] Waveform: %s\n", wave_names[wave_type]);
	    	printf("[INFO] Frequency: %.2f\n", frequency);
		printf("[INFO] Amplitude: %.2f\n", amplitude);
		printf("[INFO] Mean: %.2f\n\n", mean);
	}
    
    delay(500);
}

void save_settings(char* filename, float amplitude, float frequency, float mean) {
    ///* Save the current settings to a file. The file will be overwritten if it already exists. */
    // The settings are saved in the following order: waveform, amplitude, frequency, mean

    char *waveform = strdup(wave_names[wave_type]);
    
    FILE *file = fopen(filename, "w+");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
        
    strlwr(waveform);
    
    fprintf(file, "%s\n", waveform);
    fprintf(file, "%f\n", amplitude);
    fprintf(file, "%f\n", frequency);
	fprintf(file, "%f\n", mean);
    fclose(file);
    
    printf("\033[2J\033[H");
    printf("[INFO] Saving Values:\n");
    printf("[INFO] Waveform: %s\n", waveform);
    printf("[INFO] Frequency: %.2f\n", frequency);
    printf("[INFO] Amplitude: %.2f\n", amplitude);
    printf("[INFO] Mean: %.2f\n", mean);
}

void sigint_handler(int sig) {
    ///* Signal handler for SIGINT (Ctrl+C). This function is called when the user presses Ctrl+C or when they toggle the switch. */
	char user_input;
	
    stop_flag = 1;
    printf("\033[2J\033[H");
    printf("\n[INFO] Would you like to save the values? (y/n)\n");
	while (1) {
		scanf(" %c", &user_input);
		if (user_input == 'y' || user_input == 'Y') {
			printf("[INFO] Saving settings to file\n");
			save_settings(SETTING_FILE, amplitude, frequency, mean);
			break;
		}
		else if (user_input == 'n' || user_input == 'N') {
			printf("[INFO] Exiting without saving values\n");
			break;
		}
		else {
			printf("[ERROR] Invalid Input\n");
		}
	}
	delay(500);
}

void write_to_dac(unsigned short val) {
    ///* Function to write the value to the DAC. */
    out16(DA_CTLREG, 0x0a23);
    out16(DA_FIFOCLR, 0);
    out16(DA_Data, val);
    out16(DA_CTLREG, 0x0a43);
    out16(DA_FIFOCLR, 0);
    out16(DA_Data, val);
}

void* waveform_thread(void* arg) {
    ///* Thread function to generate the waveform. This function runs in an infinite loop until the stop_flag is set. */
    int i, type, delay_us;
    float delta, voltage, amp, offset, freq;
    unsigned short dac_value;

    delta = (2.0 * PI) / NUM_POINTS;

    while (!stop_flag) {
        amp = amplitude;
        offset = mean;
        freq = frequency;
        type = wave_type;

        delay_us = (int)(1.0 / freq / NUM_POINTS * 1e6);

        // Different waveform types and their corresponding calculations
        for (i = 0; i < NUM_POINTS && !stop_flag; i++) {
            switch (type) {
                case 0: voltage = offset + amp * sin(delta * i); break;
                case 1: voltage = (i < NUM_POINTS / 2) ? (offset + amp) : (offset - amp); break;
                case 2: voltage = (i < NUM_POINTS / 2) ? (offset - amp + (2 * amp * i) / (NUM_POINTS / 2)) : (offset + amp - (2 * amp * (i - NUM_POINTS / 2)) / (NUM_POINTS / 2)); break;
                case 3: voltage = offset - amp + (2 * amp * i) / (NUM_POINTS - 1); break;
                default: voltage = offset; break;
            }
            if (voltage < 0.0) voltage = 0.0;
            if (voltage > 5.0) voltage = 5.0;

            dac_value = (unsigned short)((voltage / 5.0) * 0xFFFF);
            write_to_dac(dac_value);
            
            if (change_waveform) {
            	change_waveform = 0;
            	break;
            }

            usleep(delay_us);
        }
    }
    return NULL;
}

void* potentiometer_thread(void* arg) {
    ///* Thread function to read the potentiometer values and adjust the waveform parameters (amplitude, frequency) accordingly. */
    unsigned short raw[2];
    int local_mode;
    int count;
    unsigned short chan;

    while (!stop_flag) {

        // Use of mutexes to ensure thread safety when accessing shared variables
        pthread_mutex_lock(&control_mutex);
        local_mode = control_mode;
        pthread_mutex_unlock(&control_mutex);
        
        if (local_mode == 1) {
        	mean = 2.5;

	        out16(INTERRUPT, 0x60c0);
	        out16(TRIGGER, 0x2081);
	        out16(AUTOCAL, 0x007f);
	        out16(AD_FIFOCLR, 0);
            
            // Set the mux channel to read from the potentiometer
	        for (count = 0; count < 2; count++) {
	            chan = ((count & 0x0f) << 4) | (0x0f & count);
	            out16(MUXCHAN, 0x0D00 | chan);   // Select channel
	            usleep(1000);                    // Let mux settle
	            out16(AD_DATA, 0);               // Start ADC conversion
	
	            // Wait for conversion to complete
	            while (!(in16(MUXCHAN) & 0x4000));
	            raw[count] = in16(AD_DATA);
	        }
	
	        // Amplitude control using channel 0
	        amplitude = ((float)raw[0] / 65535.0f) * AMPLITUDE_MAX;
	        if (amplitude < 0.1) {
	        	amplitude = 0.1;
	        }
	
	        // Frequency control using channel 1
	        frequency = 1.0f + ((float)raw[1] / 65535.0f) * 9.0f;
	
	        printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
	        
	        fflush(stdout);
        }
	
	        usleep(10000); // Delay to prevent excessive polling
	    }

    return NULL;
}

void* kbd_control(void* arg) {
    ///* Thread function to handle keyboard input for controlling the waveform parameters (amplitude, frequency, mean, waveform type). */
    char c;
    int local_mode;
    struct termios oldt, newt;
    fd_set readfds;
    struct timeval tv;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (control_mode == 0) {
    	printf("\033[2J\033[H");
        printf("[Keyboard Control Mode]\n");
        printf(" Control Instructions:\n");
        printf("  - '1': Sine Wave\n");
        printf("  - '2': Square Wave\n");
        printf("  - '3': Triangle Wave\n");
        printf("  - '4': Sawtooth Wave\n");
        printf("\n");
        printf("  - Arrow UP/DOWN: Increase / Decrease Frequency (1.0 - 10.0 Hz)\n");
        printf("  - Arrow LEFT/RIGHT: Increase / Decrease Amplitude (0.1 - 2.5 V)\n");
        printf("\n");
        printf(" Press 'm' to switch to Hardware Control Mode\n");
        printf(" Press 'e' to exit the program\n");
        printf(" Or Toggle Switch 1 (Kill Switch) to shut down immediately\n");
        printf("-----------------------------------------------------------\n");
    } else {
    	printf("\033[2J\033[H");
        printf("[Hardware Control Mode]\n");
        printf("-----------------------------------------------------------\n");
        printf(" Control Instructions:\n");
        printf("  - A/D Channel 0 Knob: Adjust Amplitude\n");
        printf("  - A/D Channel 1 Knob: Adjust Frequency\n");
        printf("\n");
        printf(" Toggle Switch Functions (set only ONE to '1' at a time):\n");
        printf("   Switch 1: Exit program (Kill Switch)\n");
        printf("   Switch 2: Select Square Wave\n");
        printf("   Switch 3: Select Triangle Wave\n");
        printf("   Switch 4: Select Sawtooth Wave\n");
        printf("\n");
        printf(" Press 'm' to switch back to Keyboard Control Mode\n");
        printf("-----------------------------------------------------------\n");

    }
	printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
    fflush(stdout);
    while (!stop_flag) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 0.1 sec

        if (select(STDIN_FILENO+1, &readfds, NULL, NULL, &tv) > 0) {
            c = getchar();
            if (c == '\033') {
                getchar();
                switch(getchar()) {
                    case 'A':
                        pthread_mutex_lock(&control_mutex);
                        if (control_mode == 0) {
                            if (frequency < 10.0f) frequency += 0.1f;
                            else frequency = 10.0f;
                            printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
                            fflush(stdout);
                        }
                        pthread_mutex_unlock(&control_mutex);
                        break;
                    case 'B':
                        pthread_mutex_lock(&control_mutex);
                        if (control_mode == 0) {
                            if (frequency > 1.0f) frequency -= 0.1f;
                            else frequency = 1.0f;
                            printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
                            fflush(stdout);
                        }
                        pthread_mutex_unlock(&control_mutex);
                        break;
                    case 'C':
                        pthread_mutex_lock(&control_mutex);
                        if (control_mode == 0) {
                            if (amplitude < AMPLITUDE_MAX) amplitude += 0.1f;
                            else amplitude = AMPLITUDE_MAX;
                            
                            if (amplitude > mean) {
                            	amplitude = mean;
                            	printf("\r[ERROR] Waveform exceeds max voltage range. Adjusting values: Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V", frequency, amplitude, mean);
                            	fflush(stdout);
                            }
                            else {
                            
	                            printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
	                            fflush(stdout);
                            }
                        }
                        pthread_mutex_unlock(&control_mutex);
                        break;
                    case 'D':
                        pthread_mutex_lock(&control_mutex);
                        if (control_mode == 0) {
                            if (amplitude > 0.1f) amplitude -= 0.1f;
                            else amplitude = 0.1f;
                            
                            if (amplitude > mean) {
                            	amplitude = mean;
                            	printf("\r[ERROR] Waveform exceeds max voltage range. Adjusting values: Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V", frequency, amplitude, mean);
                            	fflush(stdout);
                            }
                            else {
	                            printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
	                            fflush(stdout);
                            }
                        }
                        pthread_mutex_unlock(&control_mutex);
                        break;
                }
            } else {
                pthread_mutex_lock(&control_mutex);
                local_mode = control_mode;
                pthread_mutex_unlock(&control_mutex);

                if (c == 'm') {
                    pthread_mutex_lock(&control_mutex);
                    control_mode = (control_mode == 0) ? 1 : 0;
                    if (control_mode == 0) {
                        printf("\033[2J\033[H");
                        printf("\n[Switched to Keyboard Control Mode]\n");
                        printf("-----------------------------------------------------------\n");
                        printf(" Control Instructions:\n");
                        printf("  - '1': Sine Wave\n");
                        printf("  - '2': Square Wave\n");
                        printf("  - '3': Triangle Wave\n");
                        printf("  - '4': Sawtooth Wave\n");
                        printf("\n");
                        printf("  - Arrow UP/DOWN: Increase / Decrease Frequency (1.0 - 10.0 Hz)\n");
                        printf("  - Arrow LEFT/RIGHT: Increase / Decrease Amplitude (0.1 - 2.5 V)\n");
                        printf("\n");
                        printf(" Press 'm' to switch to Hardware Control Mode\n");
                        printf(" Press 'e' to exit the program\n");
                        printf(" Or Toggle Switch 1 (Kill Switch) to shut down immediately\n");
                        printf("-----------------------------------------------------------\n");

                    } else {
                        printf("\033[2J\033[H");
                        printf("\n[Switched to Hardware Control Mode]\n");
                        printf("-----------------------------------------------------------\n");
                        printf(" Control Instructions:\n");
                        printf("  - A/D Channel 0 Knob: Adjust Amplitude\n");
                        printf("  - A/D Channel 1 Knob: Adjust Frequency\n");
                        printf("\n");
                        printf(" Toggle Switch Functions (set only ONE to '1' at a time):\n");
                        printf("   Switch 1: Exit program (Kill Switch)\n");
                        printf("   Switch 2: Select Square Wave\n");
                        printf("   Switch 3: Select Triangle Wave\n");
                        printf("   Switch 4: Select Sawtooth Wave\n");
                        printf("\n");
                        printf(" Press 'm' to switch back to Keyboard Control Mode\n");
                        printf("-----------------------------------------------------------\n");

                    }
                    pthread_mutex_unlock(&control_mutex);
                }

                if (local_mode == 0) {
                    if (c == 'e') stop_flag = 1;
                    if (c >= '1' && c <= '4') {
                        wave_type = c - '1';
                        change_waveform = 1;
                        printf("\n[INFO] Waveform type set to %s \n", wave_names[wave_type]);
                        fflush(stdout);
                    }
                    if (c == 'k') {
                        pthread_mutex_lock(&control_mutex);
                        if (mean < MEAN_MAX) mean += 0.1f;
                        else mean = MEAN_MAX;
                        
						if (amplitude > mean) {
							mean = amplitude;
                        	printf("\r[ERROR] Waveform exceeds max voltage range. Adjusting values: Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V", frequency, amplitude, mean);
                        	fflush(stdout);
                        }
                        else {
		                    printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
		                    fflush(stdout);
                        }
                        pthread_mutex_unlock(&control_mutex);
                    }
                    if (c == 'j') {
                        pthread_mutex_lock(&control_mutex);
                        if (mean > 0.0f) mean -= 0.1f;
                        else mean = 0.0f;
                        
						if (amplitude > mean) {
							mean = amplitude;
                        	printf("\r[ERROR] Waveform exceeds max voltage range. Adjusting values: Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V", frequency, amplitude, mean);
                        	fflush(stdout);
                        }
                        else {
		                    printf("\r[INFO] Frequency: %.2f Hz | Amplitude: %.2f V | Mean: %.2f V                                                       ", frequency, amplitude, mean);
		                    fflush(stdout);
                        }
                        pthread_mutex_unlock(&control_mutex);
                    }
                }
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return NULL;
}


void* toggle_switch_thread(void* arg) {
    ///* Thread function to handle the toggle switch input for controlling the waveform type as well as stopping the program safely. */
    unsigned char toggle_switch_value, last_switch_value = 0x00;
    int local_mode;

    while (!stop_flag) {
        out8(DIO_CTLREG, 0x90);
        toggle_switch_value = in8(DIO_PORTA);

        // Only act if switch state changed
        if (toggle_switch_value != last_switch_value) {
            last_switch_value = toggle_switch_value;

            if (toggle_switch_value == 0xFF || toggle_switch_value == 0xF8) {
            	printf("\033[2J\033[H");
                printf("\n[Kill Switch] Activated. Shutting down...\n");
                raise(SIGINT);
            }

            pthread_mutex_lock(&control_mutex);
            local_mode = control_mode;
            pthread_mutex_unlock(&control_mutex);

            if (local_mode == 1) {  // Only allow waveform switching in pot mode
                switch (toggle_switch_value) {
                    case 0xf4:
                        printf("\n[INFO] Switching to SQUARE WAVE\n");
                        wave_type = SQUARE;
                        change_waveform = 1;
                        break;
                    case 0xf2:
                        printf("\n[INFO] Switching to TRIANGLE WAVE\n");
                        wave_type = TRIANGLE;
                        change_waveform = 1;
                        break;
                    case 0xf1:
                        printf("\n[INFO] Switching to SAWTOOTH WAVE\n");
                        wave_type = SAWTOOTH;
                        change_waveform = 1;
                        break;
                    case 0xf0:
                        printf("\n[INFO] Switching to SINE WAVE\n");
                        wave_type = SINE;
                        change_waveform = 1;
                        break;
                }
            }
        }

        usleep(10000);
    }
    return NULL;
}


void init_pci_das1602() {
    ///* Function to initialize the PCI-DAS1602 device. */
    struct pci_dev_info info;
    int i;

    memset(&info, 0, sizeof(info));
    if (pci_attach(0) < 0) { perror("pci_attach"); exit(EXIT_FAILURE); }

    info.VendorId = 0x1307;
    info.DeviceId = 0x01;

    if ((hdl = pci_attach_device(0, PCI_SHARE | PCI_INIT_ALL, 0, &info)) == 0) {
        perror("pci_attach_device");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < 5; i++) {
        badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
        iobase[i] = mmap_device_io(0x0f, badr[i]);
    }

    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("ThreadCtl");
        exit(EXIT_FAILURE);
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall");
        exit(EXIT_FAILURE);
    }
}


int main(int argc, char* argv[]) {
    ///* Main function to initialize the PCI-DAS1602 device and start the threads for waveform generation, Hardware control, keyboard control, and toggle switch handling. */
    char mode;
    struct sigaction sa;
    char user_input;
    printf("\033[2J\033[H"); // Clear terminal
    printf("===========================================================\n");
    printf("            Welcome to the PCI-DAS1602 Controller!         \n");
    printf("===========================================================\n");
    printf(" This program allows you to generate and control waveforms\n");
    printf(" using the PCI-DAS1602 Data Acquisition Card.\n");
    printf("\n");
    printf(" Features:\n");
    printf("  - Output waveforms via DAC (Sine, Square, Triangle, Sawtooth)\n");
    printf("  - Real-time control of frequency and amplitude\n");
    printf("  - Multiple control modes:\n");
    printf("     [k] Keyboard Mode        Use arrow keys & number keys\n");
    printf("     [h] Hardware Mode   Adjust with analog input + toggle switch\n");
    printf("  - Dynamic mode switching with 'm'\n");
    printf("\n");
    printf("Kill Switch: Use the first toggle switch to exit safely\n");
    printf("===========================================================\n\n");

    
    do {
        printf("Select Control Mode: (k = Keyboard, h = Hardware): ");
        scanf(" %c", &mode);
        mode = tolower(mode);  // convert to lowercase for case-insensitive check
    
        if (mode != 'k' && mode != 'h') {
            printf("[ERROR] Invalid option. Please enter 'k' or 'h'.\n");
        }
    } while (mode != 'k' && mode != 'h');
    
    control_mode = (mode == 'h') ? 1 : 0;

    if (control_mode != 1) {
	    if (argc == 5) {
		    char *waveform;
		    waveform = argv[1];
		    if (strcmp(waveform, "sine") && strcmp(waveform, "square") && strcmp(waveform, "triangle") && strcmp(waveform, "sawtooth")) {
		        printf("[ERROR] The waveform you selected is invalid\n");
		        wave_type = DEFAULT_WAVE_TYPE;
		        empty_file = 1;
		    }
		    if (!strcmp(waveform, "sine")) {
		        wave_type = SINE;
		    }
		    else if (!strcmp(waveform, "square")) {
		        wave_type = SQUARE;
		    }
		    else if (!strcmp(waveform, "triangle")) {
		        wave_type = TRIANGLE;
		    }
		    else if (!strcmp(waveform, "sawtooth")) {
		        wave_type = SAWTOOTH;
		    }
		    frequency = strtof(argv[2], NULL);
		    if (frequency < FREQUENCY_MIN || frequency > FREQUENCY_MAX) {
		        printf("[ERROR] The frequency you selected is invalid\n");
		        frequency = DEFAULT_FREQUENCY;
		        empty_file = 1;
		    }
		    amplitude = strtof(argv[3], NULL);
		    if (amplitude < AMPLITUDE_MIN || amplitude > AMPLITUDE_MAX) {
		        printf("[ERROR] The amplitude you selected is invalid\n");
		        amplitude = DEFAULT_AMPLITUDE;
		        empty_file = 1;
		    }
		    mean = strtof(argv[4], NULL);
	        if (mean < MEAN_MIN || mean > MEAN_MAX || amplitude > mean) {
	            printf("[ERROR] The mean saved is invalid\n");
	            mean = DEFAULT_MEAN;
	            empty_file = 1;
	        }
	    }
	    else {
	    		printf("[INFO] Insufficient arguments provided.\n");
	    		printf("[INFO] Would you like to load the values from the saved file? (y/n)\n");
	    		while (1){
		    		scanf(" %c", &user_input);
		    		if (user_input == 'y' || user_input == 'Y') {
		    			read_settings(SETTING_FILE, &amplitude, &frequency, &mean);
		    			break;
		    		}
		    		else if (user_input == 'n' || user_input == 'N') {
		    			printf("[INFO] Continuing with default values\n");
		    			empty_file = 1;
		    			break;
		    		}
		    		else {
		    			printf("[ERROR] Invalid Input\n");
		    		}
	    		}
	    }
	    delay(500);
    }

    printf("[INFO] Initializing PCI-DAS1602 device...\n");

    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    init_pci_das1602();

    printf("[INFO] Device initialized successfully.\n");
    printf("[INFO] Starting waveform, potentiometer, keyboard and kill switch threads...\n");

    pthread_create(&wave_thread, NULL, waveform_thread, NULL);
    pthread_create(&pot_thread, NULL, potentiometer_thread, NULL);
    pthread_create(&kbd_thread, NULL, kbd_control, NULL);
    pthread_create(&toggle_thread, NULL, toggle_switch_thread, NULL); 	

    pthread_join(wave_thread, NULL);
    pthread_join(pot_thread, NULL);
    pthread_join(kbd_thread, NULL);
    pthread_join(toggle_thread, NULL);

    printf("\n[INFO] All threads closed. Cleaning up resources...\n");

    pci_detach_device(hdl);

    printf("==== Program exited cleanly. Goodbye! ====\n");
    return 0;
}
