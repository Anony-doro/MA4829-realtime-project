// 09September 2005
//******************************************************************************************************
// Performs basic I/O for the Omega PCI-DAS1602 
//
// Demonstration routine to demonstrate pci hardware programming
// Demonstrated the most basic DIO and ADC and DAC functions
// - excludes FIFO and strobed operations 
//			22 Sept 2016 : Restructured to demonstrate Sine wave to DA
//
// G.Seet - 26 August 2005
//******************************************************************************************************

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
	
int badr[5];															// PCI 2.2 assigns 6 IO base addresses

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// Global variables
// enum to define waveform types
enum WaveformType {
    SINE,
    SQUARE,
    TRIANGLE,
    SAWTOOTH
};

// devices setup
struct pci_dev_info info;
void *hdl;
uintptr_t iobase[6];
uint16_t adc_in;
		
// for UI
int waveform = SINE; 
bool invalid_waveform = true;
bool invalid_freq = true;
int scanf_result;
int user_option;
char* temp_waveform;
int temp_freq;
int freq = 100;

// a/d input
unsigned int count;
unsigned short chan;
int temp_dio;		// record current dio, to compare with previous dio
uintptr_t dio_in = 0xff;
int temp_amp;				// record current potentiometer reading, to compare with previous amp
int amp = 0xffff;

// wave generation
unsigned int i;
unsigned int* data; 	// initilalise an int array pointer
float delta, dummy;
float temp;
int freq_points;
int int_signal = 0;
int abort_signal = 0;

// file IO
FILE *file;				// temp file writer to store generated data
FILE *wave_file;		// wave file reader to generate wave 
char line[256];			// act as buffer for file IO

// threading	
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int condition = 0;  	// 0: initial, 1: generating wave, 2: generating data

// functions declaration
void interrupt_handler();
void reset_settings();
void parse_arguments(int argc, char *argv[]);
void display_usage();
void update_settings();
void check_waveform();
void check_freq();
void check_toggle();
void check_pot();
void create_data_arr();
// thread functions
void* check_input();
void* generate_data();
int generate_wave();


void interrupt_handler() 
{
	int_signal = 1;
}

void reset_settings()
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

void parse_arguments(int argc, char *argv[]) {
    temp_waveform = argv[1];
    temp_freq = atoi(argv[2]);
    
    check_waveform(argc, argv);
    check_freq(argc, argv);
}

void display_usage() 
{
    printf("Usage: program_name [waveform_type] [frequency]\n");
    printf("Supported waveform types: sine, square, triangle, sawtooth\n");
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
		printf("\nPlease select from these 4 options: sine / square / triangle / sawtooth\n");
		printf("New waveform: \n");
		scanf("%s", temp_waveform);
		check_waveform();
	}
	else if (user_option == 2)
	{
		printf("New frequency (1 - 1000): \n");
		scanf_result = scanf("%d", &temp_freq);
		while (getchar() != '\n'); 	// used to clear buffer, handle error when input not numeric
		if (scanf_result == 0) 		// if result == 0 means scanf face error, hence change temp_freq = -1
		{
			temp_freq = -1;			// help check_freq() detect incorrect input
		}
		check_freq();
	}
	
	printf("Success!\n");
	printf("New setting - Waveform: %s, Frequency: %i\n", temp_waveform, freq);
	printf("\nPlease toggle the switches again to trigger keyboard input\n");

	condition = 2;
	pthread_cond_signal( &cond );      
	pthread_mutex_unlock( &mutex );
}

void check_waveform()
{
    do
    {
        invalid_waveform = false;
        if (strcmp(temp_waveform, "sine") == 0) 
        {
            waveform = SINE;
        }
        else if (strcmp(temp_waveform, "square") == 0) 
        {
            waveform = SQUARE;
        } 
        else if (strcmp(temp_waveform, "triangle") == 0)
        {
            waveform = TRIANGLE;
        } 
        else if (strcmp(temp_waveform, "sawtooth") == 0) 
        {
            waveform = SAWTOOTH;
        }
        else
        {
            invalid_waveform = true;
            printf("\nYour input waveform is invalid.\n");
            printf("Please select from these 4 options: sine / square / triangle / sawtooth\n");
            printf("New waveform: \n");
            scanf("%s", temp_waveform);
        }
    }
    while (invalid_waveform);
}

void check_freq()
{
    do
    {
        invalid_freq = false;
        if (temp_freq < 1 || temp_freq > 1000) 
        {
            invalid_freq = true;
            printf("Invalid frequency, please input within (1 - 1000): \n");
            printf("New frequency: \n");
            scanf("%d", &temp_freq);
            while(getchar() != '\n'); // used to clear buffer, handle error when input not numeric
        }
        else
        {
            freq = temp_freq;
            create_data_arr();
        }
    }
    while (invalid_freq);
}

void check_toggle()
{	
								//*****************************************************************************
								//Digital Port Functions
								//*****************************************************************************	
	out8(DIO_CTLREG, 0x90);		// Port A : Input,  Port B : Output,  Port C (upper | lower) : Output | Output			

	temp_dio = in8(DIO_PORTA); 	// Read Port A
																						
	out8(DIO_PORTB, temp_dio);	// output Port A value -> write to Port B
}

void check_pot()
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
	count=0x00;
		
	while(count <0x02) {
		chan= ((count & 0x0f)<<4) | (0x0f & count);
		out16(MUXCHAN,0x0D00|chan);			// Set channel	 - burst mode off.
		delay(1);							// allow mux to settle
		out16(AD_DATA,0); 					// start ADC 
		while(!(in16(MUXCHAN) & 0x4000));

		adc_in=in16(AD_DATA);
		if (count == 0x00) {
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
	// the exact time taken to run generate_wave() will produce desired freq
	temp = (float) 87000 / freq;						// temp to calculate frequency points iteration	needed			
	freq_points = (int) (temp + 0.5); 					// round over
	data = (int *) malloc(freq_points * sizeof(int)); 	// allocate memory for size of freq
}

void* check_input()
{
	while( 1 )
   {
   		temp_amp = amp;
   		check_toggle();
		check_pot();
		pthread_mutex_lock( &mutex );

      	if (amp < temp_amp - 100 || amp > temp_amp + 100) 
      	{
      		printf("\a");
      		condition = 2; 		// to let generate_data run
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


void* generate_data() {
	while (1)
	{
		//printf("generate data running\n");
		pthread_mutex_lock( &mutex );
	
		while (condition == 1) // while no change in input don't generate new data
		{
			//printf("locked\n");
			pthread_cond_wait( &cond, &mutex );
		}
		condition = 2;
		file = fopen("wave1.txt", "w");
		for (i = 0; i < freq_points; i++) {
			if (waveform == SINE) 
			{
				delta = (float) (2.0 * 3.142) / (float) freq_points;	// increment
				dummy = (sinf(i*delta) + 1.0) * amp / 2;				// add offset +  scale
			}
			else if (waveform == SQUARE)
			{
				if (i < freq_points / 2) {
					dummy= 0x0000;
				}
				else {
					dummy = amp;
				}
			}
			else if (waveform == TRIANGLE) {
				if (i < freq_points / 2) {
					dummy= (float) i / (float) (freq_points / 2) * amp;
				}
				else {
					dummy = (float) (freq_points - 1 - i) / (float) (freq_points / 2) * amp;
				}
			}
			else if (waveform == SAWTOOTH) {
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

int generate_wave(void)
{
	while( 1 )
	{
		pthread_mutex_lock( &mutex );
		while( condition != 1) 							// while new data is generated, lock
		{ 
			pthread_cond_wait( &cond, &mutex ); 		// while new data is generated, lock
		}
      	
      	wave_file = fopen("wave.txt", "r");  			// open wave file to read from it.
		// resembles for loop, with n lines instead of n freq_points
      	while (fgets(line, sizeof(line), wave_file))	// read each line in file
      	{
      		// line is a string, atoi converts it to int
			out16(DA_CTLREG,0x0a23);					// DA Enable, #0, #1, SW 5V unipolar	2/6
			out16(DA_FIFOCLR, 0);						// Clear DA FIFO  buffer
			out16(DA_Data, (short) atoi(line));

			out16(DA_CTLREG,0x0a43);					// DA Enable, #1, #1, SW 5V unipolar	2/6
			out16(DA_FIFOCLR, 0);						// Clear DA FIFO  buffer
			out16(DA_Data, (short) atoi(line));	
		}
		fclose(wave_file);
      	condition = 1; 			// ready to output wave
      	pthread_cond_signal( &cond );      
      	pthread_mutex_unlock( &mutex );
      	
      	if (int_signal == 1)
      	{
      		printf("Received Ctrl + C signal. Exiting program... \nExit status : 0\n");
			reset_settings();
      		return 0; 			// status 0 if terminated using Ctrl + C
      	}
      	
      	if ((temp_dio & 8) == 0) 	// if the main switch is turned off (mask = 1000)
      	{
      		printf("The main swtich is turned off.\nThe process is terminated.\nExit status: 2\n");
      		reset_settings();
      		return 2; 			//  status 2 if terminated using main switch
      	}
   	}
   return 0 ;
}

int main(int argc, char* argv[]) {
	signal(SIGINT, interrupt_handler);

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
		badr[i]=PCI_IO_ADDR(info.CpuBaseAddress[i]);
		//printf("Badr[%d] : %x\n", i, badr[i]);
	}
	
	//printf("\nReconfirm Iobase:\n");  		// map I/O base address to user space						
	for (i = 0; i < 5; i++) {					// expect CpuBaseAddress to be the same as iobase for PC
		iobase[i] = mmap_device_io(0x0f, badr[i]);	
		//printf("Index %d : Address : %x ", i, badr[i]);
		//printf("IOBASE  : %x \n", iobase[i]);
	}													
												// Modify thread control privity
	if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
		perror("Thread Control");
		exit(1);
	}
		
	// thread functions
	pthread_create(NULL, NULL, &check_input, NULL );
	pthread_create( NULL, NULL, &generate_data, NULL);
	return generate_wave();
}
