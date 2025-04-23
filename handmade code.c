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
	

// Constants
#define PI 3.14159265358979323846
#define POINTS_PER_CYCLE 100
#define MAX_FREQ 1000
#define MIN_FREQ 1


int badr[5];															// PCI 2.2 assigns 6 IO base addresses



uintptr_t dio_in;
uintptr_t iobase[6];
uint16_t adc_in;


//variables for waveform generation
unsigned int i, count;
unsigned short chan;
unsigned int data[100];
float delta, dummy;

//variables for thread control
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int condition = 0;

void initial_setup(){
    struct pci_dev_info info;
    void *hdl;
    int i; // local variable i for: for loop

    memset(&info, 0, sizeof(info)); // clear the info structure
    if (pci_attach(0)<0){
        perror("Failed to attach to PCI device");
        exit(EXIT_FAILURE);
    }
    // Vendor and Device ID
    info.VendorId = 0x1307;
    info.DeviceId = 0x01;

    if ((hdl=pci_attach_device(0, PCI_SHARE|PCI_INIT_ALL, 0, &info)) == 0){
        perror("Failed to attach to PCI device");
        exit(EXIT_FAILURE);
    }

    // Determine assigned BADRn IO addresses for PCI-DAS1602
    for (i=0; i<5; i++){
        badr[i] = PCI_IO_ADDR(info.CpuBaseAddress[i]);
    }

    printf("\nReconfirm Iobase:\n");      //map I/O base address to user space
    for (i=0; i<5; i++){                    // expect CpuBaseAddress to be the same as iobase for PC
        iobase[i] = mmap_device_io(0x0f, badr[i]);
        printf("Index %d : Address : %x\n", i, badr[i]);
        printf("IOBASE : %x\n", iobase[i]);
    }

    // Modify thread control privity
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1){
        perror("Failed to modify thread control privity");
        exit(1);
    }
}

void* check_input(){
    while(1){
        pthread_mutex_lock(&mutex);
        condition = 2;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }
}

void* generate_data(){
    while(1){
        pthread_mutex_lock(&mutex);

        while (condition == 1){
            pthread_cond_wait(&cond);
        }
        condition = 2;
        //file = fopen("wave1.txt", "w");

        delta = (float) (2.0 * 3.142) / (float) POINTS_PER_CYCLE; //increment
        for (i = 0; i < POINTS_PER_CYCLE; i++){
            if (waveform == SINE){
                dummy = (sinf(i*delta) + 1.0) * (0xffff / 2);
                data[i] = (unsigned) dummy;
            }
        while(1){
            for (i = 0; i < POINTS_PER_CYCLE; i++){
                out16(DA_CTLREG, 0x0a23);
                out16(DA_FIFOCLR, 0);
                out16(DA_Data, (short) data[i]);
               
                out16(DA_CTLREG, 0x0a43);
                out16(DA_FIFOCLR, 0);
                out16(DA_Data, (short) data[i]);
            }
        }
        pthread_mutex_unlock(&mutex);
    }
}
}
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
int main(){
    printf("\fWelcome to the Waveform Generator\n\n");

    initial_setup();

        //***************************************** */
        //D/A Port Functions
        //***************************************** */

    pthread_create(NULL, NULL, &check_input, NULL); //later change check_input to check_input()
    pthread_create(NULL, NULL, &generate_data, NULL); //later change generate_data to generate_data()
    //return generate_wave(); //later change generate_wave to generate_wave()
    return 0;
}

