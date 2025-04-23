#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hw/pci.h>
#include <hw/inout.h>
#include <sys/neutrino.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

//#include <cstdio>
//#include <cstdlib>
//#include <csignal>


#define INTERRUPT iobase[1] + 0  // Badr1 + 0 : also ADC register
#define MUXCHAN iobase[1] + 2    // Badr1 + 2
#define TRIGGER iobase[1] + 4    // Badr1 + 4
#define AUTOCAL iobase[1] + 6    // Badr1 + 6
#define DA_CTLREG iobase[1] + 8  // Badr1 + 8
#define AD_DATA iobase[2] + 0    // Badr2 + 0
#define AD_FIFOCLR iobase[2] + 2 // Badr2 + 2
#define TIMER0 iobase[3] + 0     // Badr3 + 0
#define TIMER1 iobase[3] + 1     // Badr3 + 1
#define TIMER2 iobase[3] + 2     // Badr3 + 2
#define COUNTCTL iobase[3] + 3   // Badr3 + 3
#define DIO_PORTA iobase[3] + 4  // Badr3 + 4
#define DIO_PORTB iobase[3] + 5  // Badr3 + 5
#define DIO_PORTC iobase[3] + 6  // Badr3 + 6
#define DIO_CTLREG iobase[3] + 7 // Badr3 + 7
#define PACER1 iobase[3] + 8     // Badr3 + 8
#define PACER2 iobase[3] + 9     // Badr3 + 9
#define PACER3 iobase[3] + a     // Badr3 + a
#define PACERCTL iobase[3] + b   // Badr3 + b
#define DA_Data iobase[4] + 0    // Badr4 + 0
#define DA_FIFOCLR iobase[4] + 2 // Badr4 + 2
#define DEBUG 1

int badr[5];
uintptr_t iobase[6];
int wave_mode;
int mode;
int ctrl_mode;
double frequency, term_frequency = 1, pot_frequency = 1, frequencyHolder =1;
double amplitude, term_amplitude = 1, pot_amplitude = 1;
unsigned short data[100];
uintptr_t dio_in;
uint16_t adc_in;
pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int count;
unsigned short chan;

int fd; //file writing

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int condition = 0;

pthread_t tasks[3];
pthread_attr_t attr;
void *status;

int int_sig = 1;
int initial_switch = 0;

void setup()
{
    struct pci_dev_info info;
    void *hdl;
    int i;

    memset(&info, 0, sizeof(info));
    if (pci_attach(0) < 0)
    {
        perror("pci_attach");
        exit(EXIT_FAILURE);
    }

    /* Vendor and Device ID */
    info.VendorId = 0x1307;
    info.DeviceId = 0x01;

    if ((hdl = pci_attach_device(0, PCI_SHARE | PCI_INIT_ALL, 0, &info)) == 0)
    {
        perror("pci_attach_device");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < 6; i++)
    { // Another printf BUG ? - Break printf to two statements
        if (info.BaseAddressSize[i] > 0)
        {
            printf("Aperture %d  Base 0x%x Length %d Type %s\n", i,
                   PCI_IS_MEM(info.CpuBaseAddress[i]) ? (int)PCI_MEM_ADDR(info.CpuBaseAddress[i]) : (int)PCI_IO_ADDR(info.CpuBaseAddress[i]), info.BaseAddressSize[i],
                   PCI_IS_MEM(info.CpuBaseAddress[i]) ? "MEM" : "IO");
        }
    }

    printf("IRQ %d\n", info.Irq);
    // Assign BADRn IO addresses for PCI-DAS1602
    if (DEBUG)
    {
        printf("\nDAS 1602 Base addresses:\n\n");
        for (i = 0; i < 5; i++)
        {
            badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
            if (DEBUG)
                printf("Badr[%d] : %x\n", i, badr[i]);
        }

        printf("\nReconfirm Iobase:\n"); // map I/O base address to user space
        for (i = 0; i < 5; i++)
        { // expect CpuBaseAddress to be the same as iobase for PC
            iobase[i] = mmap_device_io(0x0f, badr[i]);
            printf("Index %d : Address : %x ", i, badr[i]);
            printf("IOBASE  : %x \n", iobase[i]);
        }
    }
    // Modify thread control privity
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1)
    {
        perror("Thread Control");
        exit(1);
    }
}


void signalHandler(int signum) {
	int i;
	long t;
	int rc;
	int kill;
	
	sleep(1);
	
    //printf("\nInterrupt signal (%d) received. Exiting program...\n", signum);
    printf("\nInterrupt signal received. Exiting program now...\n", signum);

    close(fd);
    
    printf("Goodbye!\n");
    
    exit(signum);
}


void generate_waveform(int type)
{
	int i;
	float delta, dummy;
    //amplitude = term_amplitude * pot_amplitude; // 1 to 5V
    
    delta=(2.0*M_PI)/100.0;
    for (i = 0; i < 100; i++)
    {
        //double angle = (2 * M_PI * i) / 100;
        switch (type)
        {
        case 0: // Sine
  			dummy= ((sinf((float)(i*delta))) + 1) * 0x8000 ;
  			data[i]= (unsigned) dummy;			// add offset +  scale
            break;
        case 1: // Square
            data[i] = (unsigned)(i < 50) ? 0x0000 : 0xFFFF;
            break;
        case 2: // Triangle
            data[i] = (i < 50) ? (0xFFFF * i / 50) : (0xFFFF - (0xFFFF * (i - 50) / 50));
            break;
        case 3: // Sawtooth
            data[i] = 0xFFFF * i / 100;
            break;
        }
    }
}

void write_file(){
    int fd;
    char *str="Data";
    int n=strlen(str)+1;

    if ((fd=open("file.dat",O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR))<0){
        perror("cannot open");
        exit(1);
        }

    if (write(fd,str,n)!=n){
        printf("Cannot write");
        exit(1);
        }
}

void *sollog_dac_task(void *arg) // Output to Voltmeter
{
	int i;
	float tmp_amplitude, tmp_frequency;
	int df;
	
	printf("Running DAC task");
	sleep(5);
        
        //frequency = pot_frequency * term_frequency; // 1 to 10 Hz
    while(int_sig){

        for (i = 0; i < 100; i++)
        {
            out16(DA_CTLREG, 0x0a23); // DA Enable, #0, #1, SW 5V unipolar		2/6
            out16(DA_FIFOCLR, 0);     // Clear DA FIFO  buffer
            
            pthread_mutex_lock(&status_mutex);
           	if( condition == 0 )
         		pthread_cond_wait( &cond, &status_mutex );
         	condition =0;
            
            
            out16(DA_Data, (short)pot_amplitude);
            out16(DA_CTLREG, 0x0a43); // DA Enable, #1, #1, SW 5V unipolar		2/6
            out16(DA_FIFOCLR, 0);     // Clear DA FIFO  buffer
            out16(DA_Data, (short)pot_amplitude);
            printf("DAC Data [%4x]: %4x\r", i, i); // print DAC

            fflush(stdout);
  	
  			// tmp_frequency = term_frequency*pot_frequency;
  			// df = 1000000.0/tmp_frequency/100 - 200000/100;
  			// usleep(df);
            sleep(1);
  			pthread_cond_signal( &cond ); 
  			pthread_mutex_unlock(&status_mutex);
  			//sleep(1);
        }
        }
        

     printf("DAC work is done\n");
   	 pthread_exit((void *) arg);
   	 
}

void *sollog_adc_task(void *arg)
{
    int len;
    char buffer[32];

    out16(INTERRUPT, 0x60c0); // sets interrupts	 - Clears
    out16(TRIGGER, 0x2081);   // sets trigger control: 10MHz, clear, Burst off,SW trig. default:20a0
    out16(AUTOCAL, 0x007f);   // sets automatic calibration : default

    out16(AD_FIFOCLR, 0); // clear ADC buffer
    out16(MUXCHAN, 0x0D00);

    printf("\n\nRead multiple ADC\n");
    
   // Adjustment through potentiometer
    count = 0x00;
    while(int_sig){
    pthread_mutex_lock(&status_mutex);
        if(condition == 1 )
         		pthread_cond_wait( &cond, &status_mutex );
        condition =1;
        chan = ((count & 0x0f) << 4) | (0x0f & count);
        out16(MUXCHAN, 0x0D00 | chan); // Set channel	 - burst mode off.
        delay(1);                      // allow mux to settle
        out16(AD_DATA, 0);             // start ADC
        while (!(in16(MUXCHAN) & 0x4000));

        adc_in = in16(AD_DATA);

        if (count == 0x00) // Frequency
        {
            pot_frequency = (double)adc_in;
            //Write to file
            len = sprintf(buffer, "Solenoid 0: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                    perror("write failed");
                    exit(1);
            }        
        
        }
        if (count == 0x01) // Amplitude
        {
            pot_amplitude =(double)adc_in;
            //Write to file
            len = sprintf(buffer, "Solenoid 1: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                    perror("write failed");
                    exit(1);
            }

        }
        printf("pot_frequency:  %f\n", pot_frequency);
        printf("pot_amplitude:  %f\n", pot_amplitude);
        //printf("R value:  %f\n", pot_amplitude);
        
        
        
        fflush(stdout);
        count++;
        if (count == 0x02)count = 0x00;
        delay(1); // Write to MUX register - SW trigger, UP, DE, 5v, ch 0-7

        //generate_waveform(wave_mode);
        //sleep(1);
        pthread_cond_signal( &cond );  
        pthread_mutex_unlock(&status_mutex);
        }

    printf("ADC work is done\n");
    pthread_exit((void *) arg);
}


void *dac_task(void *arg) // Output to Voltmeter
{
	int i;
	float tmp_amplitude, tmp_frequency;
	int df;
	
	printf("Running DAC task");
	sleep(5);
        
        //frequency = pot_frequency * term_frequency; // 1 to 10 Hz
    while(int_sig){

        for (i = 0; i < 100; i++)
        {
            out16(DA_CTLREG, 0x0a23); // DA Enable, #0, #1, SW 5V unipolar		2/6
            out16(DA_FIFOCLR, 0);     // Clear DA FIFO  buffer
            
            pthread_mutex_lock(&status_mutex);
           	if( condition == 0 )
         		pthread_cond_wait( &cond, &status_mutex );
         	condition =0;
            
   			if (wave_mode == 0) {
   				tmp_amplitude = (float)(data[i]-0x7FFF)*pot_amplitude+0x7FFE;
   			}
   			else {
   				tmp_amplitude = (float)data[i]*pot_amplitude;
   			}
            
            
            
            out16(DA_Data, (short)tmp_amplitude);
            out16(DA_CTLREG, 0x0a43); // DA Enable, #1, #1, SW 5V unipolar		2/6
            out16(DA_FIFOCLR, 0);     // Clear DA FIFO  buffer
            out16(DA_Data, (short)tmp_amplitude);
            printf("DAC Data [%4x]: %4x\r", i, i); // print DAC

            fflush(stdout);
  	
  			tmp_frequency = term_frequency*pot_frequency;
  			df = 1000000.0/tmp_frequency/100 - 200000/100;
  			usleep(df);
  			pthread_cond_signal( &cond ); 
  			pthread_mutex_unlock(&status_mutex);
  			//sleep(1);
        }
        }
        

     printf("DAC work is done\n");
   	 pthread_exit((void *) arg);
}

void *adc_task(void *arg)
{
    int len;
    char buffer[32];

    out16(INTERRUPT, 0x60c0); // sets interrupts	 - Clears
    out16(TRIGGER, 0x2081);   // sets trigger control: 10MHz, clear, Burst off,SW trig. default:20a0
    out16(AUTOCAL, 0x007f);   // sets automatic calibration : default

    out16(AD_FIFOCLR, 0); // clear ADC buffer
    out16(MUXCHAN, 0x0D00);

    printf("\n\nRead multiple ADC\n");
    
   // Adjustment through potentiometer
    count = 0x00;
    while(int_sig){
    pthread_mutex_lock(&status_mutex);
        if(condition == 1 )
         		pthread_cond_wait( &cond, &status_mutex );
        condition =1;
        chan = ((count & 0x0f) << 4) | (0x0f & count);
        out16(MUXCHAN, 0x0D00 | chan); // Set channel	 - burst mode off.
        delay(1);                      // allow mux to settle
        out16(AD_DATA, 0);             // start ADC
        while (!(in16(MUXCHAN) & 0x4000));

        adc_in = in16(AD_DATA);

        if (count == 0x00) // Frequency
        {
            pot_frequency = (double)adc_in / 0xFFFF/2 +0.5; // 0 to 1
            //Write to file
            len = sprintf(buffer, "Solenoid 0: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                    perror("write failed");
                    exit(1);
            }        
        
        }
        if (count == 0x01) // Amplitude
        {
            pot_amplitude =0.5+ 0.5* (double)(adc_in-1.0) / 0xFFFF; // 0 to 1
            pot_amplitude *= (term_amplitude/2.5) ;
            //Write to file
            len = sprintf(buffer, "Solenoid 1: %d\n", adc_in);	
            if (write(fd, buffer, len) != len) {
                    perror("write failed");
                    exit(1);
            }

        }
        printf("pot_frequency:  %f\n", pot_frequency);
        printf("pot_amplitude:  %f\n", pot_amplitude);
        //printf("R value:  %f\n", pot_amplitude);
        
        
        
        fflush(stdout);
        count++;
        if (count == 0x02)count = 0x00;
        delay(1); // Write to MUX register - SW trigger, UP, DE, 5v, ch 0-7

        //generate_waveform(wave_mode);
        //sleep(1);
        pthread_cond_signal( &cond );  
        pthread_mutex_unlock(&status_mutex);
        }

    printf("ADC work is done\n");
    pthread_exit((void *) arg);
}

void *dio_task(void *arg)
{
	
	while(1){
    out8(DIO_CTLREG, 0x90);
    dio_in = in8(DIO_PORTA);
    if ( in8(DIO_PORTA) == 0xFF)
{
    		printf("\nInterrupt signal from switch received. Exiting program...\n");
    		//printf("\nInterrupt signal SIGINT received. Exiting program now...\n", signum);
    		printf("Goodbye!\n");
            close(fd);
            exit(0);
            
     }
     }
     
   	 //return NULL;
}

void *user_io_task(void *arg)
{
    char command[100];
    while (1)
    {
        printf("Enter command (sine, square, tri, saw, freq <value>, amp <value>, exit): ");
        fgets(command, sizeof(command), stdin);
        pthread_mutex_lock(&status_mutex);
        if (strncmp(command, "sine", 4) == 0)
            wave_mode = 0;
        else if (strncmp(command, "square", 6) == 0)
            wave_mode = 1;
        else if (strncmp(command, "tri", 3) == 0)
            wave_mode = 2;
        else if (strncmp(command, "saw", 3) == 0)
            wave_mode = 3;
        else if (strncmp(command, "freq", 4) == 0)
            frequency = atof(command + 5);
        else if (strncmp(command, "amp", 3) == 0)
            amplitude = atof(command + 4);
        else if (strncmp(command, "exit", 4) == 0)
            exit(0);
        generate_waveform(wave_mode);
        pthread_mutex_unlock(&status_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
	//pthread_t tasks[3];
	int i;
	long arg;
	//void *status;
	//pthread_attr_t attr;

	long t;
	int rc;
	
    struct sigaction sa;

    // Open the file for writing
    fd = open("log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd <0) {
        perror("open failed");
        return 1;
    }




    sa.sa_handler = signalHandler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    // Register signal handler for SIGINT (Ctrl+C)
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        printf("Error: Cannot handle SIGINT\n");
        return 1;
    }

    printf("Press Ctrl+C or flip the switch to exit...\n");

    setup(); // Initial setup for the Peripheral
    // Check arguement
    
    
    if (strcmp(argv[1], "wf_gen") == 0) {
        if (argc != 6) {
            printf("Usage %s %s <ctrl_mode> <waveform> <frequency> <amplitude>\n", argv[0], argv[1]);
            printf("Control mode available: kbd, sol\n");
            printf("Waveform available: sine, square, triangle, sawtooth\n");
            printf("Frequency range: 0.1~ 1.0\n");
            printf("Amplitude range: 0.5 ~ 2.5\n");
            return 1;
        }
        mode = 0;
        
        if (strcmp(argv[2], "kbd") == 0) {
        ctrl_mode = 0;
        }
        else if (strcmp(argv[2], "sol") == 0) {
        ctrl_mode = 1;
        }
        
        if (strcmp(argv[3], "sine") == 0){
        wave_mode = 0;
        }

        else if (strcmp(argv[3], "square") == 0){
            wave_mode = 1;
        }

        else if (strcmp(argv[3], "triangle") == 0){
            wave_mode = 2;
        }

        else if (strcmp(argv[3], "sawtooth") == 0){
            wave_mode = 3;
        }
        else{
            printf("invalid waveform!\n");
            printf("sine, square, triangle, sawtooth\n");
            return 1;
        }
        
        // Frequency
        term_frequency = atof(argv[4]);
        if(term_frequency <0.1 || term_frequency >1) {
            printf("Fourth argument out of range. Available frequency range: 0.1 ~ 1.0");
            return 1;
        }

        //Amplitude
        term_amplitude = atof(argv[5]);
        if(term_amplitude <0.5 || term_amplitude >2.5){
            printf("Fifth argument out of range. Available amplitude range: 0.5 ~ 2.5");
            return 1;
        }
    }
    else if (strcmp(argv[1], "sol_log") == 0) {
        mode = 1;
        if (argc != 2) {
            printf("Usage %s %s\n", argv[0], argv[1]);
            return 1;
        }
    }
    else {
        printf("Mode not found!");
        printf("Mode available: wf_gen, sol_log");
        return 1;
    }


    generate_waveform(wave_mode);
 
    
    
    
    
    if (mode == 0){
        pthread_create(&tasks[0], NULL, dac_task, NULL);
        pthread_create(&tasks[1],NULL, adc_task, NULL);
        pthread_create(&tasks[2], NULL, dio_task, NULL);
        //pthread_create(&tasks[3], NULL, user_io_task, NULL);        
    }
    else if (mode ==1){
        pthread_create(&tasks[0], NULL, sollog_dac_task, NULL);
        pthread_create(&tasks[1],NULL, sollog_adc_task, NULL);
        pthread_create(&tasks[2], NULL, dio_task, NULL);
        //pthread_create(&tasks[3], NULL, user_io_task, NULL);
    }

    for (i=0; i<3; i++){
    	pthread_join(tasks[i], NULL);
    }
    
    //printf("Program ending");
	while(1);
    return 0;
}