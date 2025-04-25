
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <hw/pci.h>
#include <hw/inout.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <termios.h>

// Hardware registers definition											
#define	INTERRUPT		iobase[1] + 0				// Badr1 + 0 : also ADC register
#define	MUXCHAN			iobase[1] + 2				// Badr1 + 2
#define	TRIGGER			iobase[1] + 4				// Badr1 + 4
#define	AUTOCAL			iobase[1] + 6				// Badr1 + 6
#define DA_CTLREG		iobase[1] + 8				// Badr1 + 8

#define	AD_DATA			iobase[2] + 0				// Badr2 + 0
#define	AD_FIFOCLR		iobase[2] + 2				// Badr2 + 2

#define	TIMER0			iobase[3] + 0				// Badr3 + 0
#define	TIMER1			iobase[3] + 1				// Badr3 + 1
#define	TIMER2			iobase[3] + 2				// Badr3 + 2
#define	COUNTCTL		iobase[3] + 3				// Badr3 + 3
#define	DIO_PORTA		iobase[3] + 4				// Badr3 + 4
#define	DIO_PORTB		iobase[3] + 5				// Badr3 + 5
#define	DIO_PORTC		iobase[3] + 6				// Badr3 + 6
#define	DIO_CTLREG		iobase[3] + 7				// Badr3 + 7
#define	PACER1			iobase[3] + 8				// Badr3 + 8
#define	PACER2			iobase[3] + 9				// Badr3 + 9
#define	PACER3			iobase[3] + a				// Badr3 + a
#define	PACERCTL		iobase[3] + b				// Badr3 + b

#define DA_Data			iobase[4] + 0				// Badr4 + 0
#define	DA_FIFOCLR		iobase[4] + 2				// Badr4 + 2

#define PI              3.14159265358979323846
#define POINTS_PER_CYCLE 100
#define MAX_FREQ 1000
#define MIN_FREQ 1
#define FREQ_MULT 2
#define FREQ_STEP 1
#define AMP_STEP 1
#define PULSE_WIDTH_RATIO 0.1

														// PCI 2.2 assigns 6 IO base addresses

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// Global variables
struct {
    enum WaveformType type;
    float frequency;
    int amplitude;
    int running;
    unsigned int* data;
} state;

// Waveform Types
enum WaveformType {
    SINE, // used for switch statements in waveformCheck()
    SQUARE,
    TRIANGULAR,
    SAWTOOTH,
	PULSE,
	CARDIAC,
	NOTHING
};

// Set-up for PCI Device
struct pci_dev_info info;
void *hdl;

uintptr_t iobase[6];
int badr[5];	
uint16_t adc_in;

unsigned int i, count; // global type declaration for counter of loops

// for UI
bool waveformInvalid; // Invalid input checker for waveform type selection
bool freqInvalid; // Invalid input checker for frequency selection
bool amplitudeInvalid; // Invalid input checker for amplitude selection

int scanf_result;
unsigned short user_option;
char* temp_waveform; // takes in user input for waveform type as string
int temp_freq;
int freq = 100;

// A/D Input
unsigned short chan; // channel for A/D inputs
int temp_dio;		// record current dio, to compare with previous dio
uintptr_t dio_in = 0xff;
int temp_amp;				// record current potentiometer reading, to compare with previous amp
int amp = 0xffff; // initialise amplitude as 1

// Wave Generation
unsigned short waveform; // stores waveform type (SINE, SQUARE, TRIANGULAR, SAWTOOTH) retrieved from enum WaveformType
unsigned int* data; 	// initilalise an int array pointer
float delta, dummy;
float temp;
int freq_points;
int signalInterrupt = 0;
int abort_signal = 0;

// file IO
FILE *file;				// temp file writer to store generated data
FILE *wave_file;		// wave file reader to generate wave 
char line[256];			// act as buffer for file IO

// threading	
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
unsigned short condition = 0;  	// 0: initial, 1: generating wave, 2: generating data

// Functions Declaration
void interrupt();
void reset();
void parse_arguments(int argc, char *argv[]);
void display_usage();
void update_settings();
void waveformCheck();
void freqCheck();
void toggle();
void potentiometer();
void create_data_arr();

// Thread Functions
void* userInput();
void* dataGenerate();
int waveGenerate();


void interrupt() 
{
	signalInterrupt = 1;
}

void reset()
{
										// Unreachable code
										// Reset DAC to 5v
	out16(DA_CTLREG, (short) 0x0a23);	
	out16(DA_FIFOCLR, (short) 0);			
	out16(DA_Data, 0x7fff);				// Mid range - Unipolar
	
	out16(DA_CTLREG, (short) 0x0a43);	
	out16(DA_FIFOCLR, (short) 0);			
	out16(DA_Data, 0x7fff);				

	printf("\n\nExit Demo Program\n");
	pci_detach_device(hdl);
		
	free(data);
	if (wave_file != NULL)
    {
    	fclose(wave_file);
		printf("Sucessfully written to file.\n");
	}
}

void parse_arguments(int argc, char *argv[])
{
    temp_waveform = argv[1];
    temp_freq = atoi(argv[2]);
    
    waveformCheck(argc, argv);
    freqCheck(argc, argv);
}

void display_usage() 
{
    printf("Usage: program_name [waveform_type] [frequency]\n");
    printf("Supported waveform types: sine, square, triangular, sawtooth\n");
    printf("Frequency range: 1 - 1000\n");
}

void update_settings() 
{
	printf("\nKeyboard input option\n");
	printf("Key in the following number if you want to update the respective field\n");
	printf("1: Waveform\n");
	printf("2: Frequency\n");
	user_option = 0;

	do 								// make sure user option is correct
	{
		printf("Your input (1 / 2): \n");
		scanf("%d", &user_option);
		while (getchar() != '\n');	// used to clear buffer, handle error when input not numeric
	}
	while (user_option != 1 && user_option != 2);

	if (user_option == 1)
	{
		printf("\nPlease select from these 4 options: sine / square / triangular / sawtooth\n");
		printf("New waveform: \n");
		scanf("%s", temp_waveform);
        waveformCheck();
	}
	if (user_option == 2)
	{
		printf("New frequency (1 - 1000): \n");
		scanf_result = scanf("%d", &temp_freq);
		while (getchar() != '\n'); 	// used to clear buffer, handle error when input not numeric
		if (scanf_result == 0) 		// if result == 0 means scanf face error, hence change temp_freq = -1
		{
			temp_freq = -1;			// help freq() detect incorrect input
		}
		freqCheck();
	}
	
	printf("Success!\n");
	printf("New setting - Waveform: %s, Frequency: %i\n", temp_waveform, freq);
	printf("\nPlease toggle the switches again to trigger keyboard input\n");

	condition = 2; // data generation
	pthread_cond_signal( &cond );      
	pthread_mutex_unlock( &mutex );
}

void waveformCheck()
{
    do
    {
        waveformInvalid = false; // Initialises as false

        if (strcmp(str_to_upper(temp_waveform), "SINE") == 0) // changes all characters of user input to uppercase for better UX
        {
            waveform = SINE;       // 0
        }
        else if (strcmp(str_to_upper(temp_waveform), "SQUARE") == 0) 
        {
            waveform = SQUARE;     // 1
        } 
        else if (strcmp(str_to_upper(temp_waveform), "TRIANGULAR") == 0)
        {
            waveform = TRIANGULAR; // 2
        } 
        else if (strcmp(str_to_upper(temp_waveform), "SAWTOOTH") == 0)
        {
            waveform = SAWTOOTH;   // 3
        }   // if any of these 4 if statements is performed, waveformInvalid remains unchanged as false & this do-while loop terminates
        else
        {
            waveformInvalid = true;
            printf("\nInvalid waveform.\n");
            printf("Please select from these 4 options: Sine / Square / Triangular / Sawtooth\n");
            printf("New waveform: \n");
            scanf("%s", temp_waveform); // takes in new user input for waveform type
        }
    }
    while (waveformInvalid); // loop continues until any of the 4 if statements is performed
}

void freqCheck()
{
    do
    {
        freqInvalid = false;                   // Initialises as false
        if (temp_freq < 1 || temp_freq > 1000) // user input frequency is out of range
        {
            freqInvalid = true;
            printf("Invalid frequency, please input a value in range of (1 - 1000): \n");
            printf("New frequency: \n");
            scanf("%d", &temp_freq);
            while (getchar() != '\n'); // used to clear buffer, handle error when input not numeric
        }
        else
        {
            freq = temp_freq;
            create_data_arr();
        }
    }
    while (freqInvalid); // loop continues until frequency is in range
}

void toggle()
{	
								//*****************************************************************************
								//Digital Port Functions
								//*****************************************************************************	
	out8(DIO_CTLREG, 0x90);		// Port A : Input,  Port B : Output,  Port C (upper | lower) : Output | Output			

	temp_dio = in8(DIO_PORTA); 	// Read Port A
																						
	out8(DIO_PORTB, temp_dio);	// output Port A value -> write to Port B
}

void potentiometer()
{
								//******************************************************************************
								// ADC Port Functions
								//******************************************************************************
								// Initialise Board								
	out16(INTERRUPT,0x60c0);	// sets interrupts	 - Clears			
	out16(TRIGGER,0x2081);		// sets trigger control: 10MHz, clear, Burst off,SW trig. default:20a0
	out16(AUTOCAL,0x007f);		// sets automatic calibration : default

	out16(AD_FIFOCLR,0); 		// clear ADC buffer
	out16(MUXCHAN,0x0D00);		// Write to MUX register - SW trigger, UP, SE, 5v, ch 0-0 	
								// x x 0 0 | 1  0  0 1  | 0x 7   0 | Diff - 8 channels
								// SW trig |Diff-Uni 5v| scan 0-7| Single - 16 channels
	//printf("\n\nRead multiple ADC\n");
	count = 0x00;
		
	while (count < 0x02)
    {
		chan = ((count & 0x0f) << 4) | (0x0f & count);
		out16(MUXCHAN, (0x0D00 | chan));			// Set channel	 - burst mode off.
		delay(1);							// allow MUX to settle
		out16(AD_DATA, 0); 					// start ADC
		while (!(in16(MUXCHAN) & 0x4000));

		adc_in = in16(AD_DATA);
		if (count == 0x00)
        {
			amp = adc_in;
		}

		fflush( stdout );
  		count++;
  		delay(5);							// Write to MUX register - SW trigger, UP, DE, 5v, ch 0-7 	
  	}
}

void create_data_arr()
{
	// 87000 is found to be a good value to map expected freq to # of freq points needed.
	// the exact time taken to run waveGenerate() will produce desired freq
	temp = (float) 87000 / freq;						// temp to calculate frequency points iteration	needed			
	freq_points = (int) (temp + 0.5); 					// round over
	data = (int *) malloc(freq_points * sizeof(int)); 	// allocate memory for size of freq
}

void* userInput()
{
	while (1)
   {
   		temp_amp = amp; // set temp_amp as the previous measured amp value
   		toggle();
		potentiometer();
		pthread_mutex_lock( &mutex );

      	if (amp < temp_amp - 100 || amp > temp_amp + 100) 
      	{
      		printf("\a");
      		condition = 2; 		// data generation
      	}
      	
      	if (dio_in != temp_dio && (temp_dio & 8) != 0) // if dio is updated. and not main switch turned off
      	{
      		printf("\a");
      		update_settings();
      	}
      	dio_in = temp_dio;		// update dio_in
      	pthread_cond_signal( &cond );      
      	pthread_mutex_unlock( &mutex );
   	}

   	return 0;
}


void* dataGenerate()
{
	while (1)
    {
		pthread_mutex_lock(&mutex); // 

		while (condition == 1) // while wave is being generated, pthread for data generation (condition == 2) is on hold
        {
			pthread_cond_wait( &cond, &mutex );
		}

		condition = 2; // data generation mode
		file = fopen("wave1.txt", "w");
		for (i = 0; i < freq_points; i++)
        {
			if (waveform == SINE) 
			{
				delta = (float) (2.0 * PI) / (float) freq_points;	// increment
				dummy = (sinf(i*delta) + 1.0) * amp / 2;				// add offset +  scale
			}
			if (waveform == SQUARE)
			{
				if (i < freq_points / 2) {
					dummy= 0x0000;
				}
				else {
					dummy = amp;
				}
			}
			if (waveform == TRIANGULAR) {
				if (i < freq_points / 2) {
					dummy= (float) i / (float) (freq_points / 2) * amp;
				}
				else {
					dummy = (float) (freq_points - 1 - i) / (float) (freq_points / 2) * amp;
				}
			}
			if (waveform == SAWTOOTH) {
				delta = (float) amp / (float) freq_points;	// gradient
				dummy = i * delta;
			}
		
			data[i] = (unsigned) dummy;	
			fprintf(file, "%d\n", data[i]);
		}
		fclose(file);
		rename("wave1.txt", "wave.txt");  // once the file is ready, rename it to wave
    	// if the amp is the same, then condition will remain as 1
		condition = 1; // ready to output wave
      	
		pthread_cond_signal( &cond );
		pthread_mutex_unlock( &mutex );
	}
	
	return 0;
}

int waveGenerate(void)
{
	float delta = (2.0 * PI) / POINTS_PER_CYCLE; //local variables
    float value; //variable setup

    pthread_mutex_lock(&state.mutex);
    
    for(i = 0; i < POINTS_PER_CYCLE; i++) {
        switch(state.type) {
            case SINE:
                value = sinf((float)(i * delta));
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
    
    pthread_mutex_unlock(&state.mutex);
}

void init_hardware(void) {
	memset(&info, 0, sizeof(info));
	if (pci_attach(0) < 0) {
  		perror("pci_attach");
  		exit(EXIT_FAILURE);
  	}

												/* Vendor and Device ID */
	info.VendorId=0x1307;
	info.DeviceId=0x01;

	if ((hdl=pci_attach_device(0, PCI_SHARE|PCI_INIT_ALL, 0, &info)) == 0) {
		perror("pci_attach_device");
		exit(EXIT_FAILURE);
	}
												// Determine assigned BADRn IO addresses for PCI-DAS1602			

	//printf("\nDAS 1602 Base addresses:\n\n");
	for (i = 0; i < 5; i++) {
		badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
	}
	
	//printf("\nReconfirm Iobase:\n");  		// map I/O base address to user space						
	for (i = 0; i < 5; i++)
    {					// expect CpuBaseAddress to be the same as iobase for PC
		iobase[i] = mmap_device_io(0x0f, badr[i]);
	}													
												// Modify thread control privity
	if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
		perror("Thread Control");
		exit(1);
	}
}

int main(int argc, char* argv[]) {
	signal(SIGINT, interrupt);

	printf("\fDemonstration Routine for PCI-DAS 1602\n\n");
	
	if (argc != 3)
    {
        printf("Wrong number of arguments!\n");
        display_usage();
		printf("Incorrect usage. Exiting.\nExit status: 1\n");
        return EXIT_FAILURE;
    }

    parse_arguments(argc, argv);
    printf("\nWelcome to the waveform generator program!\n");
    printf("Switch off the top switch to kill the program.\n");
    printf("Toggle the other switch to trigger keyboard input.\n");
    printf("Turn the potentiometer to change the amplitude anytime.\n");
    printf("Current settings - Waveform: %s, Frequency: %i\n", temp_waveform, freq);

	init_hardware();
		
	// thread functions
	pthread_create(NULL, NULL, &userInput, NULL);
	pthread_create(NULL, NULL, &dataGenerate, NULL);
	return waveGenerate();
}